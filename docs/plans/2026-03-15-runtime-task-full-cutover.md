# Runtime/Task Full Cutover Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the repository-wide `Coroutine + Scheduler` programming model with the `Runtime + Task + JoinHandle` model, remove the old scheduler/coroutine source files entirely, and revalidate the project before re-tagging `v3.0.1`.

**Architecture:** First isolate the task core from the old `Coroutine` wrapper, then refactor `Runtime` to own internal workers instead of public scheduler types, then migrate awaitables and async resources onto runtime-managed worker context. After the runtime core is stable, rewrite all examples, tests, benchmarks, and docs to the new model, physically delete the old sources, and rerun the full verification and release flow.

**Tech Stack:** C++23, C++20 coroutines, kqueue, epoll, io_uring, Bash benchmark scripts, Python unittest, CMake, GitHub CLI

---

### Task 1: Establish the exact old-interface removal surface

**Files:**
- Inspect: `galay-kernel/kernel/Coroutine.h`
- Inspect: `galay-kernel/kernel/Runtime.h`
- Inspect: `galay-kernel/kernel/Runtime.cc`
- Inspect: `galay-kernel/kernel/Scheduler.hpp`
- Inspect: `galay-kernel/kernel/IOScheduler.hpp`
- Inspect: `galay-kernel/kernel/ComputeScheduler.h`
- Inspect: `galay-kernel/kernel/EpollScheduler.h`
- Inspect: `galay-kernel/kernel/KqueueScheduler.h`
- Inspect: `galay-kernel/kernel/IOUringScheduler.h`

**Step 1: Write the failing inventory check**

Run:

```bash
rg -n "\bCoroutine\b|ComputeScheduler|EpollScheduler|KqueueScheduler|IOUringScheduler|IOScheduler|SpawnAwaitable|WaitResult" galay-kernel examples test benchmark README.md docs
```

Expected: many hits; this confirms the full migration surface.

**Step 2: Save the deletion checklist**

Record the exact files and public APIs that must be removed before release.

### Task 2: Extract the task core out of `Coroutine`

**Files:**
- Create: `galay-kernel/kernel/Task.h`
- Create: `galay-kernel/kernel/Task.cc`
- Modify: `galay-kernel/kernel/Runtime.h`
- Modify: `galay-kernel/kernel/Runtime.cc`
- Modify: `galay-kernel/kernel/Waker.h`
- Modify: `galay-kernel/kernel/Waker.cc`
- Test: `test/`

**Step 1: Write a focused failing runtime API test**

Pick or add a runtime-facing test that only uses:

- `Task<T>`
- `Runtime::blockOn(...)`
- `Runtime::spawn(...)`
- `JoinHandle<T>::wait()/join()`
- `RuntimeHandle::current()/tryCurrent()`

Expected: it should fail if `Task` cannot build independently from `Coroutine`.

**Step 2: Implement `Task` core extraction**

Move or split the retained task primitives out of `Coroutine.h`:

- `TaskRef`
- `TaskState`
- `TaskCompletionState<T>`
- `TaskPromise<T>`
- `Task<T>`
- `JoinHandle<T>`
- runtime TLS helpers

**Step 3: Repoint `Runtime` at `Task`**

Update `Runtime.h` / `Runtime.cc` so it no longer includes or names `Coroutine`.

**Step 4: Run runtime-facing tests**

Run:

```bash
cmake --build build --target T27-runtime_stress T42-runtime_strict_scheduler_counts --parallel
./build/bin/T27-runtime_stress
./build/bin/T42-runtime_strict_scheduler_counts
```

Expected: both pass with the extracted task core.

### Task 3: Replace public scheduler ownership with internal workers

**Files:**
- Create: `galay-kernel/kernel/IoWorker.h`
- Create: `galay-kernel/kernel/IoWorker.cc`
- Create: `galay-kernel/kernel/ComputeWorker.h`
- Create: `galay-kernel/kernel/ComputeWorker.cc`
- Modify: `galay-kernel/kernel/Runtime.h`
- Modify: `galay-kernel/kernel/Runtime.cc`
- Modify: `galay-kernel/kernel/BackendReactor.h`
- Reuse: `galay-kernel/kernel/SchedulerCore.h`
- Reuse: `galay-kernel/kernel/WakeCoordinator.h`

**Step 1: Write failing compile-time coverage for removed public methods**

Identify and remove these public APIs from `Runtime`:

- `addIOScheduler(...)`
- `addComputeScheduler(...)`
- `getIOScheduler(...)`
- `getComputeScheduler(...)`
- `getNextIOScheduler()`
- `getNextComputeScheduler()`

Expected: docs/examples/tests that still call them should fail until migrated.

**Step 2: Implement internal worker ownership**

Make `Runtime` own internal workers and backend selection directly, without exposing scheduler pointers.

**Step 3: Preserve high-level runtime semantics**

Ensure:

- `blockOn(...)` still auto-starts when needed
- `spawn(...)` still returns `JoinHandle<T>`
- `spawnBlocking(...)` still works

**Step 4: Run runtime API tests again**

Run:

```bash
cmake --build build --target T27-runtime_stress T42-runtime_strict_scheduler_counts --parallel
./build/bin/T27-runtime_stress
./build/bin/T42-runtime_strict_scheduler_counts
```

Expected: pass after worker migration.

### Task 4: Rewire awaitables and async resources to runtime worker context

**Files:**
- Modify: `galay-kernel/kernel/Awaitable.h`
- Modify: `galay-kernel/kernel/Awaitable.inl`
- Modify: `galay-kernel/kernel/Awaitable.cc`
- Modify: `galay-kernel/kernel/IOController.hpp`
- Modify: `galay-kernel/async/TcpSocket.h`
- Modify: `galay-kernel/async/TcpSocket.cc`
- Modify: `galay-kernel/async/UdpSocket.h`
- Modify: `galay-kernel/async/UdpSocket.cc`
- Modify: `galay-kernel/async/AsyncFile.*`
- Modify: `galay-kernel/async/AioFile.*`
- Modify: `galay-kernel/async/FileWatcher.*`

**Step 1: Write a failing async smoke target**

Use existing smoke targets that exercise:

- TCP accept/connect/send/recv
- UDP sendto/recvfrom
- file read/write or file watcher

Expected: they fail until awaitables no longer require old scheduler types.

**Step 2: Replace scheduler-dependent suspend logic**

Make awaitable suspension use the current task’s runtime/worker context instead of public `IOScheduler`.

**Step 3: Update async public headers**

Remove old scheduler-centric examples and commentary from socket/file headers.

**Step 4: Run focused async tests**

Run:

```bash
cmake --build build --target T9-file_watcher T19-readv_writev T23-sendfile_basic --parallel
./build/bin/T9-file_watcher
./build/bin/T19-readv_writev
./build/bin/T23-sendfile_basic
```

Expected: pass on the new runtime-managed path.

### Task 5: Migrate all examples to the new runtime/task entry model

**Files:**
- Modify: `examples/include/E1-sendfile_example.cc`
- Modify: `examples/include/E2-tcp_echo_server.cc`
- Modify: `examples/include/E3-tcp_client.cc`
- Modify: `examples/include/E4-coroutine_basic.cc`
- Modify: `examples/include/E5-udp_echo.cc`
- Modify: `examples/import/E1-sendfile_example.cc`
- Modify: `examples/import/E2-tcp_echo_server.cc`
- Modify: `examples/import/E3-tcp_client.cc`
- Modify: `examples/import/E4-coroutine_basic.cc`
- Modify: `examples/import/E5-udp_echo.cc`
- Modify: `examples/import/E6-mpsc_channel.cc`
- Modify: `examples/import/E7-unsafe_channel.cc`
- Modify: `examples/import/E8-async_sync.cc`
- Modify: `examples/import/E9-timer_sleep.cc`
- Modify: `docs/04-示例代码.md`
- Modify: `docs/03-使用指南.md`

**Step 1: Write the failing example audit**

Run:

```bash
rg -n "\bCoroutine\b|scheduler\.start\(|scheduler\.spawn\(|getNextIOScheduler\(|getNextComputeScheduler\(" examples docs/03-使用指南.md docs/04-示例代码.md
```

Expected: many hits before migration.

**Step 2: Convert include examples**

Use:

- root `Task<T>` or `Task<void>`
- `runtime.blockOn(...)`
- `runtime.spawn(...)`
- `RuntimeHandle::current()` for nested task fan-out

**Step 3: Convert import examples**

Apply the same high-level pattern to every import example.

**Step 4: Run example targets**

Run:

```bash
cmake --build build --target \
  E1-SendfileExample E2-TcpEchoServer E3-TcpClient E4-CoroutineBasic E5-UdpEcho \
  --parallel
./build/bin/E1-SendfileExample
./build/bin/E2-TcpEchoServer
./build/bin/E3-TcpClient
./build/bin/E4-CoroutineBasic
./build/bin/E5-UdpEcho
```

Expected: all include examples pass on new APIs.

### Task 6: Migrate tests and benchmarks off old interfaces

**Files:**
- Modify: `test/`
- Modify: `benchmark/`
- Modify: `scripts/` only if benchmark/test target names or semantics require it

**Step 1: Write the failing repository-wide old-interface scan**

Run:

```bash
rg -n "\bCoroutine\b|ComputeScheduler|EpollScheduler|KqueueScheduler|IOUringScheduler|IOScheduler|SpawnAwaitable|WaitResult" test benchmark
```

Expected: many hits before migration.

**Step 2: Convert tests**

Rewrite direct scheduler/coroutine tests into runtime/task semantics while preserving behavioral coverage.

**Step 3: Convert benchmarks**

Keep benchmark target names stable if possible, but migrate benchmark implementation to the runtime/task model.

**Step 4: Run full test and benchmark orchestrators**

Run:

```bash
bash scripts/run_test_matrix.sh "$PWD/build" "$PWD/build/test_matrix_logs_post_cutover"
bash scripts/run_benchmark_triplet.sh \
  --baseline-ref cde3da1 \
  --refactored-path "$PWD" \
  --repeat 3 \
  --output-root "$PWD/build/benchmark-triplet-post-cutover"
```

Expected: test matrix completes; benchmark triplet completes and yields parsable results.

### Task 7: Delete the old scheduler/coroutine sources and clean exports

**Files:**
- Delete: `galay-kernel/kernel/Coroutine.h`
- Delete: `galay-kernel/kernel/Coroutine.cc`
- Delete: `galay-kernel/kernel/Scheduler.hpp`
- Delete: `galay-kernel/kernel/Scheduler.cc`
- Delete: `galay-kernel/kernel/IOScheduler.hpp`
- Delete: `galay-kernel/kernel/ComputeScheduler.h`
- Delete: `galay-kernel/kernel/ComputeScheduler.cc`
- Delete: `galay-kernel/kernel/EpollScheduler.h`
- Delete: `galay-kernel/kernel/EpollScheduler.cc`
- Delete: `galay-kernel/kernel/KqueueScheduler.h`
- Delete: `galay-kernel/kernel/KqueueScheduler.cc`
- Delete: `galay-kernel/kernel/IOUringScheduler.h`
- Delete: `galay-kernel/kernel/IOUringScheduler.cc`
- Modify: `CMakeLists.txt`
- Modify: `galay-kernel-config.cmake.in`
- Modify: `galay-kernel-api-surface.cmake.in`
- Modify: package export or install lists as needed

**Step 1: Delete only after repository scan is clean**

Run:

```bash
rg -n "\bCoroutine\b|ComputeScheduler|EpollScheduler|KqueueScheduler|IOUringScheduler|IOScheduler|SpawnAwaitable|WaitResult" galay-kernel examples test benchmark README.md docs
```

Expected: no remaining production references before physical deletion.

**Step 2: Update build and install surfaces**

Ensure removed files are no longer compiled, installed, exported, or documented as supported headers.

**Step 3: Run a clean rebuild**

Run:

```bash
cmake -S . -B build-cutover -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_BENCHMARKS=ON
cmake --build build-cutover --parallel
```

Expected: clean rebuild succeeds with old files removed.

### Task 8: Rewrite the main docs and add migration docs

**Files:**
- Modify: `README.md`
- Modify: `docs/00-快速开始.md`
- Modify: `docs/01-架构设计.md`
- Modify: `docs/02-API参考.md`
- Modify: `docs/03-使用指南.md`
- Modify: `docs/04-示例代码.md`
- Modify: `docs/06-高级主题.md`
- Modify: `docs/07-常见问题.md`
- Create: `docs/21-迁移指南.md`
- Create: `docs/22-平台能力矩阵.md`
- Create: `docs/23-术语与关键词索引.md`
- Modify: `docs/README.md`

**Step 1: Write the failing doc scan**

Run:

```bash
rg -n "\bCoroutine\b|ComputeScheduler|EpollScheduler|KqueueScheduler|IOUringScheduler|IOScheduler|scheduler\.start\(|getNextIOScheduler\(" README.md docs
```

Expected: old guidance still present before rewrite.

**Step 2: Rewrite mainline docs**

Make the high-level runtime/task model the only recommended path.

**Step 3: Add migration and RAG support docs**

Add:

- migration guide
- backend capability matrix
- terminology/keyword index

**Step 4: Re-scan docs**

Run:

```bash
rg -n "\bCoroutine\b|ComputeScheduler|EpollScheduler|KqueueScheduler|IOUringScheduler|IOScheduler|scheduler\.start\(|getNextIOScheduler\(" README.md docs
```

Expected: only intentional historical mentions remain, if any.

### Task 9: Full verification and release re-tag

**Files:**
- Verify: entire repository
- Modify: Git tag / GitHub release metadata after successful verification

**Step 1: Run repository verification**

Run:

```bash
python3 -m unittest \
  scripts.tests.test_run_benchmark_matrix \
  scripts.tests.test_run_benchmark_triplet \
  scripts.tests.test_run_single_benchmark_triplet \
  scripts.tests.test_run_test_matrix \
  scripts.tests.test_parse_benchmark_triplet
cmake -S . -B build-final -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_BENCHMARKS=ON
cmake --build build-final --parallel
bash scripts/run_test_matrix.sh "$PWD/build-final" "$PWD/build-final/test_matrix_logs_final"
bash scripts/run_single_benchmark_triplet.sh \
  --baseline-ref cde3da1 \
  --benchmark B6-Udp \
  --refactored-path "$PWD" \
  --repeat 3 \
  --output-root "$PWD/build-final/single-benchmark-triplet-final/B6-Udp"
```

Expected: all test orchestration passes; benchmark orchestration completes.

**Step 2: Run full triplet by backend**

Run fresh backend-separated triplets for:

- `kqueue`
- `epoll`
- `io_uring`

Expected: final matrix available for release notes.

**Step 3: Re-tag and re-publish `v3.0.1`**

After verification only:

- commit final changes
- move `v3.0.1` to the new final commit
- push/force-update tag as required
- update GitHub release body to match the new final result

**Step 4: Final sanity checks**

Run:

```bash
git status --short
gh release view v3.0.1
git rev-parse v3.0.1
```

Expected: clean worktree or intentional staged state, release visible, tag points to final commit.
