# Single Benchmark Triplet And v3.0.1 Rollout Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Convert benchmark verification to single-benchmark three-backend sequential comparison, eliminate benchmark-driven crash progression, and prepare gated `v3.0.1` adaptation across downstream `galay-*` repositories.

**Architecture:** Extend the existing matrix and triplet runners with benchmark-local filtering and historical crash quarantines, then add a higher-level single-benchmark orchestration layer that serializes `kqueue`, `epoll`, and `io_uring`. After kernel-side stability is proven with fresh single-benchmark evidence, adapt downstream repositories in dependency order using isolated `v3.0.1` worktrees.

**Tech Stack:** Bash orchestration scripts, Python report parsing, C++23 benchmark binaries, local macOS `kqueue`, remote Ubuntu `epoll`/`io_uring`, multi-repo CMake consumers.

---

### Task 1: Add Benchmark-Local Matrix Filtering

**Files:**
- Modify: `scripts/run_benchmark_matrix.sh`
- Test: `scripts/tests/test_run_benchmark_matrix.py`

**Step 1: Write the failing tests**

Add tests that prove:

- only one standalone benchmark can be selected
- paired benchmarks are skipped as a pair when filtered out
- filtered benchmarks produce explicit `[benchmark-skipped]` logs instead of accidental execution

**Step 2: Run the script tests to verify they fail**

Run:

```bash
python3 -m unittest scripts.tests.test_run_benchmark_matrix
```

Expected: FAIL because the runner does not yet support the new benchmark-local behavior cleanly.

**Step 3: Implement minimal matrix filtering**

Add the smallest runner changes required to support:

- benchmark-local selection
- pair-aware skipping
- explicit skipped logs for filtered entries

**Step 4: Re-run the matrix script tests**

Run:

```bash
python3 -m unittest scripts.tests.test_run_benchmark_matrix
```

Expected: PASS.

**Step 5: Checkpoint**

Run:

```bash
git status --short scripts/run_benchmark_matrix.sh scripts/tests/test_run_benchmark_matrix.py
```

Expected: only the intended files are changed.

### Task 2: Add Triplet Crash Quarantine And Single-Benchmark Surface

**Files:**
- Modify: `scripts/run_benchmark_triplet.sh`
- Modify: `scripts/parse_benchmark_triplet.py`
- Test: `scripts/tests/test_run_benchmark_triplet.py`
- Test: `scripts/tests/test_parse_benchmark_triplet.py`

**Step 1: Write the failing tests**

Add tests that prove:

- `io_uring` historical UDP crash paths are classified before execution
- the triplet dry run shows `B4/B5` skipped for historical `io_uring` labels
- historical `io_uring` compat `B6` is marked unsupported instead of executed
- partial single-benchmark reports still parse cleanly

**Step 2: Run the script tests to verify they fail**

Run:

```bash
python3 -m unittest scripts.tests.test_run_benchmark_triplet scripts.tests.test_parse_benchmark_triplet
```

Expected: FAIL because the runner/parser still assumes broad triplet execution.

**Step 3: Implement minimal triplet quarantine and parsing support**

Change only what is required so that:

- historical `io_uring` UDP crash paths never run
- triplet output remains parseable when only one benchmark is present
- unsupported historical paths are explicit in logs and reports

**Step 4: Re-run the triplet/parser tests**

Run:

```bash
python3 -m unittest scripts.tests.test_run_benchmark_triplet scripts.tests.test_parse_benchmark_triplet
```

Expected: PASS.

**Step 5: Checkpoint**

Run:

```bash
git status --short scripts/run_benchmark_triplet.sh scripts/parse_benchmark_triplet.py scripts/tests/test_run_benchmark_triplet.py scripts/tests/test_parse_benchmark_triplet.py
```

Expected: only the intended files are changed.

### Task 3: Add Single-Benchmark Three-Backend Orchestrator

**Files:**
- Create: `scripts/run_single_benchmark_triplet.sh`
- Create: `scripts/tests/test_run_single_benchmark_triplet.py`

**Step 1: Write the failing tests**

Add tests that prove:

- the runner accepts one benchmark name
- backend order is always `kqueue -> epoll -> io_uring`
- each backend delegates to the filtered triplet runner
- output roots are separated per backend

**Step 2: Run the new tests to verify they fail**

Run:

```bash
python3 -m unittest scripts.tests.test_run_single_benchmark_triplet
```

Expected: FAIL because the orchestrator does not exist yet.

**Step 3: Implement the minimal orchestrator**

Create a thin wrapper that:

- accepts exactly one benchmark target
- runs one backend at a time
- forwards the benchmark-local filter into the triplet runner
- stops immediately on any unexpected execution error

**Step 4: Re-run the new tests**

Run:

```bash
python3 -m unittest scripts.tests.test_run_single_benchmark_triplet
```

Expected: PASS.

**Step 5: Checkpoint**

Run:

```bash
git status --short scripts/run_single_benchmark_triplet.sh scripts/tests/test_run_single_benchmark_triplet.py
```

Expected: only the intended files are changed.

### Task 4: Re-Verify Crash-First Benchmarks Sequentially

**Files:**
- Modify: `docs/05-性能测试.md`
- Create: `docs/plans/2026-03-15-single-benchmark-triplet-results.md`

**Step 1: Run `B5-UdpClient` sequentially across all three backends**

Run the new single-benchmark runner with `B5-UdpClient`.

Expected:

- `kqueue` completes cleanly
- `epoll` completes cleanly
- `io_uring` historical labels are skipped without crashes
- no `Segmentation fault`, `Killed`, or `dumped core` appears in logs

**Step 2: Run `B6-Udp` sequentially across all three backends**

Run the same flow for `B6-Udp`.

Expected:

- refactored `io_uring` executes
- historical `io_uring` compat paths are explicit `unsupported`
- no crash signatures appear

**Step 3: Parse and summarize the fresh per-benchmark reports**

Record:

- backend-by-backend before/after values
- whether the benchmark is stable enough to accept as a valid comparison unit

**Step 4: Update the results doc**

Add the exact paths, dates, and conclusions for `B5` and `B6`.

### Task 5: Re-Verify Scheduler And Channel Benchmarks Sequentially

**Files:**
- Modify: `docs/05-性能测试.md`
- Modify: `docs/plans/2026-03-15-single-benchmark-triplet-results.md`

**Step 1: Run `B1-ComputeScheduler` and `B14-SchedulerInjectedWakeup` one at a time**

Expected: fresh three-backend before/after evidence with no mixed-run contamination.

**Step 2: Run `B8-MpscChannel`, `B9-UnsafeChannel`, and `B10-Ringbuffer` one at a time**

Expected:

- one benchmark per run
- one backend at a time
- no backend concurrency
- no re-use of stale aggregate triplet data

**Step 3: Update the results doc after each benchmark**

For each benchmark, record:

- `kqueue` matrix
- `epoll` matrix
- `io_uring` matrix
- accepted / rejected / needs-fix verdict

### Task 6: Re-Verify TCP And File Benchmarks Sequentially

**Files:**
- Modify: `docs/05-性能测试.md`
- Modify: `docs/plans/2026-03-15-single-benchmark-triplet-results.md`

**Step 1: Run `B3-TcpClient`, `B7-FileIo`, `B12-TcpIovClient`, and `B13-Sendfile` one at a time**

Expected: each benchmark has its own fresh three-backend result set.

**Step 2: Confirm global benchmark conclusions only use per-benchmark fresh data**

Reject any conclusion that comes from:

- mixed benchmark runs
- parallel backend runs
- stale triplet outputs from before the runner hardening

### Task 7: Open Downstream `v3.0.1` Adaptation Wave

**Files:**
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-utils/CMakeLists.txt`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-ssl/CMakeLists.txt`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-http/CMakeLists.txt`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-rpc/CMakeLists.txt`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mcp/CMakeLists.txt`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-redis/CMakeLists.txt`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mysql/CMakeLists.txt`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-mongo/CMakeLists.txt`
- Modify: `/Users/gongzhijie/Desktop/projects/git/galay-etcd/CMakeLists.txt`

**Step 1: Create isolated `v3.0.1` worktrees or branches in dependency order**

Order:

1. `galay-utils`
2. `galay-ssl`
3. `galay-http`
4. `galay-rpc`
5. `galay-mcp`
6. `galay-redis`
7. `galay-mysql`
8. `galay-mongo`
9. `galay-etcd`

Expected: each repo has an isolated place to adapt without contaminating current work.

**Step 2: Update version metadata and kernel dependency expectations**

For each repo:

- set the repository version line or package config to `v3.0.1`-aligned metadata
- ensure it resolves the verified `galay-kernel`

**Step 3: Build and test each repo before moving to the next**

Run that repo's local build and test entry points.

Expected: no repo moves forward with a known compile or runtime defect.

**Step 4: Record repo-specific adaptation notes**

Update a rollout tracking doc with:

- repo name
- adaptation status
- build/test result
- remaining blockers, if any

### Task 8: Final Verification And Documentation Sync

**Files:**
- Modify: `docs/05-性能测试.md`
- Modify: `docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-results.md`
- Modify: `docs/plans/2026-03-15-single-benchmark-triplet-results.md`

**Step 1: Re-run the script unit test suite**

Run:

```bash
python3 -m unittest scripts.tests.test_run_benchmark_matrix scripts.tests.test_run_benchmark_triplet scripts.tests.test_parse_benchmark_triplet scripts.tests.test_run_single_benchmark_triplet
```

Expected: PASS.

**Step 2: Re-run targeted kernel smoke verification**

At minimum:

- current local test matrix
- current remote `epoll` test matrix
- current remote `io_uring` crash smoke

Expected: zero crash signatures and zero test failures.

**Step 3: Sync documentation**

Publish:

- the per-benchmark three-backend comparison results
- the crash-free validation note
- the downstream `v3.0.1` rollout status

**Step 4: Final checkpoint**

Run:

```bash
git status --short
```

Expected: only intentional changes remain for review.
