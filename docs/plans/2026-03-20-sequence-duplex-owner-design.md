# Sequence 双工 Owner 设计

## 背景

`galay-kernel` 当前把 `SequenceAwaitableBase` 注册到同一个 `IOController` 的 `READ` 槽里复用：

- 同一 `IOController` 只能安全承载一个 sequence owner
- 第二个 sequence 会静默覆盖第一个 owner
- 即使第二个 sequence 很快失败，如果它在 `await_resume()` 中清理 `SEQUENCE` 槽，也可能把第一个合法 owner 一起删掉

这已经被最小复现验证：

- `test/T98-sequence_owner_conflict.cc`

同时，`galay-http` / `galay-ssl` 的实际使用给出了两个不同需求：

1. 线性或半双工协议流程，希望一个读 sequence 和一个写 sequence 能在同一连接上并发存在
2. TLS 这类状态机流程，一个 sequence 自己就会在读写之间来回切换，天然需要独占整个底层连接的读写所有权

因此，这一轮设计不能简单做成“所有 sequence 都能随便并存”，否则会把读写字节消费边界、错误归属和 wake 归属做乱。

## 目标

- 支持同一个 `IOController` 上同时存在：
  - 一个只读 sequence owner
  - 一个只写 sequence owner
- 保持当前 reactor 热路径为 O(1)
- 不引入 owner 链表、动态广播或任意多 owner 调度
- 为 TLS / SSL 这类会跨读写切换的 sequence 保留“独占双向”的建模方式
- 冲突必须显式失败，不允许静默覆盖

## 非目标

- 不支持同方向多个 sequence 并发消费同一 socket
- 不支持任意多个 sequence owner 同时挂在同一 `IOController`
- 不做字节流级别的自动 demux
- 不在本轮重写 `AwaitableBuilder` / `StateMachineAwaitable` 公共 API

## 方案比较

### 方案 A：保持单 owner，仅做 fail-fast

优点：

- 改动最小
- 风险最低
- 立即消除 silent corruption

缺点：

- 仍然无法支持真正的双工 sequence 并发
- 与用户期望的“像 Tokio split 那样读写并存”不匹配

### 方案 B：读 owner / 写 owner / 双向独占 owner 三态模型

核心思想：

- 读方向一个 owner 槽
- 写方向一个 owner 槽
- sequence 在注册时声明自己的 owner domain：
  - `Read`
  - `Write`
  - `ReadWrite`
- `ReadWrite` owner 原子占用两个方向槽位，作为双向独占 sequence

优点：

- 热路径仍然是固定两个槽位，O(1)
- 对应 Tokio `split()` 的真实语义：一个 reader + 一个 writer
- 对 TLS 这类双向状态机也有正确建模，不会错误拆成两个半边
- 不需要引入额外分发容器、锁或遍历

缺点：

- `IOController`、reactor、`SequenceAwaitableBase` 都要做一轮方向化改造
- `StateMachineAwaitable` / `LinearMachine` 需要补充 domain 推导或显式声明

### 方案 C：任意多个 sequence owner + 通用分发器

优点：

- 表面最通用

缺点：

- 同方向 owner 必须做仲裁，否则无法定义谁消费字节
- reactor 热路径会退化成容器遍历 / 广播 / 二次分发
- `io_uring` token 也要从双槽位模型改成动态 owner 表
- 对性能和正确性都不友好

结论：采用方案 B。

## Tokio 对照

Tokio 的 `TcpStream::split()` / `into_split()` 本质上就是一个读半边和一个写半边并发使用，不是“任意多个 owner 挂同一 stream”。专用 `split()` 走的是更轻量的路径，避免通用 split 的额外开销。

这更接近方案 B，而不是方案 C。

## 最终方案

### 1. 为 sequence 引入 owner domain

新增一个小型 domain 枚举，例如：

```cpp
enum class SequenceOwnerDomain : uint8_t {
    Read,
    Write,
    ReadWrite,
};
```

含义：

- `Read`：只会等待读相关事件
- `Write`：只会等待写相关事件
- `ReadWrite`：sequence 可能跨方向切换，必须独占两个方向

### 2. IOController 从“一个 sequence owner”升级为“两个固定槽位”

`IOController` 保留现有普通 awaitable 的：

- `m_awaitable[READ]`
- `m_awaitable[WRITE]`

同时新增 sequence owner 固定槽位：

- `m_sequence_owner[READ]`
- `m_sequence_owner[WRITE]`

这样可以做到：

- 普通 `RecvAwaitable` / `SendAwaitable` 路径不受影响
- sequence owner 不再挤占普通 awaitable 指针槽
- 双工 sequence 分发仍然是 O(1)

### 3. SequenceAwaitableBase 记录注册 domain

`SequenceAwaitableBase` 需要补这些状态：

- `m_registered_domain`
- `m_requested_domain`
- 只清理自己真正占用的 owner 槽

注册规则：

- `Read` 只占用 `READ` sequence 槽
- `Write` 只占用 `WRITE` sequence 槽
- `ReadWrite` 必须同时占用两个槽，否则注册失败

冲突规则：

- 同方向已有 owner，第二个同方向 owner 直接失败
- 任一方向已有 owner 时，`ReadWrite` owner 注册失败
- `ReadWrite` owner 已在时，任何其他 sequence 注册失败

### 4. 方向推导规则

为了让 API 尽量优雅：

- 线性 `SequenceAwaitable<ResultT, InlineN>` / `AwaitableBuilder` 在 `build()` 时根据步骤类型推导 domain
  - 只有读类步骤：`Read`
  - 只有写类步骤：`Write`
  - 同时包含读写步骤、`connect`、或无法静态确定的复杂步骤：`ReadWrite`
- `StateMachineAwaitable<MachineT>` 默认视为 `ReadWrite`
- 如果后续确实需要更激进的优化，再允许 machine 显式覆写 domain trait

这样能保证：

- 常见 `recv.parse` builder 自动成为只读 owner
- 常见 `send.finish` builder 自动成为只写 owner
- TLS 这类 state machine 默认安全，不会被错误拆成单向 owner

### 5. Reactor 改成按方向分发 sequence owner

#### kqueue / epoll

- `addSequence(IOController*)` 不再只拿一个 owner
- 它分别查看：
  - `READ` sequence owner
  - `WRITE` sequence owner
- 对每个方向独立调用 `prepareForSubmit(...)`
- 最终把需要的读写事件合并成一次 fd 注册
- 事件返回时：
  - 读事件只分发给读方向 sequence owner
  - 写事件只分发给写方向 sequence owner

#### io_uring

- 继续复用 `READ` / `WRITE` 两个 SQE token 槽
- 读方向 sequence owner 用 `READ` token
- 写方向 sequence owner 用 `WRITE` token
- CQE 回来时按 token 槽位回到对应方向 owner

这样不需要把 `io_uring` token 系统重写成动态 owner 表。

### 6. 性能结论

方案 B 预计不会引入可感知的负向性能回退，原因是：

- owner 槽位仍然固定为两个，查找与分发是 O(1)
- 不需要动态分配 owner 容器
- 不需要遍历多个 owner
- 不需要额外的仲裁锁

新增成本主要是：

- 注册时多做一到两次槽位冲突检查
- reactor 处理事件时从“看一个 sequence owner”变成“最多看两个方向 owner”

这点开销相比系统调用和协议解析本身可以忽略。

### 7. 回归验证

至少需要覆盖：

- 同 controller 上一个只读 sequence + 一个只写 sequence 可以并发成功
- 同方向两个 sequence 冲突时显式失败
- 一个双向独占 sequence 会正确阻止其他 sequence 注册
- `T96` timeout 与 `T97` await-context 继续通过
- 现有 parser/builder regression 不退化
- 下游 `galay-http` H2 / WSS 场景在新内核上保持正确

## 推荐结论

推荐采用“读 owner / 写 owner / 双向独占 owner”三态模型。

这是当前最优雅、性能最稳、也最贴近 Tokio split 语义的实现方式：

- 允许真正有意义的双工并发
- 不错误承诺任意多个 sequence 广播
- 让 TLS 这类双向状态机继续有安全的独占语义
