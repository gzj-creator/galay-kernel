# Baseline Refactored Benchmark And API Prune Design

**Context**

The benchmark workflow has already been hardened to avoid crash-driven progress, but the comparison target has changed.

The user no longer wants the legacy three-state matrix or the older tag-based comparison.
The required baseline is now:

- `baseline`: the latest committed state before the current refactor, i.e. commit `cde3da1`
- `refactored`: the current `.worktrees/v3` working tree

This changes both the runner contract and the success criteria:

- every benchmark comparison must now be `baseline -> refactored`
- backend order remains `kqueue -> epoll -> io_uring`
- each benchmark must still run in isolation
- stale transitional script/report interfaces must be removed, not merely ignored
- unusable or dead transitional public interfaces and source files must be deleted rather than carried forward

**Goals**

- convert the benchmark pipeline to a strict two-state comparison: `baseline` vs `refactored`
- preserve the single-benchmark, backend-serialized execution discipline
- keep historical crash quarantines where they are still needed for the baseline
- remove obsolete benchmark labels, parser fields, and report shapes tied to the legacy three-state flow
- audit and delete dead or unusable transitional interfaces/files from the refactored codebase

**Non-Goals**

- keeping legacy historical labels alive in public runner interfaces
- preserving compatibility shims that only exist to support superseded benchmark/report workflows
- deleting still-supported public APIs merely because they are old; deletion must be justified by non-use or explicit replacement

## Chosen Approach

The new workflow is a two-layer runner over two code states:

- keep the low-level benchmark matrix runner
- keep the single-benchmark three-backend runner entry point
- change the compared labels from the legacy three-state contract to `baseline / refactored`
- keep compat builds only when the current benchmark source must be compiled against the baseline because the baseline does not contain the benchmark source directly

This preserves the hardened execution discipline while removing the now-unwanted middle comparison state.

## Runner Semantics

### 1. Baseline definition

`baseline` is fixed to commit `cde3da1`, which is the latest committed repository state before the current refactor in `.worktrees/v3`.

`refactored` is the current `.worktrees/v3` worktree.

No runner output, parser field, or report table should expose legacy historical labels as active comparison labels.

### 2. Benchmark-local execution

One benchmark is executed at a time.

Standalone benchmarks:

- `B1-ComputeScheduler`
- `B6-Udp`
- `B7-FileIo`
- `B8-MpscChannel`
- `B9-UnsafeChannel`
- `B10-Ringbuffer`
- `B13-Sendfile`
- `B14-SchedulerInjectedWakeup`

Paired benchmark groups:

- `B2-TcpServer + B3-TcpClient`
- `B4-UdpServer + B5-UdpClient`
- `B11-TcpIovServer + B12-TcpIovClient`

Paired benchmarks remain inseparable at execution time.

### 3. Backend order

For each selected benchmark:

1. `kqueue`
2. `epoll`
3. `io_uring`

Within each backend:

1. `baseline`
2. `refactored`

No backend parallelism. No cross-benchmark parallelism.

### 4. Crash policy

The benchmark pipeline must still classify known baseline-only invalid paths before execution.

Current concrete rule:

- baseline `io_uring` UDP timeout/lifetime paths are not executable validation targets
- baseline `B4/B5` on `io_uring` must be explicitly skipped
- baseline compat `B6-Udp` on `io_uring` must be explicitly unsupported
- refactored `io_uring` UDP paths must execute normally and remain crash-free

This quarantine remains valid even after the label migration.

### 5. Output shape

Per benchmark output root:

- `kqueue/...`
- `epoll/...`
- `io_uring/...`
- each backend contains `baseline/...` and `refactored/...`
- a merged per-benchmark summary compares only `baseline` vs `refactored`

The only sign-off question is:

"For benchmark X, across all three backends, how did `baseline` compare with `refactored`?"

## API And File Pruning Policy

This round must remove transitional surfaces instead of preserving them indefinitely.

### 1. Public API keep-list

The runtime/task public surface that remains supported is:

- `Runtime::blockOn(Task<T>)`
- `Runtime::spawn(Task<T>)`
- `Runtime::spawnBlocking(...)`
- `Runtime::handle()`
- `RuntimeHandle::current()` / `tryCurrent()`
- `RuntimeHandle::spawn(...)`
- `RuntimeHandle::spawnBlocking(...)`
- `JoinHandle::join()` / `wait()`
- `Coroutine::then(...)`

Anything outside this keep-list that is only a transitional convenience, dead compatibility path, or unused wrapper should be audited for deletion.

### 2. Deletion criteria

A public or build-visible surface should be removed when all of the following are true:

- it is not in the approved keep-list
- it has no required test/example/documented use, or it only exists for superseded compatibility
- there is a clear replacement already used by tests/examples/docs

A source file should be deleted when all of the following are true:

- it is no longer referenced by the build, module export surface, includes, tests, or examples
- it exists only to support superseded benchmark/report/API compatibility
- removing it does not reduce the supported public surface above

### 3. High-risk audit areas

The first audit targets are:

- benchmark/report labels and CLI flags in `scripts/`
- parser keys and markdown table shapes in `scripts/parse_benchmark_triplet.py`
- benchmark/docs references to legacy historical labels, obsolete delta wording, and historical triplet semantics
- compile-time runtime API surface checks in `test/T57-runtime_task_api_surface.cc`
- old compatibility-only source/test artifacts that still reference superseded benchmark or API paths

## Benchmark Order

Execution order remains:

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

`B5` remains a stability/smoke benchmark only.
Final UDP performance sign-off continues to use `B6`.

## Verification Requirements

Kernel-side:

- script unit tests pass after the label migration
- runtime/task API surface tests pass after pruning
- full test matrix passes after any API/file deletions
- fresh benchmark logs show zero `Segmentation fault`, zero `dumped core`, and no unexpected hang progression

Benchmark-side:

- all final conclusions come from fresh `baseline -> refactored` runs only
- no result table references the old three-state comparison

## Recommendation

Implement the migration in this order:

1. rewrite the runner/parser/report contract to `baseline -> refactored`
2. update tests first and make them green
3. audit and remove obsolete transitional interfaces/files
4. rerun full tests
5. rerun the full serialized benchmark matrix and publish the final two-state comparison
