# Awaitable 状态机 Builder 设计

## 背景

`galay-kernel` v3.1.0 已经把低层组合式 Awaitable 收口到：

- `SequenceAwaitable<ResultT, InlineN>`
- `SequenceStep<ResultT, InlineN, FlowT, BaseContextT, Handler>`
- `AwaitableBuilder<ResultT, InlineN, FlowT>`
- `ParseStatus`
- `ByteQueueView`

这套公开面解决了两类主要问题：

- 线性 IO pipeline，如 `send -> recv`
- `recv -> parse` 型协议解析，其中 `ParseStatus::kNeedMore` / `kContinue` 能很好处理半包与粘包

但它对“非线性、双向切换”的场景仍然不够贴合。典型例子是 TLS：

- `SSL_read` 可能在一次“读明文”逻辑里返回 `WantWrite`
- 一次“发送明文”逻辑可能先产出多段密文，再等待底层写就绪
- handshake / shutdown 会在 `WantRead` 与 `WantWrite` 之间多轮来回切换

当前这种场景虽然仍能用 `SequenceAwaitable` 或业务侧显式状态机完成，但实现会自然退化成“手工维护一组队列和 cursor”，可读性与可维护性都偏弱。

## 目标

本轮设计目标：

- 保留现有 `AwaitableBuilder` 链式写法，继续服务常见的 `recv.parse.send` 场景
- 新增状态机模式，让复杂 Awaitable 可以直接以“状态推进”的方式表达
- 让链式 builder 与自定义状态机共享同一套底层驱动逻辑
- 最大化复用现有 `SEQUENCE` 调度通道，不重写 `epoll` / `kqueue` / `io_uring` 后端
- 为 `galay-ssl` 这类复杂双向协议提供更自然的建模基础

## 非目标

- 不在本轮引入新的通用 task combinator，如 `map/flatMap/andThen`
- 不在本轮推翻 `SequenceAwaitable + SequenceStep`，它仍然保留为显式低层组合手段
- 不在本轮为全部 IO 类型一次性提供状态机动作，第一版只覆盖读写主路径
- 不在本轮重写调度器或 reactor 后端
- 不在本轮直接迁移所有业务仓库的自定义 Awaitable

## 方案比较

### 方案 A：继续扩展队列式 `SequenceAwaitable`

继续以 `SequenceAwaitable` 为核心，增加更多 queue / re-arm / local-step 能力，让业务层自己拼出状态机。

优点：

- 改动最小
- 与现有 builder / step 模型完全一致

缺点：

- 队列模型更适合“步骤序列”，不适合“当前状态决定下一步”的逻辑
- TLS 这类 `Read -> WantWrite -> Write -> Read` 场景仍然会显得别扭
- 复杂 awaitable 的局部状态与事件注册意图分离，读代码时心智负担大

### 方案 B：底层引入状态机驱动器，Builder 变成双模式 facade

新增通用状态机 Awaitable 驱动器；`AwaitableBuilder` 保留链式写法，但线性 DSL 不再直接拥有独立执行内核，而是编译成内置 `LinearMachine`。复杂场景则通过 `fromStateMachine(...)` 直接接入状态机。

优点：

- 简单场景保持易用
- 复杂场景有更自然的建模方式
- 底层执行内核统一，后续维护成本更低
- 最符合 Tokio 式“底层状态机 + 上层语法糖/codec”的分层思路

缺点：

- `Awaitable.h` 抽象层需要做一轮泛化
- 第一版需要仔细处理 `Continue` 空转保护、错误传播与回归测试

### 方案 C：直接用状态机模型取代链式 Builder

移除当前 builder DSL，统一要求业务代码显式实现状态机。

优点：

- 内核模型最统一

缺点：

- 普通 `recv.parse.send` 场景会变重
- 现有公开面退化，用户体验明显变差
- 不符合“高频简单场景依然便捷”的目标

结论：采用方案 B。

## 最终方案

### 1. 底层新增状态机动作模型

新增一个极小的动作集合：

- `Continue`
- `WaitForRead`
- `WaitForWrite`
- `Complete`
- `Fail`

建议形态：

```cpp
enum class MachineSignal {
    kContinue,
    kWaitRead,
    kWaitWrite,
    kComplete,
    kFail,
};

template <typename ResultT>
struct MachineAction {
    MachineSignal signal = MachineSignal::kContinue;

    char* read_buffer = nullptr;
    size_t read_length = 0;

    const char* write_buffer = nullptr;
    size_t write_length = 0;

    std::optional<ResultT> result;
    std::optional<IOError> error;

    static MachineAction continue_();
    static MachineAction waitRead(char* buffer, size_t length);
    static MachineAction waitWrite(const char* buffer, size_t length);
    static MachineAction complete(ResultT result);
    static MachineAction fail(IOError error);
};
```

这里 `Fail(IOError)` 只用于内核级失败或状态机契约错误。

业务层如果希望返回自定义错误，应当通过：

- `Complete(std::unexpected(custom_error))`

把领域错误放进自己的 `result_type`。

### 2. 新增状态机概念与 Awaitable 载体

新增一个状态机约束：

```cpp
template <typename MachineT>
concept AwaitableStateMachine =
    requires(MachineT& machine, std::expected<size_t, IOError> io_result) {
        typename MachineT::result_type;
        { machine.advance() } -> std::same_as<MachineAction<typename MachineT::result_type>>;
        { machine.onRead(std::move(io_result)) } -> std::same_as<void>;
        { machine.onWrite(std::move(io_result)) } -> std::same_as<void>;
    };
```

再新增：

- `StateMachineAwaitable<MachineT>`

它负责：

- 持有 `MachineT`
- 持有内部 `RecvIOContext` / `SendIOContext`
- 在 `await_suspend()` 里驱动 `machine.advance()`
- 在底层读写完成后把结果喂回 `machine.onRead()` / `machine.onWrite()`

### 3. 复用现有 `SEQUENCE` 调度通道

第一版不新增新的 `IOEventType`。

`StateMachineAwaitable` 直接复用当前的 `SEQUENCE` 管道：

- 注册时仍走 `IOScheduler::addSequence(...)`
- reactor 仍然通过 `prepareForSubmit()` 选择当前活跃动作
- 完成后仍然调用 `onActiveEvent(...)`

也就是说，当前后端改动目标不是“新增状态机后端”，而是让 `SEQUENCE` 可以承载两种执行模型：

- 队列式 sequence
- 单活跃状态机

为了减少后端改动，第一版最现实的方式是：

- 让 `StateMachineAwaitable` 继承现有 `SequenceAwaitableBase`
- 维持 `front()` / `prepareForSubmit()` / `onActiveEvent()` 这组接口

后续如果状态机模型稳定，再考虑把 `SequenceAwaitableBase` 更名为更泛化的 `CompositeAwaitableBase`。

本轮不做该命名清理。

### 4. `AwaitableBuilder` 升级为双模式 facade

`AwaitableBuilder` 保持链式模式不变：

```cpp
auto aw = AwaitableBuilder<Result, 4, Flow>(&controller, flow)
    .recv<&Flow::onRecv>(buf, len)
    .parse<&Flow::onParse>()
    .send<&Flow::onSend>(out, out_len)
    .build();
```

新增状态机模式：

```cpp
auto aw = AwaitableBuilder<Result>::fromStateMachine(
    &controller,
    MyMachine{...}
).build();
```

关键点：

- 链式 builder 不再拥有独立的底层执行内核
- 它会编译成一个内置 `LinearMachine`
- 最终 `build()` 返回 `StateMachineAwaitable<LinearMachine<...>>`

这样：

- 普通场景继续用熟悉 DSL
- 高级场景直接写状态机
- 两者共享同一个驱动器

### 5. 内置 `LinearMachine` 承接现有 builder 语义

新增内部实现：

- `detail::LinearMachine<ResultT, InlineN, FlowT>`

它把现在的 builder 节点翻译成状态机节点：

- `recv` 节点 -> `WaitForRead`
- `send` 节点 -> `WaitForWrite`
- `parse` 节点 -> 本地推进，返回 `Continue / WaitForRead / Complete`

`ParseStatus` 语义保持不变：

- `kNeedMore`：回到最近一个 `recv` 节点，生成新的 `WaitForRead`
- `kContinue`：继续本地推进，不等待新的内核事件
- `kCompleted`：进入下一个节点；如果没有下一个节点，则等待 `ops.complete(...)` 已经设置结果

这保证：

- `T63-custom_sequence_awaitable`
- `T76-sequence_parser_need_more`
- `T77-sequence_parser_coalesced_frames`

这些现有回归目标在语义上都不需要改变。

### 6. 自定义状态机示例：SSL

像 `SslRecvAwaitable` 这类复杂 Awaitable，更适合直接写成：

```cpp
struct SslRecvMachine {
    using result_type = std::expected<Bytes, SslError>;

    MachineAction<result_type> advance();
    void onRead(std::expected<size_t, IOError> result);
    void onWrite(std::expected<size_t, IOError> result);

private:
    enum class State {
        kDrainPlaintext,
        kNeedCipherRead,
        kNeedCipherWrite,
        kCompleted,
        kFailed,
    };
};
```

这种写法能自然表达：

- 先尝试从 OpenSSL 缓冲区继续读明文
- 需要网络读时返回 `WaitForRead`
- `SSL_read` 返回 `WantWrite` 时切到 `WaitForWrite`
- 写完后再回到读或继续 drain

相比显式维护 `m_tasks + m_cursor + addTask(...)`，状态机会更直观。

## 错误处理与安全约束

### `Continue` 空转保护

状态机模型最大的风险是错误实现导致的本地死循环。

因此 `StateMachineAwaitable::pump()` 必须设置单次内联推进上限，例如：

- `kMaxInlineTransitions = 64`

如果单次 `await_suspend()` 或单次 IO 完成后连续推进超过上限，内核应：

- 以 `IOError(kParamInvalid, 0)` 或新增更明确的错误码结束 awaitable

建议第一版直接新增更明确的 `IOErrorCode`，避免把状态机错误混入普通参数错误。

### 错误边界

- 底层 syscall 失败：通过 `onRead()` / `onWrite()` 以 `std::expected<size_t, IOError>` 喂给状态机
- 状态机自身决定是否把它转成领域错误
- 状态机契约错误或内核保护触发：通过 `MachineAction::fail(IOError)` 结束

### 单活跃 IO 约束

第一版明确约束：

- 每个状态机同一时刻只允许一个活跃 IO 动作
- 不支持“同时挂读写”或多 SQE 并发推进

这个约束刻意保持简单，也与 TLS / framing 这类典型场景匹配。

## 对 `galay-kernel` 的改动范围判断

### 需要改动

- `galay-kernel/kernel/Awaitable.h`
- `galay-kernel/kernel/Awaitable.cc`
- 与 `AwaitableBuilder` 直接相关的测试

### 大概率不需要重写

- `IOScheduler` 接口
- `IOController` 槽位模型
- `EpollReactor` / `KqueueReactor` / `IOUringReactor` 的 `SEQUENCE` 主流程

原因是这三类后端当前已经支持：

- 在 `addSequence(...)` 时，根据当前活跃任务决定挂读/挂写
- 在事件完成后调用 `onActiveEvent(...)`
- 根据返回值决定继续 re-arm 还是唤醒协程

这恰好就是状态机 awaitable 需要的最小底座。

## 测试策略

新增测试建议：

- `T85-state_machine_awaitable_surface.cc`
  - 验证 `MachineAction`、`AwaitableStateMachine`、`StateMachineAwaitable`、`AwaitableBuilder::fromStateMachine(...)` 公开表面存在
- `T86-state_machine_read_write_loop.cc`
  - 用 socketpair 验证 `WaitForRead -> WaitForWrite -> WaitForRead -> Complete` 循环可用
- `T87-awaitable_builder_linear_machine_surface.cc`
  - 验证链式 builder 仍可构建，并桥接到底层状态机实现

必须回归的现有测试：

- `T30-custom_awaitable.cc`
- `T40-io_uring_custom_awaitable_no_null_probe.cc`
- `T63-custom_sequence_awaitable.cc`
- `T76-sequence_parser_need_more.cc`
- `T77-sequence_parser_coalesced_frames.cc`

第二阶段如果迁移 `galay-ssl`，再单独补真实 TLS 场景回归。

## 迁移顺序

建议分四步落地：

1. 新增状态机核心类型与 `StateMachineAwaitable`，暂不改现有 builder
2. 新增 `AwaitableBuilder::fromStateMachine(...)`，让复杂场景先能接入
3. 把链式 builder 编译为 `LinearMachine`
4. 在下游仓库选一个真实复杂 awaitable 试迁，优先 `SslRecvAwaitable`

这个顺序能保证：

- 新能力先以增量方式进入
- 现有 builder 回归不被第一步打爆
- 真正的业务复杂度用真实案例检验，而不是只停留在抽象层

## 结论

本轮应当把“状态机”视为 `Awaitable` 组合能力的底层统一模型，把链式 `AwaitableBuilder` 视为其面向高频场景的 facade。

也就是说：

- 底层统一成 `StateMachineAwaitable`
- 上层保留链式 builder
- 复杂场景开放 `fromStateMachine(...)`
- 当前 `SEQUENCE` 通道继续作为调度底座复用

这条路径比继续堆 queue API 更适合 TLS、握手、shutdown 等双向状态切换场景，也不会牺牲已有 `recv.parse.send` 的使用体验。
