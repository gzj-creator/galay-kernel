# MpscChannel Waiter Fix Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 修复 `MpscChannel` 在 batch receive 场景中的漏唤醒/错误挂起问题，并完成本地与关键 benchmark 回归。

**Architecture:** 使用原子 waiter 句柄替代共享 `Waker` 发布；`recv/recvBatch` 通过真实 dequeue 结果决定 ready/suspend；发送路径和 size 记账一起收敛，保证 batch 压测与测试路径一致。

**Tech Stack:** C++23, coroutines, moodycamel::ConcurrentQueue, ComputeScheduler, CMake

---

### Task 1: 写失败复现测试

**Files:**
- Create: `test/T38-MpscBatchReceiveStress.cc`
- Test: `test/T37-MpscBatchReceiveProgress.cc`

**Step 1: Write the failing test**

新增一个大消息量单生产者 + batch consumer 的 stress test，要求在有限时间内完成接收；失败时输出 `received/size/producer_done`。

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target T38-MpscBatchReceiveStress -j4 && build/bin/T38-MpscBatchReceiveStress`
Expected: FAIL 或 timeout，表现为消费者未完成且通道仍有积压。

**Step 3: Keep existing progress test**

保留 `T37` 作为较小规模快速回归，不在这一步修改其语义。

**Step 4: Commit**

不单独提交；继续下一任务。

### Task 2: 收敛 waiter 发布与接收状态机

**Files:**
- Modify: `galay-kernel/concurrency/MpscChannel.h`
- Test: `test/T38-MpscBatchReceiveStress.cc`

**Step 1: Write minimal implementation**

把 `m_waker + m_waiting` 收敛为原子 waiter；抽出内部 helper：发布 waiter、撤销 waiter、唤醒 waiter、真实消费单条/批量数据。

**Step 2: Rework awaitables**

让 `await_ready()` / `await_suspend()` 走真实 dequeue 缓存结果，`await_resume()` 负责清理 waiter 并返回缓存或最终 dequeue 结果。

**Step 3: Run targeted tests**

Run: `cmake --build build --target T37-MpscBatchReceiveProgress T38-MpscBatchReceiveStress B8-MpscChannel -j4`
Expected: `T37/T38` PASS，`B8` 不再卡在 batch receive 段。

**Step 4: Commit**

不单独提交；继续全量回归。

### Task 3: 全量回归与 benchmark 复核

**Files:**
- Test: `test/T19-ReadvWritev.cc`
- Test: `test/T20-RingbufferIo.cc`
- Test: `test/T24-Spawn.cc`
- Test: `test/T25-SpawnSimple.cc`
- Test: `test/T26-ConcurrentRecvSend.cc`
- Test: `test/T31-UnsafeChannelDeferredWake.cc`
- Test: `test/T32-IOSchedulerLocalFirst.cc`
- Test: `test/T34-ReadvArrayBorrowed.cc`
- Test: `test/T35-ReadvArrayCountGuard.cc`
- Test: `test/T36-TaskCoreWakePath.cc`
- Test: `test/T37-MpscBatchReceiveProgress.cc`
- Test: `test/T38-MpscBatchReceiveStress.cc`
- Benchmark: `benchmark/B8-MpscChannel.cc`
- Benchmark: `benchmark/B9-UnsafeChannel.cc`

**Step 1: Run local regression**

Run: `cmake --build build --target T19-ReadvWritev T20-RingbufferIo T24-Spawn T25-SpawnSimple T26-ConcurrentRecvSend T31-UnsafeChannelDeferredWake T32-IOSchedulerLocalFirst T34-ReadvArrayBorrowed T35-ReadvArrayCountGuard T36-TaskCoreWakePath T37-MpscBatchReceiveProgress T38-MpscBatchReceiveStress B8-MpscChannel B9-UnsafeChannel -j4`
Expected: tests PASS, `B8` 能完整输出 batch throughput 段。

**Step 2: If local passes, compare throughput**

Run: `build/bin/B8-MpscChannel` and `build/bin/B9-UnsafeChannel`
Expected: `B8` 不再挂死，吞吐可用于和 `B9` 以及后续 Rust/Go 数据对比。

**Step 3: Summarize residual risk**

记录 timeout 路径与远端 epoll Release 回归是否仍需补跑。

**Step 4: Commit**

按用户要求再决定是否提交。
