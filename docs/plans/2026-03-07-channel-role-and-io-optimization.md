# Channel Role And IO Optimization Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 收敛通道职责与 benchmark 口径，停止对 `MpscChannel` 做单线程性能追逐，并把后续优化重心切换到 IO 热路径。

**Architecture:** `UnsafeChannel` 作为单线程高性能通道，`MpscChannel` 作为跨线程正确性通道；benchmark 分为单线程与跨线程两类；优化主线从 channel 切换为 epoll/kqueue/reactor/socket awaitable 热路径。

**Tech Stack:** C++23, coroutines, epoll, kqueue, moodycamel::ConcurrentQueue, galay-kernel benchmarks/tests

---

### Task 1: 固化通道职责与 benchmark 口径

**Files:**
- Modify: `benchmark/B8-MpscChannel.cc`
- Modify: `benchmark/B9-UnsafeChannel.cc`
- Modify: `docs/05-性能测试.md`
- Modify: `docs/14-并发.md`

**Step 1: Write the failing documentation/test expectation**

把 benchmark 头部说明和文档中的 channel 定位改成明确区分：
- `UnsafeChannel` = 单线程/同调度器高性能通道
- `MpscChannel` = 跨线程 MPSC 通道

**Step 2: Run quick verification**

Run: `cmake --build build --target B8-MpscChannel B9-UnsafeChannel -j4`
Expected: build PASS

**Step 3: Keep public API unchanged**

不修改 `UnsafeChannel` / `MpscChannel` 对外接口；本任务只收敛定位与说明。

**Step 4: Commit**

```bash
git add benchmark/B8-MpscChannel.cc benchmark/B9-UnsafeChannel.cc docs/05-性能测试.md docs/14-并发.md
git commit -m "docs: clarify channel benchmark roles"
```

### Task 2: 固定当前 IO benchmark 基线

**Files:**
- Modify: `docs/05-性能测试.md`
- Modify: `benchmark/B2-TcpServer.cc`
- Modify: `benchmark/B3-TcpClient.cc`
- Modify: `benchmark/B11-TcpIovServer.cc`
- Modify: `benchmark/B12-TcpIovClient.cc`

**Step 1: Record current baseline fields**

给 benchmark 输出补上明确模式标签，例如 backend、build type、plain/iov、single/cross-thread。

**Step 2: Run baseline collection**

Run: `cmake --build build --target B2-TcpServer B3-TcpClient B11-TcpIovServer B12-TcpIovClient -j4`
Expected: build PASS

**Step 3: Document comparison rules**

把本地 kqueue、远端 epoll Release、Rust、Go 的对比口径写清楚，避免不同模式直接横比。

**Step 4: Commit**

```bash
git add docs/05-性能测试.md benchmark/B2-TcpServer.cc benchmark/B3-TcpClient.cc benchmark/B11-TcpIovServer.cc benchmark/B12-TcpIovClient.cc
git commit -m "bench: normalize IO benchmark baseline metadata"
```

### Task 3: 优化 epoll + kqueue 事件分发热路径

**Files:**
- Modify: `galay-kernel/kernel/EpollScheduler.cc`
- Modify: `galay-kernel/kernel/EpollScheduler.h`
- Modify: `galay-kernel/kernel/KqueueScheduler.cc`
- Modify: `galay-kernel/kernel/KqueueScheduler.h`
- Modify: `galay-kernel/kernel/IOScheduler.hpp`
- Test: `test/T32-IOSchedulerLocalFirst.cc`
- Test: `test/T33-EpollAcceptRearm.cc`

**Step 1: Write or tighten failing regression if needed**

先为事件批量 drain / 本地优先 / rearm 行为补或收紧回归测试，再动实现。

**Step 2: Implement minimal hot-path flattening**

目标：减少每个已完成事件上的重复包装、重复分支与重复调度调用。

**Step 3: Run focused tests**

Run: `cmake --build build --target T32-IOSchedulerLocalFirst T33-EpollAcceptRearm -j4`
Expected: PASS

**Step 4: Commit**

```bash
git add galay-kernel/kernel/EpollScheduler.cc galay-kernel/kernel/EpollScheduler.h galay-kernel/kernel/KqueueScheduler.cc galay-kernel/kernel/KqueueScheduler.h galay-kernel/kernel/IOScheduler.hpp test/T32-IOSchedulerLocalFirst.cc test/T33-EpollAcceptRearm.cc
git commit -m "perf(io): flatten epoll and kqueue dispatch path"
```

### Task 4: 优化 socket awaitable 热路径

**Files:**
- Modify: `galay-kernel/kernel/Awaitable.h`
- Modify: `galay-kernel/kernel/Awaitable.inl`
- Modify: `galay-kernel/async/TcpSocket.h`
- Test: `test/T19-ReadvWritev.cc`
- Test: `test/T20-RingbufferIo.cc`
- Test: `test/T26-ConcurrentRecvSend.cc`
- Test: `test/T34-ReadvArrayBorrowed.cc`
- Test: `test/T35-ReadvArrayCountGuard.cc`

**Step 1: Write or tighten failing tests if needed**

针对 read/write/readv/writev 的 borrowed path 和 hot path 复用补测试约束。

**Step 2: Implement minimal allocation/dispatch reduction**

减少 awaitable 构造、IO context 组装、完成回填中的不必要开销，保持外部 API 不变。

**Step 3: Run focused tests**

Run: `cmake --build build --target T19-ReadvWritev T20-RingbufferIo T26-ConcurrentRecvSend T34-ReadvArrayBorrowed T35-ReadvArrayCountGuard -j4`
Expected: PASS

**Step 4: Commit**

```bash
git add galay-kernel/kernel/Awaitable.h galay-kernel/kernel/Awaitable.inl galay-kernel/async/TcpSocket.h test/T19-ReadvWritev.cc test/T20-RingbufferIo.cc test/T26-ConcurrentRecvSend.cc test/T34-ReadvArrayBorrowed.cc test/T35-ReadvArrayCountGuard.cc
git commit -m "perf(io): reduce socket awaitable hot path overhead"
```

### Task 5: 回归与对比

**Files:**
- Test: `test/T19-ReadvWritev.cc`
- Test: `test/T20-RingbufferIo.cc`
- Test: `test/T24-Spawn.cc`
- Test: `test/T25-SpawnSimple.cc`
- Test: `test/T26-ConcurrentRecvSend.cc`
- Test: `test/T31-UnsafeChannelDeferredWake.cc`
- Test: `test/T32-IOSchedulerLocalFirst.cc`
- Test: `test/T33-EpollAcceptRearm.cc`
- Test: `test/T34-ReadvArrayBorrowed.cc`
- Test: `test/T35-ReadvArrayCountGuard.cc`
- Test: `test/T36-TaskCoreWakePath.cc`
- Test: `test/T37-MpscBatchReceiveProgress.cc`
- Test: `test/T38-MpscBatchReceiveStress.cc`
- Benchmark: `benchmark/B8-MpscChannel.cc`
- Benchmark: `benchmark/B9-UnsafeChannel.cc`
- Benchmark: `benchmark/B2-TcpServer.cc`
- Benchmark: `benchmark/B3-TcpClient.cc`
- Benchmark: `benchmark/B11-TcpIovServer.cc`
- Benchmark: `benchmark/B12-TcpIovClient.cc`

**Step 1: Run local regression**

Run: `cmake --build build --target T19-ReadvWritev T20-RingbufferIo T24-Spawn T25-SpawnSimple T26-ConcurrentRecvSend T31-UnsafeChannelDeferredWake T32-IOSchedulerLocalFirst T33-EpollAcceptRearm T34-ReadvArrayBorrowed T35-ReadvArrayCountGuard T36-TaskCoreWakePath T37-MpscBatchReceiveProgress T38-MpscBatchReceiveStress B8-MpscChannel B9-UnsafeChannel B2-TcpServer B3-TcpClient B11-TcpIovServer B12-TcpIovClient -j4`
Expected: build PASS

**Step 2: Run remote Ubuntu Release + epoll verification**

按既有远端路径同步并运行关键 tests/benchmarks，记录 plain/iov 与 Rust/Go 的差距变化。

**Step 3: Summarize remaining bottlenecks**

若通道口径已分离且 IO 仍落后，优先归因到 reactor/completion/uring，而不是 channel。

**Step 4: Commit**

```bash
git add docs/05-性能测试.md
# 加上本轮实际修改文件
 git commit -m "perf(io): benchmark and optimize IO hot paths"
```
