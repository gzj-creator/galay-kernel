# Runtime Task Benchmark And API Polish Design

## 背景

`v3` 已经完成 scheduler 内核升级，当前 worktree 又在其上引入了 Tokio 风格的 runtime task API：

- `Task<T>`
- `JoinHandle<T>`
- `Runtime::blockOn(...)`
- `Runtime::spawn(...)`
- `Runtime::spawnBlocking(...)`
- `RuntimeHandle`

但这轮工作还没有完成两件关键事情：

1. 用统一、可复用、可追溯的 benchmark 流程证明 `pre-v3 -> v3 -> refactored` 三组状态的性能变化。
2. 把这次新增的 runtime/task API 做进一步收口，去掉重复入口和内部泄漏的公开接口，让上层 API 更稳定、更易懂。

本设计把“性能验证”和“API 收口”绑定在同一轮里推进，但顺序上明确先建立证据、再依据结果调整实现。

## 三组基线

本轮 benchmark 对比固定为三组：

- `pre-v3`: commit `09c8917`
- `v3`: commit `cde3da1`
- `refactored`: 当前 `.worktrees/v3` 工作树

这样可以把性能变化拆成两段看：

- `pre-v3 -> v3`: scheduler v3 本身带来的变化
- `v3 -> refactored`: runtime/task API 重构与整理带来的变化

最终报告同时展示：

- `pre-v3 -> v3`
- `v3 -> refactored`
- `pre-v3 -> refactored`

## 目标

- 建立一套可复用的三路 benchmark matrix 流程，覆盖全部 benchmark target。
- 统一三组基线的构建方式、运行参数、日志格式和结果汇总方式。
- 核心 runtime / scheduler benchmark 相比 `v3` 必须更优。
- 其余 benchmark 相比 `v3` 不允许出现明显回退。
- 收口本轮新增的 runtime/task API，删除明显重复或不必要暴露的接口。
- 统一文档、示例和测试对高层 runtime task API 的主推荐路径。

## 非目标

- 本轮不删除旧的 `Coroutine` / scheduler 兼容层。
- 本轮不大规模删除已有 `Scheduler` 级公开 API。
- 本轮不引入新的任务组合能力，如 `JoinSet`、取消、`select`、`whenAll`。
- 本轮不重写 benchmark 的业务模型，只统一入口、采样和结果汇总。

## Benchmark 运行设计

### 1. 三组独立 worktree / build

每组都使用独立源码目录与构建目录，避免缓存、日志和中间产物互相污染：

- `.worktrees/pre-v3-bench`
- `.worktrees/v3-baseline`
- `.worktrees/v3`（当前）

每组都重新运行：

- `cmake -S ... -B ...`
- `cmake --build ...`

并统一：

- backend
- build type
- 并行度
- benchmark 端口
- 日志输出目录结构

### 2. Benchmark matrix 统一覆盖全部 14 个 benchmark

沿用并扩展现有 `scripts/run_benchmark_matrix.sh`，目标覆盖：

- standalone:
  - `B1-ComputeScheduler`
  - `B6-Udp`
  - `B7-FileIo`
  - `B8-MpscChannel`
  - `B9-UnsafeChannel`
  - `B10-Ringbuffer`
  - `B13-Sendfile`
  - `B14-SchedulerInjectedWakeup`
- paired:
  - `B2-TcpServer` + `B3-TcpClient`
  - `B4-UdpServer` + `B5-UdpClient`
  - `B11-TcpIovServer` + `B12-TcpIovClient`

三组基线都跑同一套矩阵。

### 3. 采样方式

为了降低抖动影响，每个 benchmark 至少采样 `3` 次，最终用 `median` 做比较。

产物包括：

- 每次采样的原始 stdout / stderr 日志
- 三组 benchmark 的结构化汇总表
- 相对 `v3` 的 delta 百分比

### 4. 历史基线缺失 benchmark 的处理

优先级如下：

1. 直接运行该基线原生 benchmark target。
2. 若 benchmark 是当前分支新增但底层能力在历史基线仍可链接验证，则用兼容 driver 补齐。
3. 若历史基线根本不具备该能力，再标记为 `unsupported`。

像 `B14-SchedulerInjectedWakeup` 这类新增 benchmark，优先采用兼容 driver 链接历史基线库做采样，而不是直接跳过。

## 验收标准

### 1. 核心 benchmark

本轮把以下 benchmark 视为核心 runtime / scheduler 指标：

- `B1-ComputeScheduler`
- `B14-SchedulerInjectedWakeup`
- `B8-MpscChannel`

要求：

- 吞吐类指标相对 `v3` 更高
- 延迟类指标相对 `v3` 更低

### 2. 非核心 benchmark

其余 benchmark 相对 `v3` 不允许出现明显回退。

本轮将“明显回退”定义为：

- 主指标中位数退化超过 `10%`

### 3. 通过判定

本轮要判定为通过，必须同时满足：

- 核心 benchmark 相对 `v3` 更优
- 其余 benchmark 不出现超过 `10%` 的明显回退
- 关键 runtime / scheduler 回归测试通过
- 文档主推荐路径统一到新的 runtime task API
- 本轮新增但没必要暴露的接口已经收口

## 公开 API 收口设计

### 1. 高层公开入口

新的高层公开入口收口到以下集合：

- `Task<T>`
- `Runtime::blockOn(Task<T>)`
- `Runtime::spawn(Task<T>)`
- `Runtime::spawnBlocking(F)`
- `Runtime::handle()`
- `RuntimeHandle::current()`
- `RuntimeHandle::tryCurrent()`
- `RuntimeHandle::spawn(Task<T>)`
- `RuntimeHandle::spawnBlocking(F)`
- `JoinHandle<T>::join()`
- `JoinHandle<T>::wait()`

### 2. 兼容层 / 低层保留项

以下接口本轮保留，但不再作为主推荐路径：

- `Coroutine`
- `Coroutine::wait()`
- 全局 `spawn(Coroutine)`
- `Runtime::start()` / `stop()`
- `Runtime::getNextIOScheduler()` / `getNextComputeScheduler()`
- 直接 `Scheduler::spawn(...)`

### 3. 本轮计划删除或收回的冗余接口

- 删除 `JoinHandle::result()`，统一只保留 `join()`
- 将 `Task<T>` 上主要用于 runtime 内部接线的方法收回内部或 friend 路径：
  - `taskRef()`
  - `belongScheduler()`
  - `belongScheduler(Scheduler*)`
  - `threadId()`
  - `asCoroutine()`
- `BlockingExecutor` 作为 runtime 内部实现细节存在，但不作为文档推荐公开能力

## 去冗余代码方向

- 合并 `Task<T>` / `Task<void>` 中重复的等待与结果提取逻辑
- 合并 `JoinHandle<T>` / `JoinHandle<void>` 中重复的等待与结果提取逻辑
- 收拢 runtime 提交任务、绑定 runtime 上下文和 blocking 执行的重复流程
- 统一示例 / 文档 / 测试中的主路径，只展示 `Runtime::blockOn(...)` / `Runtime::spawn(...)` / `RuntimeHandle`

## 验证范围

除 benchmark 外，本轮还要跑关键 runtime / scheduler 回归：

- `T1-coroutine_chain`
- `T25-spawn_simple`
- `T27-runtime_stress`
- `T42-runtime_strict_scheduler_counts`
- `T52-runtime_block_on_result`
- `T53-runtime_block_on_exception`
- `T54-runtime_spawn_join_handle`
- `T55-runtime_handle_current`
- `T56-runtime_spawn_blocking`
- `T36-task_core_wake_path`
- `T39-awaitable_hot_path_helpers`
- `T43` 到 `T50`

目标是同时证明：

- benchmark 变化真实可追溯
- API 收口没有破坏旧兼容层
- 新的 runtime task API 仍然稳定

## 产物

本轮最终产物包括：

- 一份三组 benchmark 对比结果文档
- 一套可复用的 benchmark matrix / 结果汇总脚本
- 收口后的 runtime/task API
- 更新后的示例、README 和运行时文档

## 结论

本轮推荐按“先证据、后清理”的顺序推进：

1. 先建立 `pre-v3 -> v3 -> refactored` 的统一 benchmark 证据链。
2. 再在证据保护下收口 runtime/task API，删掉重复入口和内部泄漏接口。
3. 若 benchmark 暴露回退，再做针对性优化并重新跑全量三组对比。

这样可以确保：

- 性能结论不是凭感觉
- 接口收口不会掩盖性能问题
- 最终留下的是一套更优雅、证据更完整的 runtime/task API

## Final Validation (2026-03-13)

最终执行结果如下：

- API 收口目标完成：
  - `JoinHandle::result()` 已移除
  - `Task<T>` 的 `taskRef()` / `belongScheduler()` / `threadId()` / `asCoroutine()` 已从 public surface 收回内部 bridge
  - `T57-runtime_task_api_surface` 通过
- 关键 runtime / scheduler 回归测试通过：
  - `T1`
  - `T25`
  - `T27`
  - `T36`
  - `T39`
  - `T42`-`T50`
  - `T52`-`T57`
- benchmark 验收 **未通过**：
  - `B14-SchedulerInjectedWakeup` 相比 `v3` 明显更好
  - `B1-ComputeScheduler` 在最终 run 中没有保持对 `v3` 的优势
  - `B8-MpscChannel` 在最终 run 中明显回退，尤其是 latency
  - `B6-Udp` packet loss 也出现了超过阈值的回退

因此，本轮应结论为：

- `runtime/task API` 收口成功
- 兼容性验证成功
- 性能目标仍需下一轮针对 `B1` / `B8` / `B6` 做设计层优化，而不是把当前结果宣称为已达标
