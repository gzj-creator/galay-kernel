# Scheduler V3 Enterprise Refactor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 将 `Scheduler` 内核重构为企业级分层架构，统一 `kqueue` / `epoll` / `io_uring` 的调度纪律，修复 `B8` 相关 tail latency / batch path 问题，并完成本地与 Ubuntu 上的全量测试和全 benchmark baseline vs optimized 对比。

**Architecture:** 先抽离统一的 `SchedulerCore` / `WakeCoordinator` / `BackendReactor` 边界，再迁移 `kqueue`、`epoll`、`io_uring`，最后重构 `MpscChannel` 的等待注册与唤醒语义。验证阶段分本地 `kqueue` 和 Ubuntu 上 `epoll` / `io_uring` 两组，全量执行 tests 与 benchmarks，并生成统一结果文档。

**Tech Stack:** C++23、C++20 协程、`kqueue`、`epoll`、`io_uring`、`moodycamel::ConcurrentQueue`、CMake、SSH、Ubuntu 远端构建环境。

---

### Task 1: 为分层重构建立回归保护网

**Files:**
- Modify: `test/T43-wait_continuation_scheduler_path.cc`
- Modify: `test/T44-scheduler_wakeup_coalescing.cc`
- Modify: `test/T45-scheduler_ready_budget.cc`
- Modify: `test/T46-scheduler_injected_burst_fastpath.cc`
- Modify: `test/T47-scheduler_queue_edge_wakeup.cc`
- Create: `test/T48-scheduler_core_loop_stages.cc`
- Create: `test/T49-channel_wait_registration.cc`

**Step 1: 写 `SchedulerCore` 固定 loop 阶段的 failing test**

- 验证本轮执行顺序稳定为：collect remote -> collect completions -> run ready -> poll
- 对三后端至少跑通一个最小桩场景

**Step 2: 写 `MpscChannel` waiter registration 的 failing test**

- 覆盖：
  - single waiter 正确注册/清除
  - 边沿 signal 只发一次
  - clear / re-arm 不丢 wake

**Step 3: 跑新旧 scheduler 回归测试，确认当前实现至少有一项失败**

Run: `cmake -S . -B build && cmake --build build --target T43-wait_continuation_scheduler_path T44-scheduler_wakeup_coalescing T45-scheduler_ready_budget T46-scheduler_injected_burst_fastpath T47-scheduler_queue_edge_wakeup T48-scheduler_core_loop_stages T49-channel_wait_registration -j4 && ./build/bin/T43-wait_continuation_scheduler_path && ./build/bin/T44-scheduler_wakeup_coalescing && ./build/bin/T45-scheduler_ready_budget && ./build/bin/T46-scheduler_injected_burst_fastpath && ./build/bin/T47-scheduler_queue_edge_wakeup && ./build/bin/T48-scheduler_core_loop_stages && ./build/bin/T49-channel_wait_registration`

Expected: 现有测试通过，`T48` / `T49` 至少有一项 FAIL，证明重构目标被真实约束。

**Step 4: Commit**

```bash
git add test/T43-wait_continuation_scheduler_path.cc test/T44-scheduler_wakeup_coalescing.cc test/T45-scheduler_ready_budget.cc test/T46-scheduler_injected_burst_fastpath.cc test/T47-scheduler_queue_edge_wakeup.cc test/T48-scheduler_core_loop_stages.cc test/T49-channel_wait_registration.cc
git commit -m "test: add scheduler core and channel registration coverage"
```

说明：仅在用户明确要求提交时执行。

### Task 2: 抽离 `WakeCoordinator`

**Files:**
- Create: `galay-kernel/kernel/WakeCoordinator.h`
- Create: `galay-kernel/kernel/WakeCoordinator.cc`
- Modify: `galay-kernel/kernel/KqueueScheduler.h`
- Modify: `galay-kernel/kernel/KqueueScheduler.cc`
- Modify: `galay-kernel/kernel/EpollScheduler.h`
- Modify: `galay-kernel/kernel/EpollScheduler.cc`
- Modify: `galay-kernel/kernel/IOUringScheduler.h`
- Modify: `galay-kernel/kernel/IOUringScheduler.cc`

**Step 1: 写最小类型与状态接口**

- 封装：`sleeping`、`wakeupPending`、`requestWakeOnEdge`、`requestWakeWhileSleeping`、`forceWakeForStop`
- 支持统计 counter

**Step 2: 将三后端中的 wake 决策迁移到统一协调器**

**Step 3: 保持现有 `T44` / `T47` 通过**

Run: `cmake --build build --target T44-scheduler_wakeup_coalescing T47-scheduler_queue_edge_wakeup -j4 && ./build/bin/T44-scheduler_wakeup_coalescing && ./build/bin/T47-scheduler_queue_edge_wakeup`

Expected: PASS。

**Step 4: Commit**

```bash
git add galay-kernel/kernel/WakeCoordinator.h galay-kernel/kernel/WakeCoordinator.cc galay-kernel/kernel/KqueueScheduler.h galay-kernel/kernel/KqueueScheduler.cc galay-kernel/kernel/EpollScheduler.h galay-kernel/kernel/EpollScheduler.cc galay-kernel/kernel/IOUringScheduler.h galay-kernel/kernel/IOUringScheduler.cc
git commit -m "refactor: extract unified wake coordination"
```

说明：仅在用户明确要求提交时执行。

### Task 3: 抽离 `SchedulerCore`

**Files:**
- Create: `galay-kernel/kernel/SchedulerCore.h`
- Create: `galay-kernel/kernel/SchedulerCore.cc`
- Modify: `galay-kernel/kernel/IOScheduler.hpp`
- Modify: `galay-kernel/kernel/KqueueScheduler.cc`
- Modify: `galay-kernel/kernel/EpollScheduler.cc`
- Modify: `galay-kernel/kernel/IOUringScheduler.cc`

**Step 1: 定义统一 loop 阶段接口**

- `collectRemote()`
- `runReady()`
- `beforePoll()`
- `afterPoll()`
- `drainInjected()`

**Step 2: 将现有 ready budget / injected burst credit / injected queue bookkeeping 收敛到 `SchedulerCore`**

**Step 3: 让三后端只保留 poll/completion 相关逻辑**

**Step 4: 跑 scheduler core 回归**

Run: `cmake --build build --target T45-scheduler_ready_budget T46-scheduler_injected_burst_fastpath T48-scheduler_core_loop_stages -j4 && ./build/bin/T45-scheduler_ready_budget && ./build/bin/T46-scheduler_injected_burst_fastpath && ./build/bin/T48-scheduler_core_loop_stages`

Expected: PASS。

**Step 5: Commit**

```bash
git add galay-kernel/kernel/SchedulerCore.h galay-kernel/kernel/SchedulerCore.cc galay-kernel/kernel/IOScheduler.hpp galay-kernel/kernel/KqueueScheduler.cc galay-kernel/kernel/EpollScheduler.cc galay-kernel/kernel/IOUringScheduler.cc
git commit -m "refactor: extract scheduler core loop"
```

说明：仅在用户明确要求提交时执行。

### Task 4: 定义 backend adapter 边界并迁移 `kqueue`

**Files:**
- Create: `galay-kernel/kernel/BackendReactor.h`
- Create: `galay-kernel/kernel/KqueueReactor.h`
- Create: `galay-kernel/kernel/KqueueReactor.cc`
- Modify: `galay-kernel/kernel/KqueueScheduler.h`
- Modify: `galay-kernel/kernel/KqueueScheduler.cc`

**Step 1: 定义 backend 适配器接口**

- `poll(timeout)`
- `registerController(...)`
- `removeController(...)`
- `collectCompletions(...)`
- `notifyPrimitive()` / `consumeWakeEvent()`

**Step 2: 将 `kqueue` 逻辑迁移到 `KqueueReactor`**

**Step 3: 保持本地 `kqueue` scheduler 测试通过**

Run: `cmake --build build --target T27-runtime_stress T39-awaitable_hot_path_helpers T43-wait_continuation_scheduler_path T44-scheduler_wakeup_coalescing T45-scheduler_ready_budget T46-scheduler_injected_burst_fastpath T47-scheduler_queue_edge_wakeup T48-scheduler_core_loop_stages -j4 && ./build/bin/T27-runtime_stress && ./build/bin/T39-awaitable_hot_path_helpers && ./build/bin/T43-wait_continuation_scheduler_path && ./build/bin/T44-scheduler_wakeup_coalescing && ./build/bin/T45-scheduler_ready_budget && ./build/bin/T46-scheduler_injected_burst_fastpath && ./build/bin/T47-scheduler_queue_edge_wakeup && ./build/bin/T48-scheduler_core_loop_stages`

Expected: PASS。

### Task 5: 迁移 `epoll`

**Files:**
- Create: `galay-kernel/kernel/EpollReactor.h`
- Create: `galay-kernel/kernel/EpollReactor.cc`
- Modify: `galay-kernel/kernel/EpollScheduler.h`
- Modify: `galay-kernel/kernel/EpollScheduler.cc`

**Step 1: 将 `epoll` 的 fd/eventfd/register/poll/completion 逻辑迁移到 `EpollReactor`**

**Step 2: 保持调度与唤醒语义通过统一 core/coordinator 驱动**

**Step 3: 在 Linux 上跑针对 `epoll` 的 scheduler 回归**

Run: `cmake -S . -B build-epoll -DGALAY_KERNEL_BACKEND=epoll && cmake --build build-epoll --target $(cd test && printf '%s ' T*.cc | sed 's/\.cc//g') -j4`

Expected: 构建成功；随后全量测试通过。

### Task 6: 迁移 `io_uring`

**Files:**
- Create: `galay-kernel/kernel/IOUringReactor.h`
- Create: `galay-kernel/kernel/IOUringReactor.cc`
- Modify: `galay-kernel/kernel/IOUringScheduler.h`
- Modify: `galay-kernel/kernel/IOUringScheduler.cc`

**Step 1: 将 `io_uring` 的 queue setup / SQE submit / CQE consume 逻辑迁移到 `IOUringReactor`**

**Step 2: 通过统一 core/coordinator 驱动 remote wake、公平性和 continuation 恢复**

**Step 3: 在 Linux 上跑针对 `io_uring` 的 scheduler 回归**

Run: `cmake -S . -B build-uring -DGALAY_KERNEL_BACKEND=io_uring && cmake --build build-uring --target $(cd test && printf '%s ' T*.cc | sed 's/\.cc//g') -j4`

Expected: 构建成功；随后全量测试通过。若缺 `liburing` 或权限，先补依赖/环境，再重跑。

### Task 7: 重构 `MpscChannel` 等待注册与批量 drain 语义

**Files:**
- Modify: `galay-kernel/concurrency/MpscChannel.h`
- Modify: `galay-kernel/kernel/Waker.cc`
- Modify: `galay-kernel/kernel/Coroutine.cc`
- Modify: `test/T49-channel_wait_registration.cc`
- Optionally Modify: `benchmark/B8-mpsc_channel.cc`

**Step 1: 抽象 `WaitRegistration` 或等价结构，显式描述 waiter armed / clear / generation 语义**

**Step 2: 将 `recv()` / `recvBatch()` 共享 drain 路径，减少重复唤醒和过早挂起**

**Step 3: 保持 channel 语义测试通过**

Run: `cmake --build build --target T49-channel_wait_registration -j4 && ./build/bin/T49-channel_wait_registration`

Expected: PASS。

**Step 4: 跑 `B8-MpscChannel`，观察 latency / batch / cross-scheduler 指标**

Run: `cmake --build build --target B8-MpscChannel -j4 && ./build/bin/B8-MpscChannel`

Expected: 相比首轮 `v3`，至少 latency 波动收敛或平均值改善，cross-scheduler throughput 不回退。

### Task 8: 本地 `kqueue` 全量测试与全 benchmark 对比

**Files:**
- Modify: `docs/plans/2026-03-13-scheduler-v3-benchmark-results.md`

**Step 1: 生成 baseline 构建（如缺）**

Run: `cmake -S /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3-baseline -B /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3-baseline/build`

**Step 2: 跑 optimized 全量 tests**

Run: `cmake --build build -j4 && for t in ./build/bin/T*; do echo "=== $t ==="; "$t"; done`

Expected: 全部 PASS。

**Step 3: 跑 baseline 与 optimized 全量 benchmarks**

Run: `for b in ./build/bin/B*; do echo "=== $b ==="; "$b"; done`

以及 baseline 对应 `./.worktrees/v3-baseline/build/bin/B*`。若 baseline 无新增 benchmark（如 `B14`），采用当前源码链接 baseline 库方式补齐。

**Step 4: 更新结果文档**

- 列出每个 benchmark 的 baseline / optimized 数值
- 标注提升 / 持平 / 回退

### Task 9: Ubuntu 上 `epoll` 全量测试与全 benchmark 对比

**Files:**
- Modify: `docs/plans/2026-03-13-scheduler-v3-benchmark-results.md`

**Step 1: SSH 连接 Ubuntu 并准备依赖**

Run: `ssh ubuntu@140.143.142.251`

在远端检查并安装：
- `build-essential`
- `cmake`
- `clang` 或 `g++`
- `liburing-dev`（后续 `io_uring` 用）
- 其他构建必需工具

**Step 2: 构建 baseline / optimized 的 `epoll` 版本**

**Step 3: 跑远端全量 tests**

**Step 4: 跑远端全量 benchmarks**

**Step 5: 收集结果回写文档**

### Task 10: Ubuntu 上 `io_uring` 全量测试与全 benchmark 对比

**Files:**
- Modify: `docs/plans/2026-03-13-scheduler-v3-benchmark-results.md`

**Step 1: 检查 kernel / `liburing` / 权限条件**

**Step 2: 构建 baseline / optimized 的 `io_uring` 版本**

**Step 3: 跑远端全量 tests**

**Step 4: 跑远端全量 benchmarks**

**Step 5: 收集结果回写文档**

### Task 11: 最终汇总与差异清单

**Files:**
- Modify: `docs/plans/2026-03-13-scheduler-v3-benchmark-results.md`
- Optionally Modify: `docs/05-性能测试.md`

**Step 1: 汇总三套后端结果**

- `kqueue`
- `epoll`
- `io_uring`

**Step 2: 为每个 benchmark 标记**

- improved
- neutral
- regressed

**Step 3: 写出回退解释与接受判断**

**Step 4: 最终全量验证**

Run: 
- 本地：`for t in ./build/bin/T*; do "$t"; done && for b in ./build/bin/B*; do "$b"; done`
- Linux `epoll`：全量 tests + benchmarks
- Linux `io_uring`：全量 tests + benchmarks

Expected: 所有测试通过；所有 benchmark 已有 baseline vs optimized 对比结果。
