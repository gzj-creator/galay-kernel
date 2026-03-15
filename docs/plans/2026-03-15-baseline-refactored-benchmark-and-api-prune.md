# Baseline Refactored Benchmark And API Prune Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the old three-state benchmark workflow with a strict `baseline(cde3da1) -> refactored` comparison, remove obsolete benchmark/report interfaces, and prune dead or unusable transitional APIs/files.

**Architecture:** Keep the hardened single-benchmark three-backend execution model, but collapse compared labels to `baseline` and `refactored`. Preserve compat execution only where the baseline needs the current benchmark source for comparison. Audit the runtime/task public surface against the approved keep-list, remove dead transitional files, then re-verify with full tests and fresh serialized benchmarks.

**Tech Stack:** Bash orchestration scripts, Python report parsing, C++23 coroutine/runtime code, CMake, local macOS `kqueue`, remote Ubuntu `epoll`/`io_uring`.

---

### Task 1: Rewrite Runner Tests For `baseline -> refactored`

**Files:**
- Modify: `scripts/tests/test_run_benchmark_triplet.py`
- Modify: `scripts/tests/test_run_single_benchmark_triplet.py`
- Modify: `scripts/tests/test_parse_benchmark_triplet.py`

**Step 1: Write the failing tests**

Update the script tests so they expect:

- `--baseline-ref` instead of the legacy dual-ref CLI
- backend-local logs under `baseline` and `refactored`
- markdown/parser output shaped as `baseline | refactored`
- no public runner output still relying on the legacy three-state labels or delta field

**Step 2: Run the tests to verify they fail**

Run:

```bash
python3 -m unittest \
  scripts.tests.test_run_benchmark_triplet \
  scripts.tests.test_run_single_benchmark_triplet \
  scripts.tests.test_parse_benchmark_triplet
```

Expected: FAIL because the current runners and parser still expose the old three-state contract.

**Step 3: Checkpoint the changed tests**

Run:

```bash
git status --short \
  scripts/tests/test_run_benchmark_triplet.py \
  scripts/tests/test_run_single_benchmark_triplet.py \
  scripts/tests/test_parse_benchmark_triplet.py
```

Expected: only the intended test files are changed.

### Task 2: Implement The Two-State Runner And Parser Contract

**Files:**
- Modify: `scripts/run_benchmark_triplet.sh`
- Modify: `scripts/run_single_benchmark_triplet.sh`
- Modify: `scripts/parse_benchmark_triplet.py`

**Step 1: Implement the minimal runner migration**

Change the runners so that they:

- accept `--baseline-ref`
- build/run only `baseline` and `refactored`
- keep benchmark-local filtering and backend serialization unchanged
- retain baseline `io_uring` UDP quarantine behavior
- keep compat only where the baseline cannot be compared directly otherwise

**Step 2: Implement the minimal parser migration**

Change the parser so that it:

- recognizes only `baseline` and `refactored`
- renders markdown tables with only those two columns
- computes delta as `refactored` vs `baseline`
- removes the legacy historical-label output fields and obsolete delta field

**Step 3: Run the updated script tests**

Run:

```bash
python3 -m unittest \
  scripts.tests.test_run_benchmark_triplet \
  scripts.tests.test_run_single_benchmark_triplet \
  scripts.tests.test_parse_benchmark_triplet
```

Expected: PASS.

**Step 4: Run the broader script suite**

Run:

```bash
python3 -m unittest \
  scripts.tests.test_run_benchmark_matrix \
  scripts.tests.test_run_benchmark_triplet \
  scripts.tests.test_run_single_benchmark_triplet \
  scripts.tests.test_parse_benchmark_triplet
```

Expected: PASS with no regressions in the matrix runner.

### Task 3: Update Docs And Result Shapes To The New Baseline

**Files:**
- Modify: `docs/05-性能测试.md`
- Modify: `docs/plans/2026-03-15-single-benchmark-triplet-results.md`
- Modify: `README.md`

**Step 1: Update the docs to the new comparison contract**

Rewrite docs so they describe:

- `baseline=cde3da1`
- `refactored=current .worktrees/v3`
- `baseline -> refactored` comparison only
- `B5` as smoke only and `B6` as final UDP comparison

**Step 2: Remove stale three-state wording**

Delete or rewrite references to:

- legacy historical labels
- legacy delta wording
- any obsolete three-state benchmark instructions that are no longer authoritative

**Step 3: Verify the docs no longer expose the old label contract**

Run a stale-label grep across `README.md`, `docs/`, and `scripts/`.

Expected: no hits in active docs/scripts, or only intentionally preserved historical archive docs under older plan files.

### Task 4: Lock The Approved Runtime/Task API Surface

**Files:**
- Modify: `test/T57-runtime_task_api_surface.cc`
- Modify: `test/CMakeLists.txt`
- Modify: `galay-kernel/kernel/Runtime.h`
- Modify: `galay-kernel/kernel/Coroutine.h`
- Modify: `docs/02-API参考.md`
- Modify: `docs/03-使用指南.md`
- Modify: `docs/18-运行时Runtime.md`
- Modify: `examples/include/E4-coroutine_basic.cc`
- Modify: `examples/import/E4-coroutine_basic.cc`

**Step 1: Write the failing API surface checks**

Extend `T57-runtime_task_api_surface.cc` so it asserts:

- the approved keep-list remains usable:
  - `Runtime::blockOn`
  - `Runtime::spawn`
  - `Runtime::spawnBlocking`
  - `RuntimeHandle::current` / `tryCurrent`
  - `RuntimeHandle::spawn`
  - `RuntimeHandle::spawnBlocking`
  - `JoinHandle::join` / `wait`
  - `Coroutine::then`
- any explicitly identified dead transitional helpers are absent

**Step 2: Run the API surface test to verify it fails**

Run:

```bash
cmake --build build --target T57-runtime_task_api_surface -j4 && ./build/bin/T57-runtime_task_api_surface
```

Expected: FAIL if stale transitional surface is still exposed after tightening the keep-list assertions.

**Step 3: Implement the API cleanup**

Remove or hide only the audited transitional surface that is not in the keep-list, then update docs/examples so they use only the retained APIs.

**Step 4: Re-run the API surface test**

Run:

```bash
cmake --build build --target T57-runtime_task_api_surface T61-coroutine_then_compat -j4 && \
./build/bin/T57-runtime_task_api_surface && \
./build/bin/T61-coroutine_then_compat
```

Expected: PASS.

### Task 5: Delete Dead Benchmark/Compatibility Files After Reference Audit

**Files:**
- Audit: `benchmark/`
- Audit: `scripts/`
- Audit: `test/`
- Audit: `galay-kernel/module/galay.kernel.cppm`
- Audit: `galay-kernel/CMakeLists.txt`

**Step 1: Produce the reference audit**

Use `rg` to identify files that are now only referenced by superseded benchmark or compatibility flows.

Expected: a bounded list of remaining active references to audit.

**Step 2: Delete only files proven dead**

Delete the files that satisfy all of the following:

- no longer referenced by build/module/include/test/example paths
- exist only for superseded compatibility or reporting flows
- are not required by the approved runtime/task keep-list or the final benchmark workflow

**Step 3: Verify no deleted file is still referenced**

Run:

```bash
cmake --build build -j4
```

Expected: PASS with no missing-file or stale-reference errors.

### Task 6: Re-Run The Full Test Matrix After Pruning

**Files:**
- Modify as needed based on failures from Tasks 4-5

**Step 1: Run targeted runtime/scheduler regression tests**

Run:

```bash
cmake --build build --target \
  T1-coroutine_chain \
  T25-spawn_simple \
  T43-wait_continuation_scheduler_path \
  T44-scheduler_wakeup_coalescing \
  T45-scheduler_ready_budget \
  T46-scheduler_injected_burst_fastpath \
  T47-scheduler_queue_edge_wakeup \
  T48-scheduler_core_loop_stages \
  T49-channel_wait_registration \
  T52-runtime_block_on_result \
  T53-runtime_block_on_exception \
  T54-runtime_spawn_join_handle \
  T55-runtime_handle_current \
  T56-runtime_spawn_blocking \
  T57-runtime_task_api_surface \
  T61-coroutine_then_compat \
  T63-custom_sequence_awaitable \
  -j4
```

Then run:

```bash
./build/bin/T1-coroutine_chain && \
./build/bin/T25-spawn_simple && \
./build/bin/T43-wait_continuation_scheduler_path && \
./build/bin/T44-scheduler_wakeup_coalescing && \
./build/bin/T45-scheduler_ready_budget && \
./build/bin/T46-scheduler_injected_burst_fastpath && \
./build/bin/T47-scheduler_queue_edge_wakeup && \
./build/bin/T48-scheduler_core_loop_stages && \
./build/bin/T49-channel_wait_registration && \
./build/bin/T52-runtime_block_on_result && \
./build/bin/T53-runtime_block_on_exception && \
./build/bin/T54-runtime_spawn_join_handle && \
./build/bin/T55-runtime_handle_current && \
./build/bin/T56-runtime_spawn_blocking && \
./build/bin/T57-runtime_task_api_surface && \
./build/bin/T61-coroutine_then_compat && \
./build/bin/T63-custom_sequence_awaitable
```

Expected: PASS.

**Step 2: Run the full local test matrix**

Run the repository’s complete local test command after the targeted suite is green.

Expected: PASS with no new failures after pruning.

### Task 7: Run Fresh Serialized Benchmarks For All Targets

**Files:**
- Modify: `docs/plans/2026-03-15-single-benchmark-triplet-results.md`
- Modify: `docs/05-性能测试.md`

**Step 1: Run fresh smoke-first benchmarks in order**

Run one benchmark at a time, backend-serialized:

1. `B5-UdpClient`
2. `B6-Udp`
3. `B1-ComputeScheduler`
4. `B14-SchedulerInjectedWakeup`
5. `B8-MpscChannel`
6. `B9-UnsafeChannel`
7. `B10-Ringbuffer`
8. `B3-TcpClient`
9. `B7-FileIo`
10. `B12-TcpIovClient`
11. `B13-Sendfile`

**Step 2: Use only the new two-state runner interface**

Run the single-benchmark orchestrator with:

- `--baseline-ref cde3da1`
- current worktree as `refactored`
- one benchmark per run
- `kqueue -> epoll -> io_uring`

**Step 3: Re-run final accepted benchmarks with `repeat=3`**

After smoke passes confirm stability, rerun the accepted benchmarks with `repeat=3` for the final matrix.

**Step 4: Update the result docs after each benchmark**

Record:

- exact output roots
- `baseline` vs `refactored` numbers
- accepted / rejected / smoke-only verdict
- whether baseline paths were explicitly skipped/unsupported

### Task 8: Final Verification Before Claiming Completion

**Files:**
- Modify any touched files required by verification fixes

**Step 1: Re-run script tests fresh**

Run:

```bash
python3 -m unittest \
  scripts.tests.test_run_benchmark_matrix \
  scripts.tests.test_run_benchmark_triplet \
  scripts.tests.test_run_single_benchmark_triplet \
  scripts.tests.test_parse_benchmark_triplet
```

Expected: PASS.

**Step 2: Re-run the final local test command fresh**

Run the complete local test suite again after the final benchmark/doc changes.

Expected: PASS.

**Step 3: Re-check benchmark crash signatures**

Run:

```bash
find build -name "*.log" -print0 | xargs -0 grep -HnE "Segmentation fault|dumped core|benchmark-timeout" || true
```

Expected: no unexpected crash progression in accepted final result roots.

**Step 4: Re-check worktree diff**

Run:

```bash
git status --short
```

Expected: only intentional benchmark/doc/API cleanup changes remain.
