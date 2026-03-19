# Awaitable Builder `readv/writev` 设计

## 背景

`galay-kernel` 当前已经完成了 Awaitable 状态机内核收敛：

- 线性 `AwaitableBuilder<ResultT, InlineN, FlowT>`
- 显式状态机入口 `AwaitableBuilder<ResultT>::fromStateMachine(...)`
- 底层统一的 `StateMachineAwaitable`

但 builder 公开面目前只覆盖单 buffer 的：

- `.recv(char* buffer, size_t length)`
- `.send(const char* buffer, size_t length)`

与此同时，`TcpSocket` 已经原生支持：

- `readv(...)`
- `writev(...)`

这造成两个直接问题：

1. 上层协议库无法用 builder 直接表达 scatter-gather IO。
2. 像 `galay-http` 这样的仓库，即使只是“header + body 两段发送”，也需要继续保留一层协议私有 awaitable 或手工状态机，代码收敛不到 builder 公开面上。

## 目标

本轮目标：

- 为线性 `AwaitableBuilder` 新增显式 `readv(...)` / `writev(...)`
- 为状态机动作新增 `waitReadv(...)` / `waitWritev(...)`
- 让线性 builder 和 `fromStateMachine(...)` 共用同一套 `READV/WRITEV` 调度桥
- 保持 borrowed `iovec` 语义，与 `TcpSocket::readv/writev` 对齐
- 为 `galay-http`、后续 `galay-redis` / `galay-etcd` / `galay-mysql` 的协议发送路径提供统一承载能力

## 非目标

- 不修改现有 `.recv/.send` 语义
- 不把 `.recv/.send` 扩成“既支持单 buffer 又支持 iovec”的重载混合接口
- 不在本轮引入 owning iovec 容器
- 不在本轮改变 `AwaitableStateMachine` 的 handler 签名
- 不在本轮直接重写所有上层仓库，只先补齐 kernel 能力

## 方案比较

### 方案 A：为 builder 新增显式 `readv/writev`

示意：

```cpp
auto aw = AwaitableBuilder<Result, 4, Flow>(&controller, flow)
    .readv<&Flow::onReadv>(read_iovecs, read_count)
    .parse<&Flow::onParse>()
    .writev<&Flow::onWritev>(write_iovecs, write_count)
    .build();
```

优点：

- API 语义最清楚
- 与 `TcpSocket::readv/writev` 命名完全一致
- 文档、测试、上层迁移都容易对齐

缺点：

- 需要在 `MachineAction` / `LinearMachine` / `StateMachineAwaitable` 里补齐 iovec 通路

### 方案 B：扩展现有 `.recv/.send`，支持 iovec 重载

优点：

- 表面 API 数量最少

缺点：

- `.recv/.send` 同时承载单 buffer 与 scatter-gather，阅读时不直观
- 文档和回归测试容易混淆“普通 IO”与“向量 IO”

### 方案 C：只给 `fromStateMachine(...)` 增加 `waitReadv/waitWritev`

优点：

- 内核改动最小

缺点：

- 线性 builder 仍然不完整
- 上层仓库还是要继续保留很多自定义 awaitable

结论：采用方案 A。

## 最终方案

### 1. Builder 公开面新增显式 `readv/writev`

新增以下接口：

```cpp
template <auto Handler, size_t N>
AwaitableBuilder& readv(std::array<struct iovec, N>& iovecs, size_t count = N);

template <auto Handler, size_t N>
AwaitableBuilder& readv(struct iovec (&iovecs)[N], size_t count = N);

template <auto Handler, size_t N>
AwaitableBuilder& writev(std::array<struct iovec, N>& iovecs, size_t count = N);

template <auto Handler, size_t N>
AwaitableBuilder& writev(struct iovec (&iovecs)[N], size_t count = N);
```

对应 handler 形态：

```cpp
void onReadv(SequenceOps<Result, InlineN>& ops, ReadvIOContext& ctx);
void onWritev(SequenceOps<Result, InlineN>& ops, WritevIOContext& ctx);
```

设计原则：

- 延续当前 borrowed `iovec` 约束，不做额外 copy
- 运行时 count 校验复用 `ReadvIOContext` / `WritevIOContext` 现有 guard 语义
- `.recv/.send` 与 `.readv/.writev` 并存，职责边界清晰

### 2. 状态机动作新增 `waitReadv/waitWritev`

在 `MachineSignal` 中新增：

- `kWaitReadv`
- `kWaitWritev`

在 `MachineAction<ResultT>` 中新增：

- `const struct iovec* iovecs = nullptr`
- `size_t iov_count = 0`

以及两个工厂：

```cpp
static MachineAction waitReadv(const struct iovec* iovecs, size_t count);
static MachineAction waitWritev(const struct iovec* iovecs, size_t count);
```

这能让：

- 线性 builder 的 `LinearMachine`
- 自定义 `fromStateMachine(...)`

都统一表达向量 IO。

### 3. `LinearMachine` 补齐 `kReadv/kWritev` 节点

`LinearMachine::NodeKind` 新增：

- `kReadv`
- `kWritev`

节点额外保存：

- `const struct iovec* iovecs`
- `size_t iov_count`

`advance()` 中：

- `kReadv -> MachineAction::waitReadv(...)`
- `kWritev -> MachineAction::waitWritev(...)`

`invokeIONode(...)` 仍然复用：

- `ReadvIOContext`
- `WritevIOContext`

不需要引入新的 ops 模型。

### 4. `StateMachineAwaitable` 扩展 active task 桥接

当前 `StateMachineAwaitable` 只把：

- `kWaitRead -> RECV`
- `kWaitWrite -> SEND`

映射到调度器。

本轮改为同时支持：

- `kWaitReadv -> READV`
- `kWaitWritev -> WRITEV`

内部需要补：

- `ActiveKind::kReadv`
- `ActiveKind::kWritev`
- `ReadvIOContext m_readv_context`
- `WritevIOContext m_writev_context`

这样可以直接复用现有 scheduler / reactor 中已经存在的：

- `addReadv(...)`
- `addWritev(...)`
- `READV`
- `WRITEV`

无需另起新的调度通路。

### 5. `ReadvIOContext` / `WritevIOContext` 增加 runtime-span 构造

builder 节点持有的是运行时：

- `const struct iovec*`
- `size_t count`

因此现有只接受模板数组的构造函数不够用。

本轮补充：

```cpp
explicit ReadvIOContext(std::span<const struct iovec> iovecs);
explicit WritevIOContext(std::span<const struct iovec> iovecs);
```

保持：

- 0-copy borrowed metadata
- 与现有 awaitable 行为一致

### 6. 回调接口保持不变

本轮不修改 `AwaitableStateMachine` 的协议：

```cpp
void onRead(std::expected<size_t, IOError>);
void onWrite(std::expected<size_t, IOError>);
```

原因：

- `readv` 完成后本质仍然是“读了多少字节”
- `writev` 完成后本质仍然是“写了多少字节”
- 保持概念稳定，避免把本轮扩展放大成新一轮公开面破坏

## 测试策略

至少新增四类验证：

1. surface test
- 验证 `.readv/.writev`、`waitReadv/waitWritev` 公开面存在

2. builder 往返测试
- 使用 `socketpair(...)` 或本地 TCP
- 一侧 builder `writev(...)`
- 另一侧 builder `readv(...)`
- 验证字节数与内容

3. 混合流程测试
- `readv -> parse -> writev`
- 验证和现有 `.recv/.send/.parse` 能共存

4. 既有回归
- `T19-readv_writev`
- 状态机 builder surface 测试
- 普通 `.recv/.send` 路径回归

## 对上层仓库的影响

这次 kernel 能力补齐后，上层仓库能直接受益：

- `galay-http` 的 header/body 双段发送可以收敛到 builder `writev(...)`
- 当前协议私有的伪 `writev` awaitable 可以删除
- 后续其他 `galay-*` 仓库在协议 framing、批量头部、零拷贝友好发送路径上也能统一到 builder 表达

## 风险与控制

主要风险：

- borrowed `iovec` 生命周期误用
- `StateMachineAwaitable` 内部 active task 状态切换不完整
- `READV/WRITEV` 与 `RECV/SEND` 混用时的 slot 覆盖问题

控制手段：

- 复用现有 `ReadvIOContext` / `WritevIOContext` 的 guard 逻辑
- 为 `StateMachineAwaitable` 增加专门的 surface 与混合流程测试
- 保持 `.recv/.send` 路径不改语义，只做增量扩展

## 结论

本轮最小且正确的方案，是把 `readv/writev` 作为一等 builder 能力补进 `galay-kernel`，并通过 `MachineAction + LinearMachine + StateMachineAwaitable` 一次打通。

这样可以：

- 保持现有单 buffer builder 简洁性
- 给复杂协议库提供统一 scatter-gather 表达能力
- 为 `galay-http` 等上层仓库后续清理冗余 awaitable 提供内核支撑
