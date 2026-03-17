# 自定义 Awaitable 示例设计

## 背景

`galay-kernel` 现在已经提供了基于状态机的自定义 Awaitable 模型：

- `MachineAction<ResultT>`
- `AwaitableBuilder<ResultT>::fromStateMachine(...)`
- `StateMachineAwaitable<MachineT>`

但 `examples/` 里还没有一个最小、正式、可直接运行的示例，告诉用户“怎么写一个自定义 Awaitable”。

现有测试已经覆盖了这套机制的行为正确性，但测试文件不适合作为面向用户的第一入口。

## 目标

新增一个最小的 `examples` 示例，让用户可以直接看到：

1. 如何声明一个状态机类型
2. 如何实现 `advance()` / `onRead()` / `onWrite()`
3. 如何通过 `AwaitableBuilder<Result>::fromStateMachine(...).build()` 构造 awaitable
4. 如何把这个 awaitable 放进真实 `Task<void>` 流程里运行

## 非目标

- 不在这个示例里同时演示链式 Builder
- 不把示例扩展成复杂协议解析、TLS handshake 或多阶段 shutdown
- 不额外引入外部网络依赖或固定 TCP 端口

## 方案选择

### 方案 A：链式 Builder 示例

优点：

- 代码更短
- 更接近常见 `recv/parse/send` 使用方式

缺点：

- 不能直接回答“如何实现自定义 Awaitable”
- 更像 DSL 使用示例，而不是底层扩展示例

### 方案 B：状态机自定义 Awaitable 示例

优点：

- 直接展示最核心的自定义扩展入口
- 与后续 `galay-ssl` 迁移目标一致
- 用户可以直接从示例复制状态机骨架

缺点：

- 比链式 Builder 示例稍长一点

### 结论

采用方案 B。

## 设计

### 文件落位

- `examples/include/E10-custom_awaitable.cc`
- `examples/import/E10-custom_awaitable.cc`

对应 target：

- `E10-CustomAwaitable`
- `E10-CustomAwaitableImport`

并在 `docs/04-示例代码.md` 中补充索引。

### 示例行为

示例使用 `socketpair(...)` 构造一对本地 fd，自闭环完成一次最小交互：

1. 业务侧 coroutine 创建 `IOController`
2. 定义 `PingPongMachine`
3. `advance()` 首先返回 `MachineAction<Result>::waitRead(...)`
4. 读到 `"ping"` 后切换到 `waitWrite(...)`
5. 写出 `"pong"` 后 `complete(...)`
6. 对端线程负责先写 `"ping"`，再验证收到 `"pong"`

这样用户可以看到一个完整但极小的自定义 Awaitable 生命周期，不需要外部 server，也不需要端口管理。

### 状态机形状

状态机保持最小三态：

- `ReadPing`
- `WritePong`
- `Done`

结果类型使用 `std::expected<std::string, IOError>`，最终成功返回 `"pong"`。

### include / import 差异

- `include` 版沿用现有 examples 风格，使用头文件包含
- `import` 版使用 `import galay.kernel;`
- 行为保持一致，输出风格可略有差别，但不改变示例骨架

## 验证

至少验证以下命令：

```bash
cmake --build build-awaitable-state-machine --target \
  E10-CustomAwaitable E10-CustomAwaitableImport --parallel

./build-awaitable-state-machine/bin/E10-CustomAwaitable
./build-awaitable-state-machine/bin/E10-CustomAwaitableImport
```

通过标准：

- 两个示例都能成功构建
- 两个示例都能成功运行并输出通过结果
- `docs/04-示例代码.md` 中列出的文件路径和 target 与真实工程一致
