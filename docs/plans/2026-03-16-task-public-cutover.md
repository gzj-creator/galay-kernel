# Task 公开接口切换 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Remove `Coroutine` from the public API, switch scheduler submission to `TaskRef`, and migrate the repository to a pure `Task<T>` execution model without compatibility shims.

**Architecture:** First lock the new public surface with failing tests, then split the retained task core out of `Coroutine.h` into `Task.h`. After that, convert the scheduler stack to task-native submission, replace `wait/then/spawn` semantics inside `Task<T>`, migrate repository consumers, physically delete the old sources, and finish with full triplet verification.

**Tech Stack:** C++23, C++20 coroutines, kqueue, epoll, io_uring, CMake, Bash verification scripts, Python unittest, Git worktrees

---

### Task 1: Establish isolated worktree and baseline inventory

**Files:**
- Inspect: `galay-kernel/kernel/Coroutine.h`
- Inspect: `galay-kernel/kernel/Coroutine.cc`
- Inspect: `galay-kernel/kernel/Scheduler.hpp`
- Inspect: `galay-kernel/kernel/Runtime.h`
- Inspect: `galay-kernel/kernel/Runtime.cc`
- Inspect: `test/`
- Inspect: `examples/`
- Inspect: `benchmark/`
- Inspect: `docs/`

**Step 1: Create a dedicated worktree**

Run:

```bash
git worktree add .worktrees/task-public-cutover -b task-public-cutover
```

Expected: a clean isolated worktree is created for the cutover.

**Step 2: Configure a fresh build directory inside the worktree**

Run:

```bash
cmake -S . -B build-task-public-cutover -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_BENCHMARKS=ON
```

Expected: configure succeeds before any code changes.

**Step 3: Record the current old-interface surface**

Run:

```bash
rg -n "\bCoroutine\b|PromiseType|WaitResult|SpawnAwaitable|spawn\(Coroutine|spawnImmidiately|spawnDeferred\(Coroutine" galay-kernel test examples benchmark docs README.md
```

Expected: many hits across kernel, tests, examples, benchmarks, and docs.

**Step 4: Record the current scheduler/symbol surface**

Run:

```bash
rg -n "Coroutine\\.h|ComputeScheduler|KqueueScheduler|EpollScheduler|IOUringScheduler|IOScheduler" galay-kernel test examples benchmark docs README.md
```

Expected: many hits; save this output as the deletion checklist.

### Task 2: Lock the new public API with failing surface tests

**Files:**
- Create: `test/T78-task_await_surface.cc`
- Create: `test/T79-task_then_surface.cc`
- Create: `test/T80-scheduler_taskref_surface.cc`
- Reuse: `test/CMakeLists.txt`

**Step 1: Write the failing `co_await Task<T>` surface test**

Create `test/T78-task_await_surface.cc` with a runtime-level assertion such as:

```cpp
#include "galay-kernel/kernel/Runtime.h"
#include "galay-kernel/kernel/Task.h"
#include <cassert>

using namespace galay::kernel;

Task<int> childTask() {
    co_return 7;
}

Task<int> parentTask() {
    int value = co_await childTask();
    co_return value + 1;
}

int main() {
    Runtime runtime;
    assert(runtime.blockOn(parentTask()) == 8);
    return 0;
}
```

**Step 2: Write the failing `Task<void>::then(...)` surface test**

Create `test/T79-task_then_surface.cc` with a task-only chain:

```cpp
Task<void> pushStep(int step);

int main() {
    auto root = pushStep(1).then(pushStep(2));
    return 0;
}
```

Expected: compile fails until `Task<void>::then(...)` exists on the new public type.

**Step 3: Write the failing scheduler public-surface test**

Create `test/T80-scheduler_taskref_surface.cc` with concepts/static_asserts that require:

- `Scheduler::schedule(TaskRef)`
- `Scheduler::scheduleDeferred(TaskRef)`
- `Scheduler::scheduleImmediately(TaskRef)`

and forbid:

- `spawn(Coroutine)`
- `spawnImmidiately(Coroutine)`

**Step 4: Run the new targets and confirm they fail for the right reasons**

Run:

```bash
cmake --build build-task-public-cutover --target T78-task_await_surface T79-task_then_surface T80-scheduler_taskref_surface --parallel
```

Expected: FAIL because `Task.h`, direct `co_await Task<T>`, `Task<void>::then(...)`, and task-native scheduler APIs are not complete yet.

**Step 5: Commit the failing tests**

```bash
git add test/T78-task_await_surface.cc test/T79-task_then_surface.cc test/T80-scheduler_taskref_surface.cc
git commit -m "新增 Task 公开切换失败测试"
```

### Task 3: Extract the public task core out of `Coroutine`

**Files:**
- Create: `galay-kernel/kernel/Task.h`
- Create: `galay-kernel/kernel/Task.cc`
- Modify: `galay-kernel/kernel/Runtime.h`
- Modify: `galay-kernel/kernel/Runtime.cc`
- Modify: `galay-kernel/kernel/Waker.h`
- Modify: `galay-kernel/kernel/Waker.cc`
- Modify: `galay-kernel/concurrency/AsyncMutex.h`
- Modify: `galay-kernel/concurrency/AsyncWaiter.h`
- Modify: `galay-kernel/concurrency/MpscChannel.h`
- Modify: `galay-kernel/concurrency/UnsafeChannel.h`
- Modify: `galay-kernel/module/galay.kernel.cppm`

**Step 1: Move retained task primitives into `Task.h`**

Keep only task-native pieces:

- `TaskRef`
- `TaskState`
- `TaskCompletionState<T>`
- `TaskPromise<T>`
- `Task<T>`
- `JoinHandle<T>`

Move non-template runtime/task helper implementations into `Task.cc`.

**Step 2: Repoint public headers away from `Coroutine.h`**

Replace includes and forward declarations so runtime, waker, concurrency primitives, and the module surface include `Task.h` instead of `Coroutine.h`.

**Step 3: Keep existing runtime behavior compiling through the new header**

Do not delete `Coroutine` yet. Only make `Task<T>` the public entry type that can stand on its own.

**Step 4: Run the runtime-facing API tests**

Run:

```bash
cmake --build build-task-public-cutover --target T52-runtime_block_on_result T53-runtime_block_on_exception T54-runtime_spawn_join_handle T55-runtime_handle_current T56-runtime_spawn_blocking T57-runtime_task_api_surface T78-task_await_surface --parallel
./build-task-public-cutover/bin/T52-runtime_block_on_result
./build-task-public-cutover/bin/T53-runtime_block_on_exception
./build-task-public-cutover/bin/T54-runtime_spawn_join_handle
./build-task-public-cutover/bin/T55-runtime_handle_current
./build-task-public-cutover/bin/T56-runtime_spawn_blocking
./build-task-public-cutover/bin/T57-runtime_task_api_surface
```

Expected: all runtime tests pass except the still-unimplemented await/then scheduler-surface pieces.

**Step 5: Commit the task-core extraction**

```bash
git add galay-kernel/kernel/Task.h galay-kernel/kernel/Task.cc galay-kernel/kernel/Runtime.h galay-kernel/kernel/Runtime.cc galay-kernel/kernel/Waker.h galay-kernel/kernel/Waker.cc galay-kernel/concurrency/AsyncMutex.h galay-kernel/concurrency/AsyncWaiter.h galay-kernel/concurrency/MpscChannel.h galay-kernel/concurrency/UnsafeChannel.h galay-kernel/module/galay.kernel.cppm
git commit -m "拆分公开 Task 核心头文件"
```

### Task 4: Convert scheduler submission to `TaskRef`

**Files:**
- Modify: `galay-kernel/kernel/Scheduler.hpp`
- Modify: `galay-kernel/kernel/ComputeScheduler.h`
- Modify: `galay-kernel/kernel/ComputeScheduler.cc`
- Modify: `galay-kernel/kernel/KqueueScheduler.h`
- Modify: `galay-kernel/kernel/KqueueScheduler.cc`
- Modify: `galay-kernel/kernel/EpollScheduler.h`
- Modify: `galay-kernel/kernel/EpollScheduler.cc`
- Modify: `galay-kernel/kernel/IOUringScheduler.h`
- Modify: `galay-kernel/kernel/IOUringScheduler.cc`
- Modify: `galay-kernel/kernel/SchedulerCore.h`
- Modify: `test/T41-task_ref_schedule_path.cc`
- Modify: `test/T43-wait_continuation_scheduler_path.cc`
- Modify: `test/T46-scheduler_injected_burst_fastpath.cc`
- Modify: `test/T50-compute_scheduler_taskref_fastpath.cc`

**Step 1: Remove `spawn(Coroutine)` from the scheduler interface**

Change the public virtual API to:

```cpp
virtual bool schedule(TaskRef task) = 0;
virtual bool scheduleDeferred(TaskRef task) = 0;
virtual bool scheduleImmediately(TaskRef task) = 0;
```

Delete `spawn(Coroutine)`, `spawnDeferred(Coroutine)`, and `spawnImmidiately(Coroutine)`.

**Step 2: Update all backends to use task-native submission**

Make ready, injected, deferred, and immediate paths operate directly on `TaskRef`.

**Step 3: Rename the typoed immediate API everywhere**

Replace `spawnImmidiately` with `scheduleImmediately` in kernel, tests, and docs comments that will remain.

**Step 4: Run focused scheduler tests**

Run:

```bash
cmake --build build-task-public-cutover --target T41-task_ref_schedule_path T43-wait_continuation_scheduler_path T46-scheduler_injected_burst_fastpath T47-scheduler_queue_edge_wakeup T48-scheduler_core_loop_stages T49-channel_wait_registration T50-compute_scheduler_taskref_fastpath T80-scheduler_taskref_surface --parallel
./build-task-public-cutover/bin/T41-task_ref_schedule_path
./build-task-public-cutover/bin/T43-wait_continuation_scheduler_path
./build-task-public-cutover/bin/T46-scheduler_injected_burst_fastpath
./build-task-public-cutover/bin/T47-scheduler_queue_edge_wakeup
./build-task-public-cutover/bin/T48-scheduler_core_loop_stages
./build-task-public-cutover/bin/T49-channel_wait_registration
./build-task-public-cutover/bin/T50-compute_scheduler_taskref_fastpath
./build-task-public-cutover/bin/T80-scheduler_taskref_surface
```

Expected: scheduler tests pass on `TaskRef` submission and the public surface test turns green.

**Step 5: Commit the scheduler cutover**

```bash
git add galay-kernel/kernel/Scheduler.hpp galay-kernel/kernel/ComputeScheduler.h galay-kernel/kernel/ComputeScheduler.cc galay-kernel/kernel/KqueueScheduler.h galay-kernel/kernel/KqueueScheduler.cc galay-kernel/kernel/EpollScheduler.h galay-kernel/kernel/EpollScheduler.cc galay-kernel/kernel/IOUringScheduler.h galay-kernel/kernel/IOUringScheduler.cc galay-kernel/kernel/SchedulerCore.h test/T41-task_ref_schedule_path.cc test/T43-wait_continuation_scheduler_path.cc test/T46-scheduler_injected_burst_fastpath.cc test/T50-compute_scheduler_taskref_fastpath.cc
git commit -m "将调度器提交接口切换为 TaskRef"
```

### Task 5: Replace `wait/then/spawn` with task-native semantics and delete old helper types

**Files:**
- Modify: `galay-kernel/kernel/Task.h`
- Modify: `galay-kernel/kernel/Task.cc`
- Modify: `galay-kernel/kernel/Runtime.h`
- Modify: `galay-kernel/kernel/Runtime.cc`
- Delete: `galay-kernel/kernel/Coroutine.h`
- Delete: `galay-kernel/kernel/Coroutine.cc`
- Modify: `test/T1-coroutine_chain.cc`
- Delete: `test/T61-coroutine_then_compat.cc`
- Create: `test/T61-task_then_compat.cc`

**Step 1: Implement direct `co_await Task<T>`**

Add a task awaiter that:

- binds the child task to the current runtime/scheduler if needed
- schedules the child immediately
- links the waiting task through `TaskState::m_next`
- resumes the waiter with the child result or exception

**Step 2: Implement `Task<void>::then(...)`**

Keep the continuation intrusive and allocation-free by reusing `TaskState::m_then`.

**Step 3: Remove old task helper types**

Delete:

- `Coroutine`
- `PromiseType`
- `WaitResult`
- `SpawnAwaitable`

and replace any remaining task submission helper with a task-native equivalent.

**Step 4: Migrate the legacy chain tests**

Rewrite `test/T1-coroutine_chain.cc` and `test/T61-task_then_compat.cc` to use:

```cpp
co_await childTask();
auto root = stepTask(1).then(stepTask(2));
```

**Step 5: Run the task-semantics tests**

Run:

```bash
cmake --build build-task-public-cutover --target T1-coroutine_chain T25-spawn_simple T52-runtime_block_on_result T53-runtime_block_on_exception T54-runtime_spawn_join_handle T55-runtime_handle_current T61-task_then_compat T78-task_await_surface T79-task_then_surface --parallel
./build-task-public-cutover/bin/T1-coroutine_chain
./build-task-public-cutover/bin/T25-spawn_simple
./build-task-public-cutover/bin/T52-runtime_block_on_result
./build-task-public-cutover/bin/T53-runtime_block_on_exception
./build-task-public-cutover/bin/T54-runtime_spawn_join_handle
./build-task-public-cutover/bin/T55-runtime_handle_current
./build-task-public-cutover/bin/T61-task_then_compat
./build-task-public-cutover/bin/T78-task_await_surface
./build-task-public-cutover/bin/T79-task_then_surface
```

Expected: all task-semantics tests pass without any public `Coroutine` dependency.

**Step 6: Commit the task-semantics cutover**

```bash
git add galay-kernel/kernel/Task.h galay-kernel/kernel/Task.cc galay-kernel/kernel/Runtime.h galay-kernel/kernel/Runtime.cc test/T1-coroutine_chain.cc test/T61-task_then_compat.cc test/T78-task_await_surface.cc test/T79-task_then_surface.cc
git rm galay-kernel/kernel/Coroutine.h galay-kernel/kernel/Coroutine.cc test/T61-coroutine_then_compat.cc
git commit -m "移除 Coroutine 并收敛 Task 语义"
```

### Task 6: Migrate repository consumers to the new public surface

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
- Modify: `test/T3-tcp_server.cc`
- Modify: `test/T4-tcp_client.cc`
- Modify: `test/T5-udp_socket.cc`
- Modify: `test/T6-udp_server.cc`
- Modify: `test/T7-udp_client.cc`
- Modify: `test/T8-file_io.cc`
- Modify: `test/T9-file_watcher.cc`
- Modify: `test/T11-compute_scheduler.cc`
- Modify: `test/T12-mixed_scheduler.cc`
- Modify: `test/T13-async_mutex.cc`
- Modify: `test/T14-mpsc_channel.cc`
- Modify: `test/T17-unsafe_channel.cc`
- Modify: `test/T19-readv_writev.cc`
- Modify: `test/T20-ringbuffer_io.cc`
- Modify: `test/T23-sendfile_basic.cc`
- Modify: `test/T26-concurrent_recv_send.cc`
- Modify: `test/T27-runtime_stress.cc`
- Modify: `test/T29-virtual_handle_complete.cc`
- Modify: `test/T30-custom_awaitable.cc`
- Modify: `test/T32-io_scheduler_local_first.cc`
- Modify: `test/T33-epoll_accept_rearm.cc`
- Modify: `test/T34-readv_array_borrowed.cc`
- Modify: `test/T36-task_core_wake_path.cc`
- Modify: `test/T40-io_uring_custom_awaitable_no_null_probe.cc`
- Modify: `test/T45-scheduler_ready_budget.cc`
- Modify: `test/T57-io_uring_timeout_close_lifetime.cc`
- Modify: `test/T63-custom_sequence_awaitable.cc`
- Modify: `test/T76-sequence_parser_need_more.cc`
- Modify: `test/T77-sequence_parser_coalesced_frames.cc`

**Step 1: Replace direct `Coroutine.h` includes with `Task.h` or `Runtime.h`**

Update tests and examples so they include the new public headers only.

**Step 2: Replace direct scheduler-driven root execution**

Migrate old patterns like:

```cpp
scheduler.start();
scheduler.spawn(root());
scheduler.stop();
```

to runtime-driven entry where possible:

```cpp
Runtime runtime;
runtime.blockOn(rootTask());
```

**Step 3: Preserve kernel-internal tests that still need direct scheduler coverage**

Where a test truly targets scheduler internals, update it to use `TaskRef` submission instead of reintroducing `Coroutine`.

**Step 4: Run example and focused I/O tests**

Run:

```bash
cmake --build build-task-public-cutover --target E1-SendfileExample E2-TcpEchoServer E3-TcpClient E4-CoroutineBasic E5-UdpEcho T3-tcp_server T4-tcp_client T5-udp_socket T6-udp_server T7-udp_client T8-file_io T9-file_watcher T19-readv_writev T23-sendfile_basic T76-sequence_parser_need_more T77-sequence_parser_coalesced_frames --parallel
./build-task-public-cutover/bin/T8-file_io
./build-task-public-cutover/bin/T9-file_watcher
./build-task-public-cutover/bin/T19-readv_writev
./build-task-public-cutover/bin/T23-sendfile_basic
./build-task-public-cutover/bin/T76-sequence_parser_need_more
./build-task-public-cutover/bin/T77-sequence_parser_coalesced_frames
```

Expected: examples build cleanly and focused I/O/parser tests still pass on the new task model.

**Step 5: Commit the repository migration**

```bash
git add examples test
git commit -m "迁移示例与测试到 Task 接口"
```

### Task 7: Migrate benchmarks, docs, and module surface

**Files:**
- Modify: `benchmark/B1-compute_scheduler.cc`
- Modify: `benchmark/B2-tcp_server.cc`
- Modify: `benchmark/B3-tcp_client.cc`
- Modify: `benchmark/B4-udp_server.cc`
- Modify: `benchmark/B5-udp_client.cc`
- Modify: `benchmark/B6-Udp.cc`
- Modify: `benchmark/B7-file_io.cc`
- Modify: `benchmark/B8-mpsc_channel.cc`
- Modify: `benchmark/B9-unsafe_channel.cc`
- Modify: `benchmark/B11-tcp_iov_server.cc`
- Modify: `benchmark/B12-tcp_iov_client.cc`
- Modify: `benchmark/B13-Sendfile.cc`
- Modify: `benchmark/B14-scheduler_injected_wakeup.cc`
- Modify: `docs/01-架构设计.md`
- Modify: `docs/02-API参考.md`
- Modify: `docs/03-使用指南.md`
- Modify: `docs/04-示例代码.md`
- Modify: `docs/05-性能测试.md`
- Modify: `docs/08-计算调度器.md`
- Modify: `docs/11-协程.md`
- Modify: `docs/18-运行时Runtime.md`
- Modify: `docs/README.md`
- Modify: `galay-kernel/module/galay.kernel.cppm`

**Step 1: Rewrite benchmark entry points to `Task<T>`**

Remove direct `Coroutine.h` dependency and adapt benchmark roots to `Task<T>`.

**Step 2: Rewrite docs to match the new public surface**

Remove or rewrite every remaining reference to:

- `Coroutine`
- `PromiseType`
- `WaitResult`
- `SpawnAwaitable`
- `spawnImmidiately`

**Step 3: Update the C++23 module surface**

Ensure `galay.kernel` exports `Task.h` and does not export removed public headers.

**Step 4: Run the script/unit sanity checks and benchmark builds**

Run:

```bash
python3 -m unittest scripts.tests.test_run_benchmark_matrix scripts.tests.test_run_benchmark_triplet scripts.tests.test_run_single_benchmark_triplet scripts.tests.test_parse_benchmark_triplet scripts.tests.test_run_test_matrix
cmake --build build-task-public-cutover --target B1-ComputeScheduler B2-TcpServer B3-TcpClient B4-UdpServer B5-UdpClient B6-Udp B7-FileIo B8-MpscChannel B9-UnsafeChannel B11-TcpIovServer B12-TcpIovClient B13-Sendfile B14-SchedulerInjectedWakeup --parallel
```

Expected: verification scripts still pass and every benchmark target still compiles.

**Step 5: Commit the benchmark and doc migration**

```bash
git add benchmark docs galay-kernel/module/galay.kernel.cppm
git commit -m "迁移基准与文档到 Task 接口"
```

### Task 8: Remove residue and prove the old public symbols are gone

**Files:**
- Delete: any remaining old-interface-only source or test files discovered during the scan
- Modify: install/export paths if any removed header is still installed

**Step 1: Run the zero-residue public scan**

Run:

```bash
rg -n "\bCoroutine\b|PromiseType|WaitResult|SpawnAwaitable|spawn\(Coroutine|spawnImmidiately|Coroutine\\.h" galay-kernel test examples benchmark docs README.md
```

Expected: no hits outside historical plan documents that are intentionally retained.

**Step 2: Run the zero-residue scheduler-class scan**

Run:

```bash
rg -n "ComputeScheduler|KqueueScheduler|EpollScheduler|IOUringScheduler|IOScheduler" galay-kernel test examples benchmark docs README.md
```

Expected: only intended low-level scheduler implementation references remain; no public include or example should depend on them.

**Step 3: Clean install/export residue**

If install rules or the module export still reference removed public headers, delete those references now.

**Step 4: Commit the residue cleanup**

```bash
git add galay-kernel/CMakeLists.txt galay-kernel/module/galay.kernel.cppm docs
git commit -m "清理 Task 切换后的残留接口"
```

### Task 9: Run full verification across kqueue, epoll, and io_uring

**Files:**
- Reuse: `scripts/run_test_matrix.sh`
- Reuse: `scripts/run_benchmark_triplet.sh`
- Reuse: `scripts/run_single_benchmark_triplet.sh`
- Reuse: `scripts/run_benchmark_matrix.sh`
- Reuse: `scripts/tests/`

**Step 1: Run the full local test matrix on the active backend**

Run:

```bash
bash scripts/run_test_matrix.sh "$(pwd)/build-task-public-cutover" "$(pwd)/build-task-public-cutover/test_matrix_logs"
```

Expected: all tests finish without new `Segmentation fault`, `terminate called`, `dumped core`, or timeout markers.

**Step 2: Build and run the benchmark matrix on the current backend**

Run:

```bash
bash scripts/run_benchmark_matrix.sh "$(pwd)/build-task-public-cutover" "$(pwd)/build-task-public-cutover/benchmark_logs"
```

Expected: all benchmark targets complete or produce an explicit, investigated failure log.

**Step 3: Repeat the cutover verification for all three backends**

Run the same configure, build, test matrix, and benchmark matrix flow in fresh backend-specific build directories:

```bash
cmake -S . -B build-kqueue-task-public-cutover -DGALAY_KERNEL_BACKEND=kqueue -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_BENCHMARKS=ON
cmake -S . -B build-epoll-task-public-cutover -DGALAY_KERNEL_BACKEND=epoll -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_BENCHMARKS=ON
cmake -S . -B build-io_uring-task-public-cutover -DGALAY_KERNEL_BACKEND=io_uring -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_BENCHMARKS=ON
```

Then run `scripts/run_test_matrix.sh` and `scripts/run_benchmark_matrix.sh` against each build directory one by one.

**Step 4: Summarize the final triplet matrix**

Produce a release-ready table that includes:

- backend
- test pass/fail counts
- benchmark pass/fail counts
- notable regressions
- any intentional exclusions

**Step 5: Commit the final verification artifacts**

```bash
git add docs
git commit -m "记录 Task 公开切换全量验证结果"
```
