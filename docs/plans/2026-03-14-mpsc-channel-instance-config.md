# MpscChannel Instance Config Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make `MpscChannel` default batch size and single-recv prefetch limit configurable per instance through constructor defaults.

**Architecture:** Replace type-level fixed constants with per-instance fields initialized by the constructor. Keep the public API ergonomic by adding no-arg `recvBatch()` / `tryRecvBatch()` overloads that read the instance defaults, while keeping the existing explicit-count overloads.

**Tech Stack:** C++20, `MpscChannel`, existing `T14/T37/T38/T58` tests

---

### Task 1: Lock the API with a failing test

**Files:**
- Modify: `test/T58-mpsc_single_recv_prefetch.cc`

**Step 1: Write the failing test**

Extend `T58` so it constructs `MpscChannel<int>` with a custom default batch size and prefetch limit, then verifies:

- no-arg `tryRecvBatch()` uses the configured batch size
- prefetch count never exceeds the configured limit

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target T58-mpsc_single_recv_prefetch -j4`
Then: `./build/bin/T58-mpsc_single_recv_prefetch`
Expected: compile failure or runtime failure because the constructor / overloads do not exist yet.

### Task 2: Implement instance configuration

**Files:**
- Modify: `galay-kernel/concurrency/MpscChannel.h`
- Modify: `test/MpscChannelTestAccess.h`

**Step 1: Write minimal implementation**

- add constructor parameters with defaults
- store instance batch size and prefetch limit
- replace fixed prefetch storage with dynamic storage
- add no-arg `recvBatch()` / `tryRecvBatch()` overloads using instance defaults

**Step 2: Run focused tests**

Run:
- `cmake --build build --target T58-mpsc_single_recv_prefetch T14-mpsc_channel T37-mpsc_batch_receive_progress T38-mpsc_batch_receive_stress -j4`
- `./build/bin/T58-mpsc_single_recv_prefetch`
- `./build/bin/T14-mpsc_channel`
- `./build/bin/T37-mpsc_batch_receive_progress`
- `./build/bin/T38-mpsc_batch_receive_stress`

Expected: all pass

## Verification Notes

- RED:
  - `cmake --build build --target T58-mpsc_single_recv_prefetch -j4`
  - failed with `no matching constructor for initialization of 'MpscChannel<int>'`
- GREEN:
  - `cmake --build build --target T58-mpsc_single_recv_prefetch T14-mpsc_channel T37-mpsc_batch_receive_progress T38-mpsc_batch_receive_stress B8-MpscChannel -j4`
  - `./build/bin/T58-mpsc_single_recv_prefetch`
  - `./build/bin/T14-mpsc_channel`
  - `./build/bin/T37-mpsc_batch_receive_progress`
  - `./build/bin/T38-mpsc_batch_receive_stress`
  - `./build/bin/B8-MpscChannel`

Observed focused `B8` sample after the API change:

- latency: `2.41 us`
- single producer throughput: `15.63M msg/s`
- multi producer throughput: `18.52M msg/s`
- batch throughput: `18.52M msg/s`
- cross-scheduler throughput: `19.61M msg/s`
- sustained throughput: `12.10M msg/s`
