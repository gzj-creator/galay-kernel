# Full Benchmark Rerun And Doc Sync Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Re-run the full benchmark triplet for `pre-v3`, `v3`, and the current refactored worktree, then update the benchmark documentation to reflect the new optimization results.

**Architecture:** Reuse the existing triplet orchestration and parsing scripts so the comparison method stays identical to the March 13, 2026 report. Treat the new run as the source of truth, then update the results and user-facing benchmark docs with both the raw matrix and a concise optimization/effect summary.

**Tech Stack:** Bash benchmark orchestration, Python parser, existing benchmark binaries and docs

---

### Task 1: Execute a fresh full benchmark triplet

**Files:**
- Reuse: `scripts/run_benchmark_triplet.sh`

**Step 1: Run the full triplet**

Run:

```bash
bash scripts/run_benchmark_triplet.sh \
  --pre-v3-ref 09c8917 \
  --v3-ref cde3da1 \
  --refactored-path /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3 \
  --output-root /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14 \
  --repeat 3
```

Expected: all three source trees build and all benchmark logs are emitted under the new output root.

### Task 2: Parse and inspect the new report

**Files:**
- Reuse: `scripts/parse_benchmark_triplet.py`

**Step 1: Generate JSON + Markdown artifacts**

Run:

```bash
python3 scripts/parse_benchmark_triplet.py \
  --input-root /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14 \
  --json-out /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14/report.json \
  --markdown-out /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14/report.md
```

Expected: parsed report artifacts exist even if the overall gate still fails.

### Task 3: Sync documentation

**Files:**
- Modify: `docs/plans/2026-03-13-runtime-task-benchmark-and-api-polish-results.md`
- Modify: `docs/05-性能测试.md`

**Step 1: Update the benchmark results doc**

Replace the old March 13, 2026 final triplet matrix and verdict with the new March 14, 2026 rerun, while preserving enough context to explain which optimizations landed since the earlier failed run.

**Step 2: Update the performance guide**

Refresh the “how to run” / “current status” section so it points at the new output root and the latest parsed report.

**Step 3: Verify docs reflect the new run**

Re-open the updated docs and cross-check the key numbers against `report.json`.

## Verification Notes

- Full triplet run:

```bash
bash scripts/run_benchmark_triplet.sh \
  --pre-v3-ref 09c8917 \
  --v3-ref cde3da1 \
  --refactored-path /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3 \
  --output-root /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14 \
  --repeat 3
```

- Parse run:

```bash
python3 scripts/parse_benchmark_triplet.py \
  --input-root /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14 \
  --json-out /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14/report.json \
  --markdown-out /Users/gongzhijie/Desktop/projects/git/galay-kernel/.worktrees/v3/build/benchmark-triplet-2026-03-14/report.md
```

- Note: parser exited with status `1`, which is expected here because the rerun still has benchmark gate failures.
- Doc cross-check completed against `report.json` for:
  - `B8-MpscChannel` latency / single / multi
  - `B6-Udp` packet loss
  - `B14-SchedulerInjectedWakeup` injected latency
  - `B10-Ringbuffer` write throughput
