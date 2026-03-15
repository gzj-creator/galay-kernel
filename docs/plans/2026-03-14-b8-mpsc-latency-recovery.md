# B8 Mpsc Latency Recovery Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Reduce `B8-MpscChannel` latency noise by adding a consumer-start barrier to the benchmark and a small recv-side burst drain to `MpscChannel`.

**Architecture:** Keep the public API unchanged. Add one benchmark-only synchronization helper for the latency harness, and add one internal single-consumer prefetch buffer inside `MpscChannel` so repeated `recv()` calls can drain bursts with fewer queue touches while preserving ordering and `size()` semantics.

**Tech Stack:** C++20 coroutines, moodycamel concurrent queue, existing `T*` test executables, `B8-MpscChannel` benchmark

---

### Task 1: Benchmark readiness barrier

**Files:**
- Create: `benchmark/BenchmarkSync.h`
- Create: `test/T59-benchmark_sync_wait_ready.cc`
- Modify: `benchmark/B8-mpsc_channel.cc`

**Step 1: Write the failing test**

Add a small test that includes `benchmark/BenchmarkSync.h` and verifies:
- waiting on an already-true flag returns immediately
- waiting on a flag flipped by another thread returns `true`
- waiting on a never-flipped flag times out and returns `false`

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target T59-benchmark_sync_wait_ready -j4`
Expected: FAIL because `benchmark/BenchmarkSync.h` does not exist yet.

**Step 3: Write minimal implementation**

Implement a tiny polling helper in `benchmark/BenchmarkSync.h` and use it in `B8-mpsc_channel.cc` so `benchLatency()` waits for the consumer coroutine to start before launching the producer thread.

**Step 4: Run test to verify it passes**

Run: `cmake --build build --target T59-benchmark_sync_wait_ready B8-MpscChannel -j4`
Expected: PASS

### Task 2: Recv-side burst drain

**Files:**
- Create: `test/MpscChannelTestAccess.h`
- Create: `test/T58-mpsc_single_recv_prefetch.cc`
- Modify: `galay-kernel/concurrency/MpscChannel.h`

**Step 1: Write the failing test**

Add a test that:
- preloads a `MpscChannel<int>` with a burst of ordered values
- performs one `tryRecv()`
- asserts that a test-only access helper can observe prefetched values buffered inside the channel
- asserts `size()` still reflects all not-yet-delivered values
- drains the remainder with `tryRecvBatch()` and verifies order and count

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target T58-mpsc_single_recv_prefetch -j4`
Expected: FAIL because the test-only prefetch inspection hook and internal prefetch buffer do not exist yet.

**Step 3: Write minimal implementation**

Add a small fixed-size single-consumer prefetch buffer to `MpscChannel` and teach `tryRecv()` / `tryRecvBatch()` to use it without changing the public API or breaking ordering/`size()`.

**Step 4: Run test to verify it passes**

Run: `cmake --build build --target T58-mpsc_single_recv_prefetch T14-mpsc_channel -j4`
Expected: PASS

### Task 3: Focused verification

**Files:**
- Modify: `docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-results.md` if new numbers materially change the interpretation

**Step 1: Build focused targets**

Run: `cmake --build build --target T58-mpsc_single_recv_prefetch T59-benchmark_sync_wait_ready T14-mpsc_channel B8-MpscChannel -j4`

**Step 2: Run focused tests**

Run:
- `./build/bin/T58-mpsc_single_recv_prefetch`
- `./build/bin/T59-benchmark_sync_wait_ready`
- `./build/bin/T14-mpsc_channel`

**Step 3: Run focused benchmark evidence**

Run: `./build/bin/B8-MpscChannel`

Capture the latency line and compare it with the previous median/spot samples before claiming improvement.

## Verification Notes

- `T58-mpsc_single_recv_prefetch`: PASS
- `T59-benchmark_sync_wait_ready`: PASS
- `T14-mpsc_channel`: PASS
- `T37-mpsc_batch_receive_progress`: PASS
- `T38-mpsc_batch_receive_stress`: PASS

Focused `B8-MpscChannel` samples after the change:

- single producer throughput median: `20.83M msg/s`
- multi producer throughput median: `18.52M msg/s`
- batch throughput median: `19.23M msg/s`
- latency median: `24.24 us`
- cross-scheduler throughput median: `17.86M msg/s`
- sustained throughput median: `10.92M msg/s`

Compared with the prior refactored triplet median in `docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-results.md`, the focused local `B8` latency sample moved from `1333.66 us` down to `24.24 us`, and the main throughput paths also recovered into or above the earlier `v3` band.
