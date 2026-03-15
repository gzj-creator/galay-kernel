# Scheduler V3 设计

## 背景

当前 `Scheduler` 内核已经采用了较成熟的 owner-thread reactor 思路：

- `IOScheduler` 线程内优先消费本地 ready 任务
- 跨线程提交通过 injected queue + `notify()` 唤醒 loop
- 后端分别落在 `epoll` / `kqueue` / `io_uring`
- ready task 已收敛为 `TaskRef`，热路径较短

这条路线本身是可行的，和主流开源框架并不冲突；它更接近 “轻量 owner-thread reactor runtime”，而不是共享型 `io_context`。

`v3` 不重写模型，而是在当前路线下补齐三类关键能力：

1. 调度语义闭环
2. 唤醒合并
3. 公平性预算

## 目标

- 保留当前 owner-thread reactor 架构
- 强化 “任务恢复必须回到所属 `Scheduler`” 的语义
- 降低跨线程 injected path 的唤醒 syscall 开销
- 改善 ready task / injected task / I/O completion 之间的公平性
- 为后续 `Task<T>`、`thisScheduler()` 等上层 API 提供更稳的调度内核

## 非目标

- 不在 `v3` 引入 work stealing
- 不在 `v3` 引入优先级体系 / service group / 复杂配额系统
- 不把调度模型改成 Asio 风格的共享 `io_context`
- 不在 `v3` 重写 `ComputeScheduler` 为多 lane compute runtime

## 现状判断

### 优势

- 本地 fast path 清晰：`lifo_slot + local_queue`
- 跨线程注入路径简单：`inject_queue + notify`
- `TaskRef` 已经替代重量级对象穿过 hot path
- 三个 IO 后端都具备一致的总体结构

### 当前短板

1. continuation 恢复仍存在绕过 `Scheduler` 的路径
2. remote enqueue 基本伴随一次 `notify()`，高负载下容易产生唤醒风暴
3. 公平性主要靠固定经验参数，缺少预算控制
4. `ComputeScheduler` 仍是简单单线程 compute lane，定位需明确

## 方案选择

### 方案 A：保留当前 owner-thread reactor，补齐运行时纪律

这是 `v3` 采用的方案。

优点：

- 与当前代码结构兼容
- 改动范围可控
- 能直接提升正确性和高负载稳定性

缺点：

- 不会获得共享 loop / stealing 这类更重的能力

### 方案 B：改成共享型 `io_context`

不采用。

原因：

- 与当前 owner-thread、本地热路径思路冲突
- 需要重做大量调度假设

### 方案 C：向 Seastar 风格 per-core runtime 演进

不采用。

原因：

- 工程量过大
- `v3` 风险高，不适合作为当前迭代目标

## 设计

### P1：调度语义闭环

目标：任何协程恢复都必须通过所属 `Scheduler` 路径完成。

当前问题：

- `wait()` 完成链路里，child 完成后仍可能直接 `handle.resume()`
- 这会削弱 owner-thread / executor 归属语义

设计：

- 将 `TaskState` 中的 continuation 从 `Coroutine` 语义收敛到 `TaskRef`
- child 完成时：
  - 标记 `done`
  - 取出 continuation
  - 通过 continuation 所属的 `Scheduler::schedule(...)` 或 `scheduleDeferred(...)` 恢复
- 不再直接调用 waiter 的底层 `handle.resume()`

阶段性约束：

- `v3` 先保持单 waiter 语义
- 多 waiter 队列不在当前迭代实现，但需要在文档和断言中明确边界

预期收益：

- 调度语义更一致
- 跨 scheduler wait 行为更可推理
- 为后续 `Task<T>` continuation 模型打基础

### P2：`notify()` 合并

目标：remote enqueue 不再默认一次提交对应一次 wake syscall。

当前问题：

- injected path 现在通常会 `enqueue + notify`
- 在多线程对同一 scheduler 注入任务时，容易出现大量 `eventfd/pipe` 写入

设计：

- 引入 scheduler 级别的唤醒状态：
  - `sleeping`
  - `wakeup_pending`
- remote enqueue 时：
  - 先注入任务
  - 仅当 loop 处于可睡眠状态且还没有 pending wakeup 时，才真正执行 `notify()`
- loop 线程在进入阻塞等待前后负责维护 `sleeping / wakeup_pending` 状态

预期收益：

- 降低 injected-heavy 场景下的 syscall 数量
- 降低跨线程唤醒抖动
- 改善高负载下的吞吐与尾延迟

### P3：公平性预算

目标：ready task 不再无限榨干 loop，避免长期压制 I/O poll 和 injected task。

当前问题：

- 现有公平性依赖：
  - `lifo_poll_limit`
  - `inject_check_interval`
- 这已经有效，但仍属于经验参数驱动

设计：

- 为 `processPendingCoroutines()` 增加预算控制
- `v3` 先采用任务数预算：
  - 每轮最多处理 `ready_budget` 个 ready task
- 保留：
  - `lifo_slot`
  - `lifo_poll_limit`
  - `inject_check_interval`
- 但将它们降级为局部优化参数，主公平性约束由 `ready_budget` 承担

预期收益：

- 降低 ready self-reschedule 对 I/O completion 的压制
- injected task 延迟更稳定
- 更容易控制 tail latency

### ComputeScheduler 定位

`v3` 不重做 `ComputeScheduler` 架构。

当前定位统一为：

- 简单单线程 compute lane
- 适合隔离纯计算协程
- 不承担 stealing / 全局均衡职责

后续如果需要，单独开新迭代讨论多 lane compute runtime。

## 数据流变化

### Ready / resume 路径

`v3` 前：

- 某些路径通过 `Scheduler`
- 某些 continuation 直接恢复底层句柄

`v3` 后：

- 所有恢复统一走 `TaskRef -> owner Scheduler -> schedule(...) -> resume(...)`

### Remote inject 路径

`v3` 前：

- `inject_queue.enqueue(task)`
- `notify()`

`v3` 后：

- `inject_queue.enqueue(task)`
- 若需要唤醒且当前无 pending wakeup，则 `notify()`

### Event loop 路径

`v3` 前：

- `processPendingCoroutines()`
- `tick()`
- `poll`

`v3` 后：

- 按 `ready_budget` 执行 ready tasks
- `tick()`
- `poll`
- 唤醒状态机维护 `sleeping / wakeup_pending`

## 错误处理与边界

- `notify()` 失败仍通过现有 `lastError` / `m_last_error_code` 路径暴露
- `wait()` 若继续保持单 waiter：
  - 文档明确为单 waiter 语义
  - debug 构建可加断言
- `stop()` 仍必须强制唤醒 loop，不能被 wakeup coalescing 吞掉

## 验证与验收

`v3` 不能只看结构改动，必须对比原分支基线。

### 基线

- 分支：`main`
- 分叉点：`cde3da1`
- 当前首轮环境：
  - macOS
  - kqueue
  - AppleClang 17
  - Release

### 对比矩阵

#### 正确性

- `T36-task_core_wake_path`
- `T39-awaitable_hot_path_helpers`
- `T41-task_ref_schedule_path`
- 视改动范围补跑：
  - `T1-coroutine_chain`
  - `T25-spawn_simple`
  - `T27-runtime_stress`

#### Scheduler / injected path

- `B1-ComputeScheduler`
- 新增或补充一个 injected/wakeup benchmark，用于验证：
  - wakeup coalescing
  - fairness budget

#### IO

- `B2-TcpServer` + `B3-TcpClient`
- `B6-Udp`
- `B11-TcpIovServer` + `B12-TcpIovClient`
- `B13-Sendfile`

#### 并发通路

- `B8-MpscChannel`

### 采样规则

- 每项至少跑 5 轮
- 记录原始输出
- 汇总 `baseline` / `v3` / `delta%`
- 吞吐取 median
- 延迟类尽量补平均值与 `p95`

### 预期

- P1：正确性更强，微基准可能存在很小的回退
- P2：cross-thread / injected-heavy workload 明显改善
- P3：峰值吞吐不一定绝对提升，但 I/O 响应与 tail latency 更稳

## 里程碑

1. P1：统一 continuation 恢复路径
2. P2：加入 wakeup coalescing
3. P3：加入 ready budget
4. 跑 baseline vs `v3` 对比压测
5. 汇总结论并判断是否继续扩展 `ComputeScheduler`

## 结论

`v3` 延续当前 owner-thread reactor 路线，不重写调度模型，而是补齐成熟 runtime 常见的运行时纪律：

- 恢复路径统一
- 唤醒请求合并
- 调度预算控制

这样可以在保持当前轻量热路径的前提下，显著提升调度语义一致性与高负载稳定性。
