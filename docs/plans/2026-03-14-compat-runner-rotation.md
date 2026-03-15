# Compat Runner Rotation Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Eliminate the remaining `B1-ComputeScheduler` benchmark fails by making compat benchmark execution order symmetric across `pre-v3`, `v3`, and `refactored`, without regressing the already-fixed `B6`, `B8`, and `B14` results.

**Architecture:** Keep the existing matrix path for non-compat benchmarks unchanged. Refactor only the compat benchmark orchestration in `scripts/run_benchmark_triplet.sh` so compat benchmarks execute benchmark-by-benchmark, and each run rotates label order to cancel systematic first/last bias. Preserve existing log layout so `scripts/parse_benchmark_triplet.py` and docs do not need structural changes.

**Tech Stack:** Bash orchestration, Python unittest script coverage, existing triplet parser/report pipeline

---

### Task 1: Lock the new runner ordering contract with a failing test

**Files:**
- Modify: `scripts/tests/test_run_benchmark_triplet.py`
- Reuse: `scripts/run_benchmark_triplet.sh`

**Step 1: Write the failing test**

Extend the dry-run test so it asserts:

- compat execution is emitted for `refactored`, `v3`, and `pre-v3`
- compat execution is grouped by benchmark instead of by label
- the emitted dry-run command order for `B1-ComputeScheduler` rotates labels across runs

**Step 2: Run test to verify it fails**

Run:

```bash
python3 -m unittest scripts.tests.test_run_benchmark_triplet -v
```

Expected: FAIL because the current script still emits compat commands label-by-label.

**Step 3: Write minimal implementation expectations**

Keep the assertions focused on observable dry-run ordering only. Do not assert internal helper names.

**Step 4: Run test to verify it still fails for the right reason**

Run:

```bash
python3 -m unittest scripts.tests.test_run_benchmark_triplet -v
```

Expected: FAIL with missing rotated compat command order.

### Task 2: Refactor compat orchestration to rotate by benchmark and run

**Files:**
- Modify: `scripts/run_benchmark_triplet.sh`

**Step 1: Implement the smallest orchestration refactor**

Change compat execution from:

- all compat benchmarks for `refactored`
- all compat benchmarks for `v3`
- all compat benchmarks for `pre-v3`

to:

- for each compat benchmark
- for each run index
- execute labels in a rotated order such as:
  - run 1: `refactored -> v3 -> pre-v3`
  - run 2: `v3 -> pre-v3 -> refactored`
  - run 3: `pre-v3 -> refactored -> v3`

Keep:

- current output paths: `OUTPUT_ROOT/<label>/run-N/<Benchmark>.log`
- current unsupported handling for compat build/run failures
- current matrix execution for non-compat benchmarks

**Step 2: Run the dry-run test**

Run:

```bash
python3 -m unittest scripts.tests.test_run_benchmark_triplet -v
```

Expected: PASS.

**Step 3: Run parser regression tests**

Run:

```bash
python3 -m unittest scripts.tests.test_parse_benchmark_triplet -v
```

Expected: PASS because log paths and output shape stay unchanged.

### Task 3: Verify focused compat behavior before another full triplet

**Files:**
- Reuse: `benchmark/B1-compute_scheduler.cc`
- Reuse: `scripts/run_benchmark_triplet.sh`

**Step 1: Run focused local smoke checks**

Run local compat-smoke comparisons for `B1-ComputeScheduler` using the current benchmark source against:

- current refactored build
- `v3` baseline build

Use alternating label order in the smoke script to confirm the new orchestration principle is directionally better than label-batched execution.

**Step 2: Confirm no regression on already-fixed benchmarks**

Re-check the most sensitive metrics locally:

- `B6-Udp` packet loss
- `B14-SchedulerInjectedWakeup` injected latency

Expected: they should remain at or better than the `final5` quality bar.

### Task 4: Re-run the full triplet and compare against `final5`

**Files:**
- Reuse: `scripts/run_benchmark_triplet.sh`
- Reuse: `scripts/parse_benchmark_triplet.py`

**Step 1: Run a fresh full triplet**

Run:

```bash
bash scripts/run_benchmark_triplet.sh \
  --pre-v3-ref 09c8917 \
  --v3-ref cde3da1 \
  --refactored-path /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3 \
  --output-root /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14-final7 \
  --repeat 5
```

Expected: fresh logs for all labels with compat benchmarks emitted in rotated order.

**Step 2: Parse the report**

Run:

```bash
python3 scripts/parse_benchmark_triplet.py \
  --input-root /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14-final7 \
  --json-out /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14-final7/report.json \
  --markdown-out /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14-final7/report.md
```

Expected: ideally exit `0`; at minimum, fail set should be strictly no worse than `final5`.

**Step 3: Compare with `final5`**

Verify:

- `B6`, `B8`, and `B14` remain PASS
- `B1` fail count is reduced or eliminated
- no new primary/core regressions appear

### Task 5: Update docs only after a true pass

**Files:**
- Modify: `docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-results.md`
- Modify: `docs/05-性能测试.md`

**Step 1: Gate on fresh passing evidence**

Only proceed if the fresh parser run exits `0` and `overall_pass = true`.

**Step 2: Sync the final matrix**

Copy the final `pre-v3 -> v3 -> refactored` comparison into both docs and summarize:

- runner fairness changes
- benchmark stabilization changes
- final three-group comparison highlights

**Step 3: Re-open docs and cross-check**

Verify the values in docs exactly match the final `report.json`.
