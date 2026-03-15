# Single Benchmark Triplet And v3.0.1 Rollout Design

**Context**

The current triplet tooling can produce useful aggregate reports, but it is too coarse for the next phase:

- performance comparisons must be done one benchmark at a time
- each benchmark must be compared in a fixed order across `kqueue`, `epoll`, and `io_uring`
- crash-prone historical `io_uring` UDP paths must not be allowed to progress via `Segmentation fault`, `Killed`, or timeout-core-dump behavior
- downstream `galay-*` repositories must not start adapting until `galay-kernel` has both stable behavior and acceptable benchmark evidence

The user-approved execution model is:

1. pick exactly one benchmark
2. run `pre-v3 -> v3 -> refactored` for one backend
3. repeat the same benchmark for the next backend
4. repeat for the third backend
5. compare only that benchmark across all three backends
6. move on to the next benchmark only after the current one is stable

**Goals**

- make every benchmark comparison backend-ordered and benchmark-local
- eliminate all benchmark-driven crash paths from the validation workflow
- keep historical incompatibilities explicit and non-fatal
- produce per-benchmark three-backend before/after evidence that is resilient to cross-run system jitter
- start downstream `v3.0.1` adoption only after `galay-kernel` passes the stability gate

**Non-Goals**

- rewriting historical `pre-v3` or `v3` runtime implementations to behave like the refactored runtime
- treating known historical incompatibilities as refactored regressions
- parallelizing backend benchmark runs for speed at the expense of signal quality

## Chosen Approach

The recommended design is a two-layer runner:

- keep `scripts/run_benchmark_matrix.sh` as the low-level benchmark executor
- keep `scripts/run_benchmark_triplet.sh` as the low-level `pre-v3 / v3 / refactored` orchestrator
- add benchmark-local filtering so the same scripts can execute exactly one benchmark (or one paired benchmark group) at a time
- add a thin single-benchmark orchestration layer that serializes the three backends and emits one combined comparison result per benchmark

This keeps existing scripts reusable while adding the missing control surface required for stable performance validation.

## Runner Semantics

### 1. Benchmark-local filtering

The matrix runner must support running exactly one benchmark target at a time.

For standalone benchmarks, this is direct:

- `B1-ComputeScheduler`
- `B6-Udp`
- `B7-FileIo`
- `B8-MpscChannel`
- `B9-UnsafeChannel`
- `B10-Ringbuffer`
- `B13-Sendfile`
- `B14-SchedulerInjectedWakeup`

For paired benchmarks, the runner must understand the benchmark group as inseparable:

- `B2-TcpServer + B3-TcpClient`
- `B4-UdpServer + B5-UdpClient`
- `B11-TcpIovServer + B12-TcpIovClient`

If one side of a paired benchmark is filtered out, the runner must explicitly mark both logs as skipped and never start the server.

### 2. Backend order

For every selected benchmark, execution order is fixed:

1. `kqueue`
2. `epoll`
3. `io_uring`

Within each backend, label order remains:

1. `pre-v3`
2. `v3`
3. `refactored`

No backend runs in parallel with another backend. No benchmark runs in parallel with another benchmark.

### 3. Crash policy

The validation pipeline is not allowed to "learn" from crashes. Any path that is already known to be historically incompatible must be classified before execution.

Current concrete rule:

- historical `io_uring` UDP timeout/lifetime paths for `pre-v3` and `v3` are not executable validation targets
- the runner must mark those paths as `skipped` or `unsupported` before launching them
- the refactored `io_uring` UDP path still runs normally and must remain crash-free

This rule currently applies to:

- main matrix `B4-UdpServer + B5-UdpClient` on `io_uring` for historical labels
- compat `B6-Udp` on `io_uring` for historical labels

This can be extended if more historical crash-only paths are identified later.

### 4. Output shape

For each benchmark, the new runner should create one result root containing:

- `kqueue/...`
- `epoll/...`
- `io_uring/...`
- a per-benchmark merged markdown summary
- raw logs for each backend and label

The combined result should answer a narrow question:

"For benchmark X, across all three backends, how did `pre-v3`, `v3`, and `refactored` behave?"

That is the only comparison unit that should be used for performance sign-off.

## Benchmark Order

Execution should be crash-first, then performance-sensitive:

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

For paired benchmarks, the named client-side benchmark remains the reporting target, but the paired server is launched as part of execution.

## Stability Gate Before Downstream Rollout

No downstream repository work starts until all of the following are true:

- current `galay-kernel` test matrix passes
- the single-benchmark sequential comparison workflow is in place
- known historical crash paths no longer progress via actual crashes
- the benchmark under review has acceptable per-backend evidence

Only after that gate is met do we move to `v3.0.1` downstream adaptation.

## Downstream Rollout Strategy

The downstream repositories in scope are:

- `galay-utils`
- `galay-ssl`
- `galay-http`
- `galay-rpc`
- `galay-mcp`
- `galay-redis`
- `galay-mysql`
- `galay-mongo`
- `galay-etcd`

Rollout order should follow dependency depth:

1. `galay-utils`
2. `galay-ssl`
3. `galay-http`
4. `galay-rpc`
5. `galay-mcp`
6. `galay-redis`
7. `galay-mysql`
8. `galay-mongo`
9. `galay-etcd`

For each repository:

- create an isolated `v3.0.1` worktree or branch
- update version metadata and kernel dependency expectations
- build against the verified `galay-kernel`
- run that repository's tests and smoke examples
- fix compile/runtime incompatibilities before moving to the next repo

This keeps breakage localized and preserves a clean dependency ramp.

## Verification Requirements

Kernel-side verification:

- script unit tests must pass
- targeted crash smoke runs must show zero `Segmentation fault`, zero `Killed`, zero `dumped core`
- single-benchmark reports must be regenerated from fresh runs only

Downstream verification:

- configure succeeds with the new kernel
- build succeeds
- tests pass or unsupported paths are explicitly documented
- examples/smoke targets launch cleanly

## Recommendation

Implement the runner hardening first, then convert validation to benchmark-local three-backend runs, then start the downstream `v3.0.1` wave only after the kernel stability gate is met.
