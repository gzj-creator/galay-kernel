# Scheduler V3 Latency Tuning Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 在保留 `v3` 公平性语义的前提下，收回 pure injected burst 的调度延迟回归。

**Architecture:** 将 fixed ready budget 调整为“默认 budget + injected burst credit”的自适应 pass。local ready work 继续受 budget 约束；当一轮 pass 从纯 injected backlog 开始时，允许当前 pass 继续消费本轮 drain 到的 injected work，避免被人为切成多轮。

**Tech Stack:** C++23、Scheduler/IOSchedulerWorkerState、现有 `test/` 与 `benchmark/` 基建。

---

### Task 1: 写出 latency-tuning regression tests

**Files:**
- Modify: `test/T45-scheduler_ready_budget.cc`
- Create: `test/T46-scheduler_injected_burst_fastpath.cc`

**Step 1: 写 local ready backlog 的 failing test**
- 直接向 `m_worker.scheduleLocal(...)` 填充任务
- 断言首轮 `processPendingCoroutines()` 不会跑完全部 ready task

**Step 2: 写 pure injected backlog 的 failing test**
- 用现有 `spawn(...)` 形成 injected backlog
- 断言单轮 `processPendingCoroutines()` 可以跑完所有 injected task

**Step 3: 运行测试确认当前 injected fast path 失败**

Run: `cmake --build build --target T45-scheduler_ready_budget T46-scheduler_injected_burst_fastpath -j4 && ./build/bin/T45-scheduler_ready_budget && ./build/bin/T46-scheduler_injected_burst_fastpath`

Expected: `T45` PASS，`T46` FAIL。

### Task 2: 实现 injected burst credit

**Files:**
- Modify: `galay-kernel/kernel/KqueueScheduler.cc`
- Modify: `galay-kernel/kernel/EpollScheduler.cc`
- Modify: `galay-kernel/kernel/IOUringScheduler.cc`

**Step 1: 在 `processPendingCoroutines()` 中记录本轮是否从 pure injected backlog 开始**

**Step 2: 将每次 `drainInjected()` 的数量累计到 `burst_credit`**

**Step 3: 在 `ran >= ready_budget` 时，仅当 `burst_credit == 0` 才真正退出 pass**

**Step 4: 运行调度回归测试**

Run: `cmake --build build --target T43-wait_continuation_scheduler_path T44-scheduler_wakeup_coalescing T45-scheduler_ready_budget T46-scheduler_injected_burst_fastpath -j4 && ./build/bin/T43-wait_continuation_scheduler_path && ./build/bin/T44-scheduler_wakeup_coalescing && ./build/bin/T45-scheduler_ready_budget && ./build/bin/T46-scheduler_injected_burst_fastpath`

Expected: 全部 PASS。

### Task 3: 重新跑 benchmark 对比

**Files:**
- Modify: `docs/plans/2026-03-13-scheduler-v3-benchmark-results.md`

**Step 1: 跑 `B14-SchedulerInjectedWakeup`**

Run: `cmake --build build --target B14-SchedulerInjectedWakeup -j4 && ./build/bin/B14-SchedulerInjectedWakeup`

**Step 2: 跑 `B8-mpsc_channel` 作为 canary**

Run: `cmake --build build --target B8-mpsc_channel -j4 && ./build/bin/B8-mpsc_channel`

**Step 3: 更新结果文档，记录调优前后差异**
