# Awaitable Builder / Sequence 重构设计

日期：2026-03-15

## 目标

本轮重构目标是以非兼容方式移除 `CustomAwaitable` / `CustomSequenceAwaitable` / `IOScheduler::addCustom` 这一整套旧扩展模型，改为：

- 对外提供统一、强类型、组合式的 `SequenceAwaitable<ResultT, InlineN>`
- 以 `AwaitableBuilder<ResultT, InlineN, FlowT>` 作为主要公开创建入口
- 统一三端后端对“组合 Awaitable”的调度路径，不再让 backend 感知 `custom awaitable`
- 让大多数业务库只实现 `Flow + parser/handler`，不再继承 Awaitable
- 让协议解析成为一等公民，并在“未形成逻辑结果”时避免无效唤醒

## 非目标

- 不保留旧 `CustomAwaitable` 兼容层
- 不以 `std::function`、动态分配或解释式 DSL 换取更短语法
- 不让 backend 直接理解协议解析语义
- 不在第一版引入复杂的 ring buffer；优先用线性滑窗保持实现可控

## 当前问题

当前组合式 Awaitable 的问题分为两层：

1. 用户侧样板重
- 需要继承 `CustomAwaitable` / `CustomSequenceAwaitable`
- 需要手写 `await_resume()`、`onCompleted()`
- 复杂步骤还要手写 `RecvCtx/SendCtx + owner + handleComplete`

2. 内核分层不合理
- `IOScheduler::addCustom` 是一条特殊调度入口
- `kqueue / epoll / io_uring` 都要感知 `custom awaitable`
- 组合 Awaitable 热路径依赖 `std::vector<IOTask>` 与 `context->handleComplete(...)`

这会同时损害：

- 易用性：业务库必须理解 Awaitable 协议
- 一致性：backend 维护一条特殊 custom 路径
- 性能：小序列场景额外承担队列分配与不必要的唤醒

## 架构总览

### Layer 1：统一 operation 提交层

移除 `IOScheduler::addCustom`，统一引入：

```cpp
struct OperationRef;
int IOScheduler::submitOperation(IOController*, const OperationRef&);
```

backend 只关心“当前 controller 上有一个什么 operation 要提交/重提/完成”，不再关心这个 operation 来自普通 Awaitable 还是组合式 Awaitable。

### Layer 2：组合 Awaitable 内核

核心类型：

```cpp
template <typename ResultT, size_t InlineN = 4>
class SequenceAwaitable;

template <typename ResultT>
class SequenceOps;

template <typename FlowT, typename BaseContextT, auto Handler>
class SequenceStep;
```

职责：

- `SequenceAwaitable`
  - 持有当前激活 operation、完成结果、错误状态、waker
  - 持有 inline step queue，不使用 `std::vector<IOTask>`
  - 控制“local step 循环 / primitive step 提交 / 最终唤醒”

- `SequenceOps`
  - 提供 `queue()` / `queueMany()` / `complete()` / `fail()` / `clear()`
  - 是 handler 唯一的调度操作接口

- `SequenceStep`
  - 将 `BaseContextT` 与 `Flow` 的成员函数绑定
  - 替代手写 `RecvCtx/SendCtx` 子类

### Layer 3：Builder 易用层

公开主入口：

```cpp
template <typename ResultT, size_t InlineN, typename FlowT>
class AwaitableBuilder;
```

Builder 负责：

- 以强类型方式描述标准 I/O 步骤
- 描述本地解析步骤
- 绑定 `Flow`
- 约束结果完成方式
- 产出最终 `SequenceAwaitable`

普通业务库优先使用 `AwaitableBuilder`，而不是直接拼 `SequenceAwaitable`。

## Operation 模型

### OperationRef

```cpp
enum class OperationKind : uint8_t {
    Primitive,
    Local,
};

struct OperationRef {
    OperationKind kind;
    IOEventType type;
    IOContextBase* context;
    CompletionSink* sink;
};
```

语义：

- `Primitive`
  - 标准 I/O 操作，例如 `RECV/SEND/CONNECT/ACCEPT/READV/WRITEV/...`
  - backend 继续走高性能 `switch(type)` 快路径

- `Local`
  - 本地步骤，不进入 backend，不触发 poll/register
  - 用于解析、状态更新、分支、错误映射

### CompletionSink

`SequenceAwaitable` 实现 `CompletionSink` 接口，统一接收 operation 完成通知。

```cpp
struct CompletionSink {
    virtual void onOperationReady(IOController*, IOContextBase*) = 0;
    virtual void onOperationError(IOController*, IOError) = 0;
};
```

这使 backend 能对普通 Awaitable 与组合 Awaitable 共用提交流程，而不需要保留 `addCustom` 入口。

## SequenceAwaitable 语义

### await_suspend

`SequenceAwaitable::await_suspend()` 不直接向 backend 暴露 custom queue，而是执行内部驱动循环：

1. 取当前 step
2. 若为 `Local`，立即执行 handler
3. 若 handler 又 `queue()` 了新 step，则继续循环
4. 若得到 `Primitive`，调用 `submitOperation()`
5. 若已 `complete/fail`，直接返回 `false` 或设置 wake

### operation 完成后的驱动

I/O 完成后不直接恢复 coroutine，而是：

1. 将结果写入 flow/context
2. 运行对应 handler
3. 继续执行本地 local steps
4. 如仍需更多 I/O，直接重提下一步，且不唤醒 coroutine
5. 只有 `complete/fail/cancel/timeout` 时才最终唤醒

统一原则：

> `co_await` 等的是逻辑结果完成，而不是某一次 I/O 完成。

## 协议解析模型

### 解析是 local step，不进入 backend

协议解析必须与原始 I/O 分层：

- 传输层只负责将字节 append 到缓冲区
- 解析层只消费已收到的字节
- backend 不理解“头不全 / body 不全 / 粘包”

### ParseResult

所有增量解析器统一返回：

```cpp
enum class ParseStatus {
    NeedMore,
    Produced,
    Completed,
    Failed,
};

template <typename ValueT, typename ErrorT>
struct ParseResult {
    ParseStatus status;
    size_t consumed = 0;
    size_t needAtLeast = 0;
    std::optional<ValueT> value;
    std::optional<ErrorT> error;
};
```

这能覆盖：

- 半个协议头
- 头完整但 body 不全
- 一次 `recv` 带来多帧粘包
- 完成一帧但缓冲区中还残留下一帧前缀

### ByteQueueView

第一版缓冲视图采用线性滑窗：

```cpp
struct ByteQueueView {
    char* data;
    size_t begin;
    size_t end;
    size_t capacity;
};
```

提供：

- `readableBytes()`
- `writableBytes()`
- `readSpan()`
- `writeSpan()`
- `commitWrite(n)`
- `consume(n)`
- `compactIfNeeded()`

原则：

- 半包时保留未消费区间
- 粘包时 parse step 在本地循环尽量吃完
- 仅当尾部空间不足时做 compact
- steady-state 下尽量零额外分配、少拷贝

## Builder API 草案

目标是让大多数协议/业务库直接通过 Builder 组合标准 I/O 与本地解析步骤。

### 标准步骤

```cpp
auto aw = AwaitableBuilder<ResultT>(&controller, flow)
    .recvAppend(flow.buffer, &Flow::onBytes)
    .parse(&Flow::parseHeader)
    .parse(&Flow::parseBody)
    .send(flow.outBuffer, &Flow::onSend)
    .finish(&Flow::finish)
    .build();
```

支持的第一批标准接口：

- `.recv(...)`
- `.recvAppend(...)`
- `.send(...)`
- `.connect(...)`
- `.accept(...)`
- `.readv(...)`
- `.writev(...)`
- `.parse(...)`
- `.local(...)`
- `.finish(...)`

### Flow 责任

Flow 是普通状态对象，不是 Awaitable 基类，负责：

- 缓冲区与协议状态
- 头/body 长度与解析进度
- 错误映射
- 最终结果构造

## 性能要求

### 必须保持或优于现状的点

- backend primitive 提交仍走枚举 `switch(type)` 快路径
- builder 和 sequence 不引入 `std::function`
- 常见 2~4 步序列不发生堆分配
- 协议未完成时避免无意义 `wakeUp()`
- 粘包在单次恢复中尽量多消费，减少重新 poll/register

### 明确避免的做法

- 不把所有步骤都抽象为通用 thunk，避免高频 I/O 退化为纯函数指针分发
- 不把解析实现成伪 I/O 步骤
- 不让 builder 隐式拥有大缓冲区并频繁扩容

## 非兼容迁移策略

本次版本直接做非兼容迁移：

1. 删除或内部化以下旧接口
- `CustomAwaitable`
- `CustomSequenceAwaitable`
- `CustomStepContext`
- `IOScheduler::addCustom`
- backend 中与 `CUSTOM` 特判相关的路径

2. 更新所有测试、example、benchmark 到新模型

3. 更新正式文档与升级说明

4. 版本号提升到 `v3.1.0`

## 验证要求

必须完成以下验证：

- 所有 `test/` 通过
- 所有 `examples/` 可构建并运行
- 所有 `benchmark/` 能跑完
- 自实现复杂组合 Awaitable 能正常运行
- 旧接口源码与旧示例全部删除
- 正式文档、README、升级说明全部同步

新增重点测试：

- builder 线性 `send -> recv`
- builder `recv -> parse(NeedMore) -> recv -> parse(Completed)` 不提前唤醒
- 粘包场景单次恢复多帧消费
- `ssl/http` 这种复杂 flow 在新模型下的 smoke/regression
- 三端后端下 sequence 路径一致工作

## 风险与缓解

### 风险 1：后端统一入口引发回归

缓解：

- 先引入 `submitOperation` 并让普通 Awaitable 也共用内部 helper
- 再删除 `addCustom`

### 风险 2：sequence 驱动循环导致重复提交/丢 wake

缓解：

- 新增针对 `NeedMore` / `Completed` / `Failed` 的状态机测试
- 三端分别验证

### 风险 3：协议缓冲区 compact 语义出错

缓解：

- 为 `ByteQueueView` 补单测
- 为头不全/body 不全/粘包补回归

## 最终交付

本轮完成后需要同时交付：

- 新 `SequenceAwaitable` / `AwaitableBuilder` 内核
- 旧 custom 路径完全删除
- test/example/benchmark 全量迁移
- 文档更新
- 中文提交
- `v3.1.0` tag
- 对应 GitHub release
