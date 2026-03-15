# Eliminate Benchmark Fails Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Stabilize `B1-ComputeScheduler`, `B10-Ringbuffer`, and `B14-SchedulerInjectedWakeup` so the benchmark triplet passes without weakening parser gate semantics.

**Architecture:** Reuse the current benchmark sources as the single harness for all three compared code states by extending the compat benchmark flow from `B14` to `B1` and `B10`. Improve only the benchmark measurement logic that is currently noisy, then verify with focused reruns before a fresh full triplet.

**Tech Stack:** C++23 benchmarks, Bash triplet orchestration, Python parser tests, existing benchmark/report docs

---

### Task 1: Add failing compat-runner coverage

**Files:**
- Modify: `scripts/tests/test_run_benchmark_triplet.py`
- Reuse: `scripts/run_benchmark_triplet.sh`

**Step 1: Write the failing test**

Add a test that expects the triplet runner dry-run output to include compat binaries for:

- `B1-ComputeScheduler`
- `B10-Ringbuffer`
- `B14-SchedulerInjectedWakeup`

**Step 2: Run test to verify it fails**

Run:

```bash
python3 -m unittest scripts.tests.test_run_benchmark_triplet -v
```

Expected: FAIL because only `B14` currently has compat orchestration.

**Step 3: Write minimal implementation**

Extend `scripts/run_benchmark_triplet.sh` so the historical `pre-v3` and `v3` labels build and run compat binaries for `B1`, `B10`, and `B14`.

**Step 4: Run test to verify it passes**

Run:

```bash
python3 -m unittest scripts.tests.test_run_benchmark_triplet -v
```

Expected: PASS.

### Task 2: Add failing benchmark sync helper coverage

**Files:**
- Modify: `benchmark/BenchmarkSync.h`
- Create: `test/T60-benchmark_completion_latch.cc`
- Modify: `test/CMakeLists.txt`

**Step 1: Write the failing test**

Add a focused test for a benchmark completion latch helper that verifies:

- `arrive()` wakes a waiting thread once the target count is reached
- `wait()` returns immediately when already satisfied
- timeout returns `false` when the count never reaches the target

**Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target T60-benchmark_completion_latch -j4
./build/bin/T60-benchmark_completion_latch
```

Expected: FAIL because the completion latch helper does not exist yet.

**Step 3: Write minimal implementation**

Add the helper to `benchmark/BenchmarkSync.h` using a condition variable based implementation suitable for benchmarks.

**Step 4: Run test to verify it passes**

Run:

```bash
cmake --build build --target T60-benchmark_completion_latch -j4
./build/bin/T60-benchmark_completion_latch
```

Expected: PASS.

### Task 3: Stabilize `B1-ComputeScheduler`

**Files:**
- Modify: `benchmark/B1-compute_scheduler.cc`
- Reuse: `benchmark/BenchmarkSync.h`

**Step 1: Write the failing expectation**

Use the new helper and remove all `sleep_for(1ms)` completion polling from `B1`.

Expected benchmark behavior:

- warmup completion uses a latch
- measured completion uses a latch
- throughput uses sub-millisecond precision

**Step 2: Run focused benchmark to capture the current noisy baseline**

Run:

```bash
cmake --build build --target B1-ComputeScheduler -j4
./build/bin/B1-ComputeScheduler
```

Expected: output still comes from the old harness before edits.

**Step 3: Write minimal implementation**

Replace the polling waits with the benchmark completion latch and compute throughput from a high precision elapsed duration.

**Step 4: Re-run focused benchmark**

Run:

```bash
cmake --build build --target B1-ComputeScheduler -j4
./build/bin/B1-ComputeScheduler
```

Expected: output shape stays parser-compatible and no `sleep_for(1ms)` wait loops remain.

### Task 4: Stabilize `B14-SchedulerInjectedWakeup`

**Files:**
- Modify: `benchmark/B14-scheduler_injected_wakeup.cc`

**Step 1: Write the failing expectation**

Split throughput and latency into separate scheduler lifecycles with an explicit latency warmup.

**Step 2: Run focused benchmark to capture the current shape**

Run:

```bash
cmake --build build --target B14-SchedulerInjectedWakeup -j4
./build/bin/B14-SchedulerInjectedWakeup
```

Expected: throughput and latency currently run on the same scheduler instance.

**Step 3: Write minimal implementation**

Create separate benchmark phases so latency sampling starts from a clean scheduler state.

**Step 4: Re-run focused benchmark**

Run:

```bash
cmake --build build --target B14-SchedulerInjectedWakeup -j4
./build/bin/B14-SchedulerInjectedWakeup
```

Expected: output remains parser-compatible and latency phase runs after its own startup and warmup.

### Task 5: Stabilize `B10-Ringbuffer` write sampling

**Files:**
- Modify: `benchmark/B10-Ringbuffer.cc`

**Step 1: Write the failing expectation**

Refactor the write-throughput benchmark so it runs for a minimum duration rather than only until a small fixed elapsed time window finishes.

**Step 2: Run focused benchmark to capture the current shape**

Run:

```bash
cmake --build build --target B10-Ringbuffer -j4
./build/bin/B10-Ringbuffer
```

Expected: one run can still produce a low write-throughput outlier.

**Step 3: Write minimal implementation**

Keep chunk-size coverage unchanged, but require enough elapsed time before accepting a write-throughput sample.

**Step 4: Re-run focused benchmark**

Run:

```bash
cmake --build build --target B10-Ringbuffer -j4
./build/bin/B10-Ringbuffer
```

Expected: output remains parser-compatible and write-throughput samples become less volatile.

### Task 6: Verify parser compatibility

**Files:**
- Modify: `scripts/tests/test_parse_benchmark_triplet.py`
- Reuse: `scripts/parse_benchmark_triplet.py`

**Step 1: Write the failing test**

Add or refresh fixture coverage for any changed benchmark output details in:

- `B1-ComputeScheduler`
- `B10-Ringbuffer`
- `B14-SchedulerInjectedWakeup`

**Step 2: Run test to verify it fails if parser expectations are stale**

Run:

```bash
python3 -m unittest scripts.tests.test_parse_benchmark_triplet -v
```

Expected: FAIL if fixture text still assumes the old output shape.

**Step 3: Write minimal implementation**

Refresh fixtures or parser patterns only as needed to keep metric extraction identical.

**Step 4: Run test to verify it passes**

Run:

```bash
python3 -m unittest scripts.tests.test_parse_benchmark_triplet -v
```

Expected: PASS.

### Task 7: Run focused verification

**Files:**
- Reuse: `build/bin/B1-ComputeScheduler`
- Reuse: `build/bin/B10-Ringbuffer`
- Reuse: `build/bin/B14-SchedulerInjectedWakeup`

**Step 1: Run focused benchmarks multiple times**

Run:

```bash
./build/bin/B1-ComputeScheduler
./build/bin/B1-ComputeScheduler
./build/bin/B1-ComputeScheduler
./build/bin/B10-Ringbuffer
./build/bin/B10-Ringbuffer
./build/bin/B10-Ringbuffer
./build/bin/B14-SchedulerInjectedWakeup
./build/bin/B14-SchedulerInjectedWakeup
./build/bin/B14-SchedulerInjectedWakeup
```

Expected: variance narrows enough that the full triplet has a realistic chance to pass.

### Task 8: Re-run the triplet and sync docs

**Files:**
- Modify: `docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-results.md`
- Modify: `docs/05-性能测试.md`

**Step 1: Run the full triplet**

Run:

```bash
bash scripts/run_benchmark_triplet.sh \
  --pre-v3-ref 09c8917 \
  --v3-ref cde3da1 \
  --refactored-path /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3 \
  --output-root /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14-final \
  --repeat 3
```

Expected: all three labels emit fresh logs under the final output root.

**Step 2: Parse the report**

Run:

```bash
python3 scripts/parse_benchmark_triplet.py \
  --input-root /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14-final \
  --json-out /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14-final/report.json \
  --markdown-out /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14-final/report.md
```

Expected: exit code `0` and `overall_pass = true`.

**Step 3: Update the docs**

Sync the final matrix and performance summary into:

- `docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-results.md`
- `docs/05-性能测试.md`

**Step 4: Re-open docs and cross-check**

Verify the numbers in the docs match `report.json`.
