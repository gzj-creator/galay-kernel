# Triplet Regression And Crash Closure Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Eliminate the remaining `kqueue` / `epoll` / `io_uring` triplet failures, close the `io_uring` UDP crash path, and rerun the full benchmark triplet matrix with fresh evidence.

**Architecture:** Split the work by root-cause domain instead of by backend: `B8` benchmark validity/performance, `B1` benchmark validity/performance, and `io_uring` UDP crash behavior. For each domain, collect evidence first, add a deterministic regression test where feasible, implement the smallest fix, rerun targeted validation, then rerun the full triplet matrix.

**Tech Stack:** C++23, CMake, custom benchmark scripts, `kqueue`/`epoll`/`io_uring` schedulers, local macOS benchmark runs, remote Ubuntu benchmark runs.

---

### Task 1: Capture B8 Failure Evidence

**Files:**
- Modify: `benchmark/B8-mpsc_channel.cc`
- Test: `test/T68-b8_cross_scheduler_source_case.cc`
- Test: `test/T69-b8_producer_throughput_source_case.cc`
- Test: `test/T71-b8_batch_sample_duration_source_case.cc`
- Test: `test/T72-b8_single_producer_gate_source_case.cc`
- Test: `test/T73-b8_single_sample_duration_source_case.cc`

**Step 1: Save raw failure evidence**

Run:

```bash
sed -n '1,220p' build/benchmark-triplet-2026-03-14-kqueue-final5/report.md
ssh ubuntu@140.143.142.251 "sed -n '1,220p' /home/ubuntu/galay-kernel-triplet-base/build/benchmark-triplet-2026-03-14-epoll-final5/report.md"
ssh ubuntu@140.143.142.251 "sed -n '1,220p' /home/ubuntu/galay-kernel-triplet-base/build/benchmark-triplet-2026-03-14-iouring-final3/report.md"
```

Expected: fresh report snapshots showing the current `B8` failures.

**Step 2: Identify deterministic benchmark bugs**

Inspect the raw `B8` logs per backend and per label. Confirm whether the failures are caused by short-sample noise, scheduler start skew, lost wakeups in the benchmark harness, or real backend regressions.

**Step 3: Write or extend deterministic regression tests**

Add source-case tests that pin the benchmark control-flow bug without using performance thresholds.

**Step 4: Run the new tests and verify they fail before the fix**

Run:

```bash
cmake --build build -j4 --target T68-b8_cross_scheduler_source_case T69-b8_producer_throughput_source_case T71-b8_batch_sample_duration_source_case T72-b8_single_producer_gate_source_case T73-b8_single_sample_duration_source_case
ctest --test-dir build --output-on-failure -R 'T68|T69|T71|T72|T73'
```

Expected: at least one targeted test fails for the newly captured root cause.

**Step 5: Implement the minimal benchmark fix**

Change only the benchmark harness logic required to remove the root cause.

**Step 6: Re-run the targeted tests**

Run the same `ctest` command and confirm all targeted tests pass.

### Task 2: Capture B1 Failure Evidence

**Files:**
- Modify: `benchmark/B1-compute_scheduler.cc`
- Test: `test/T67-benchmark_default_scheduler_count.cc`
- Test: `test/T70-b1_throughput_sample_source_case.cc`

**Step 1: Save raw failure evidence**

Collect the raw `B1` logs for the failing `kqueue` and `io_uring` triplets.

**Step 2: Compare with a passing backend**

Compare the same `B1` sections from `epoll final5` to find whether the issue is sampling methodology or backend-specific behavior.

**Step 3: Write or extend deterministic regression tests**

Add a small source-case test for the identified `B1` harness issue if the root cause is in the benchmark implementation.

**Step 4: Run the tests and confirm the new regression fails before the fix**

Run:

```bash
cmake --build build -j4 --target T67-benchmark_default_scheduler_count T70-b1_throughput_sample_source_case
ctest --test-dir build --output-on-failure -R 'T67|T70'
```

**Step 5: Implement the minimal fix**

Only change the measurement or scheduler setup logic that explains the failure.

**Step 6: Re-run the targeted tests**

Use the same `ctest` command and confirm both tests pass.

### Task 3: Capture io_uring UDP Crash Evidence

**Files:**
- Modify: `benchmark/B6-Udp.cc`
- Modify: `galay-kernel/kernel/IOUringScheduler.cc`
- Modify: `galay-kernel/kernel/Awaitable.h`
- Test: `test/T57-io_uring_timeout_close_lifetime.cc`
- Test: `test/T5-udp_socket.cc`
- Test: `test/T6-udp_server.cc`
- Test: `test/T7-udp_client.cc`

**Step 1: Save crash logs**

Collect the crashing `B5-UdpClient.log` files and the failing compat `B6-Udp` logs from the latest `io_uring` run.

**Step 2: Trace the crash boundary**

Determine whether the crash is in the benchmark, UDP socket lifetime handling, timeout wrapper, or `io_uring` completion/lifetime handling.

**Step 3: Write a deterministic failing regression test**

Add or extend a small `io_uring` lifetime/close regression test that reproduces the same crash class without requiring the full benchmark.

**Step 4: Run the new test and watch it fail**

Run:

```bash
cmake --build build -j4 --target T57-io_uring_timeout_close_lifetime T5-udp_socket T6-udp_server T7-udp_client
ctest --test-dir build --output-on-failure -R 'T57|T5|T6|T7'
```

Expected: the new regression test fails before the fix.

**Step 5: Implement the minimal fix**

Fix the underlying lifetime or completion bug without broad refactoring.

**Step 6: Re-run the targeted tests**

Use the same `ctest` command and confirm the regression is gone.

### Task 4: Rebuild And Run Focused Verification

**Files:**
- Modify: `scripts/run_benchmark_triplet.sh`
- Modify: `scripts/parse_benchmark_triplet.py`

**Step 1: Rebuild the local tree**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

Expected: clean successful build.

**Step 2: Run focused local verification**

Run only the targeted local benchmark binaries needed to validate the fixes before the expensive triplet rerun.

**Step 3: Run focused remote verification**

Run only the failing remote benchmark binaries for `epoll` and `io_uring` to confirm the fixes before the full triplet rerun.

### Task 5: Full Matrix Re-Verification

**Files:**
- Modify: `docs/05-性能测试.md`
- Modify: `docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-results.md`

**Step 1: Run the full local `kqueue` triplet**

Run the latest triplet command in background and wait for a fresh `report.md`.

**Step 2: Run the full remote `epoll` triplet**

Run the latest triplet command in background and wait for a fresh `report.md`.

**Step 3: Run the full remote `io_uring` triplet**

Run the latest triplet command in background and wait for a fresh `report.md`.

**Step 4: Verify reports**

Run:

```bash
sed -n '1,220p' build/<latest-kqueue-report>/report.md
ssh ubuntu@140.143.142.251 "sed -n '1,220p' /home/ubuntu/.../epoll-.../report.md"
ssh ubuntu@140.143.142.251 "sed -n '1,220p' /home/ubuntu/.../iouring-.../report.md"
```

Expected: all gating rows pass, crash paths gone, and the matrix is updated with fresh data.

**Step 5: Update documentation**

Refresh the benchmark summary docs with the new matrix, key wins, and any remaining accepted limitations.
