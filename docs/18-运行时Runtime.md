# 18-运行时Runtime

本页现在只保留 `Runtime` 专题的定位信息；完整事实请优先回到主干页。

## 本页回答什么

- `Runtime` / `RuntimeBuilder` 负责什么
- 调度器数量、轮询分发、全局定时器这些问题先看哪里
- 哪些测试 / 示例最适合作为运行时语义锚点

## 当前稳定事实

- `Runtime` 负责统一管理多个 IO / 计算调度器
- `RuntimeBuilder` 负责调度器数量与高级绑核策略等配置
- `start()` / `stop()`、调度器轮询、全局 `TimerScheduler` 的完整说明已折回主干页

## 先看主干页

- 架构与调度模型：`docs/01-架构设计.md`
- 公开 API：`docs/02-API参考.md`
- 最小工作流：`docs/03-使用指南.md`
- 绑核 / 平台高级约束：`docs/06-高级主题.md`

## 源码 / 验证锚点

- 源码：`galay-kernel/kernel/Runtime.h`、`galay-kernel/kernel/Runtime.cc`
- 关联类型：`galay-kernel/kernel/ComputeScheduler.h`、`galay-kernel/kernel/IOScheduler.hpp`
- 测试：`test/T11-compute_scheduler.cc`、`test/T12-mixed_scheduler.cc`、`test/T27-runtime_stress.cc`、`test/T42-runtime_strict_scheduler_counts.cc`
- 示例：`examples/include/E2-tcp_echo_server.cc`、`examples/include/E3-tcp_client.cc`、`examples/include/E4-coroutine_basic.cc`

## RAG 关键词

- `Runtime`
- `RuntimeBuilder`
- `start`
- `stop`
- `getNextIOScheduler`
