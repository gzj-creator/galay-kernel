# Eliminate Benchmark Fails Design

## Background

The latest full benchmark triplet for `pre-v3`, `v3`, and the current refactored worktree still reports `FAIL`, but the remaining failures are concentrated in three benchmarks:

- `B1-ComputeScheduler`
- `B10-Ringbuffer`
- `B14-SchedulerInjectedWakeup`

The evidence collected from the March 14, 2026 rerun shows these failures are not all rooted in runtime regressions:

- `B10-Ringbuffer` still fails on write throughput even though the `RingBuffer` implementation is effectively unchanged from `v3`.
- `B14-SchedulerInjectedWakeup` shows large latency variance between runs of the same binary.
- `B1-ComputeScheduler` still relies on `sleep_for(1ms)` polling and millisecond-duration denominators, which amplifies sampling noise.

This makes benchmark stability the first problem to solve. The goal of this design is to remove the remaining benchmark `FAIL`s without weakening the gate semantics and without over-tuning the runtime for noisy measurements.

## Goals

- Eliminate the remaining benchmark `FAIL`s in the triplet report.
- Keep triplet comparisons fair across `pre-v3`, `v3`, and `refactored`.
- Preserve existing gate semantics in `scripts/parse_benchmark_triplet.py`.
- Avoid introducing new public runtime APIs or extra maintenance-only interfaces.
- Update benchmark docs so the published matrix matches the final rerun.

## Non-Goals

- No broad runtime hot-path rewrite in `ComputeScheduler`, `KqueueScheduler`, or `RingBuffer`.
- No loosening of pass/fail thresholds in the parser.
- No renaming of benchmark metrics already consumed by the parser and docs.

## Approaches Considered

### 1. Parser-Only Relaxation

Change parser thresholds or ignore more outliers so the current data passes.

Why rejected:

- It hides measurement instability instead of fixing it.
- It would weaken the benchmark gate semantics the user explicitly wants preserved.

### 2. Compat Benchmark Refresh

Use the current benchmark harness for all three compared code states, then stabilize the harness where needed.

Why selected:

- It keeps the comparison fair by using one measurement method across all three groups.
- It directly addresses the observed noise in `B1`, `B10`, and `B14`.
- It avoids unnecessary runtime code churn where no implementation regression is proven.

### 3. Runtime Code-First Tuning

Change the runtime implementations first and only remeasure afterward.

Why rejected:

- Current evidence does not justify a three-subsystem runtime tuning pass.
- High risk of overfitting runtime behavior to unstable benchmarks.

## Architecture

### Triplet Runner

`scripts/run_benchmark_triplet.sh` currently has a compat path only for `B14`. This design extends the same idea to `B1` and `B10` so that:

- `pre-v3` runs the current benchmark sources linked against the `pre-v3` library.
- `v3` runs the current benchmark sources linked against the `v3` library.
- `refactored` continues to run the benchmark binaries built in the current worktree.

This produces a fair three-way comparison with one benchmark harness per metric family instead of mixing old and new harness behavior.

### Benchmark Harness Changes

The benchmark sources remain responsible only for measurement quality. They do not change metric names or parser-visible semantics.

#### `B1-ComputeScheduler`

- Replace `sleep_for(1ms)` completion polling with a benchmark-local completion latch.
- Use higher precision elapsed-time calculation (`steady_clock` with microsecond or nanosecond precision).
- Keep the warmup and measured phases separated by an explicit completion barrier.
- Preserve the existing metric names:
  - `empty throughput`
  - `light throughput`
  - `heavy throughput`
  - `latency`

#### `B10-Ringbuffer`

- Keep the `RingBuffer` implementation unchanged unless the stabilized harness still shows a real regression.
- Change write-throughput measurement from a fixed `1GB` sample to a time-stabilized sample with a minimum duration.
- Preserve the existing metric name `write throughput`.
- Keep multi-chunk reporting so the parser can continue taking the best throughput sample.

#### `B14-SchedulerInjectedWakeup`

- Run throughput and latency on separate scheduler lifecycles.
- Add a short warmup before latency sampling.
- Preserve metric names:
  - `injected throughput`
  - `injected latency`

This isolates the latency measurement from leftover injected queue and wake state after the throughput phase.

## Testing Strategy

### Runner / Parser

- Add script tests to prove `B1`, `B10`, and `B14` all use the compat path for historical baselines.
- Add parser fixture coverage if any benchmark log text changes in a parser-visible way.

### Benchmark Helpers

- Add a focused test for the new benchmark synchronization helper so completion and timeout behavior are explicit.
- Avoid embedding another ad-hoc polling loop into benchmark code.

### Focused Verification

After implementation:

1. Run affected script tests.
2. Run focused benchmark binaries for `B1`, `B10`, and `B14`.
3. Re-run the full triplet.
4. Regenerate the parsed report.
5. Sync benchmark docs with the final matrix.

## Success Criteria

- `scripts/parse_benchmark_triplet.py` reports `overall_pass = true` on the fresh triplet rerun.
- No parser threshold loosening is introduced.
- No new public runtime-facing API is added just to support the benchmarks.
- `docs/05-性能测试.md` and the result summary reflect the final triplet matrix.
