# Builder Protocol 示例设计

## 背景

`galay-kernel` 已经有了：

- `E10-custom_awaitable`：展示如何通过状态机实现最小自定义 Awaitable
- `AwaitableBuilder<ResultT, InlineN, FlowT>`：展示更高频的线性协议工作流
- `ByteQueueView` + `ParseStatus`：用于半包/粘包协议解析

但 `examples/` 里还缺一个正式、最小、面向用户的链式 Builder 示例，来说明“什么时候应该优先用 Builder，而不是直接手写状态机”。

## 目标

新增一个最小的 Builder 协议示例，让用户直接看到：

1. 如何声明一个 `Flow`
2. 如何实现 `onRecv()` / `onParse()` / `onSend()`
3. 如何把 `ByteQueueView` 和 `ParseStatus::kNeedMore` 结合起来处理半包
4. 如何通过 `.recv(...).parse(...).send(...).build()` 构造协议 awaitable

## 非目标

- 不在这个示例里讲状态机底层实现细节
- 不扩展成复杂多帧协议、TLS handshake 或 shutdown 流程
- 不依赖外部 server、固定 TCP 端口或额外配置

## 方案选择

### 方案 A：最短 `recv -> send` Builder 示例

优点：

- 代码更短
- 更容易一眼看懂

缺点：

- 体现不出 `parse()` 的价值
- 看不出 Builder 为什么比直接 `co_await recv/send` 更值得单独学

### 方案 B：`recv -> parse -> send` 半包示例

优点：

- 能直接体现 `ByteQueueView + ParseStatus::kNeedMore`
- 与 Builder 的典型使用场景一致
- 和 `E10` 状态机示例形成互补

缺点：

- 比方案 A 略长

### 结论

采用方案 B。

## 设计

### 文件落位

- `examples/include/E11-builder_protocol.cc`
- `examples/import/E11-builder_protocol.cc`

对应 target：

- `E11-BuilderProtocol`
- `E11-BuilderProtocolImport`

并在 `docs/04-示例代码.md` 中补充索引。

### 示例行为

示例使用 `socketpair(...)` 构造一对本地 fd，自闭环完成一次最小长度前缀协议交互：

1. 对端线程先发送一个 frame，格式为 `u32_be length + payload`
2. payload 内容为 `"ping"`
3. 发送时故意拆成两段，制造半包
4. Builder 侧用：
   - `.recv(...)`
   - `.parse(...)`
   - `.send(...)`
5. `parse()` 在数据不完整时返回 `ParseStatus::kNeedMore`
6. 收到完整 frame 后切到 `send("pong")`
7. 最终 `onSend()` 用 `ops.complete(...)` 收口

### Flow 形状

Flow 只保留最小状态：

- `ByteQueueView inbox`
- `char scratch[...]`
- `std::array<char, 4> reply`
- `bool parsed_ping`

其中：

- `onRecv()` 只负责把本次字节 append 进 `inbox`
- `onParse()` 只负责检查 frame 是否完整，并在完整后推进到 send
- `onSend()` 只负责在发送成功后 `complete("pong")`

### include / import 差异

- `include` 版使用头文件包含
- `import` 版使用 `import galay.kernel;`
- 行为保持一致，输出允许略有差异

## 验证

至少验证以下命令：

```bash
cmake --build build-awaitable-state-machine --target E11-BuilderProtocol --parallel
./build-awaitable-state-machine/bin/E11-BuilderProtocol
cmake --build build-awaitable-state-machine --target help | rg "E11-BuilderProtocol"
```

通过标准：

- include target 成功构建并运行
- 输出显示示例通过
- 当前环境下若 import target 没有生成，需要明确记录这是模块 toolchain 限制，不是示例实现失败
