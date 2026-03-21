# Blocking Executor Pool Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace per-task detached threads in `spawnBlocking(...)` with a bounded elastic blocking thread pool, align source version metadata to `3.4.0`, verify the change, then create tag `v3.4.1`.

**Architecture:** Keep `Runtime::spawnBlocking(...)` and `RuntimeHandle::spawnBlocking(...)` API and semantics unchanged, but swap the internal `BlockingExecutor` from `std::thread(...).detach()` to a queue-backed worker pool with `min_workers`, `max_workers`, and `keep_alive`. Test the bounded behavior first, then implement the pool, then rerun runtime regressions and update release metadata.

**Tech Stack:** C++23, `std::thread`, `std::mutex`, `std::condition_variable`, `std::deque`, CMake, existing runtime/task tests.

---

### Task 1: Add the failing bounded-pool regression test

**Files:**
- Create: `test/T102-blocking_executor_pool.cc`
- Test: `test/T102-blocking_executor_pool.cc`

**Step 1: Write the failing test**

Write a regression that constructs a small `BlockingExecutor`, submits three `100ms` tasks, and asserts the total duration requires at least two waves when `max_workers = 2`.

**Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target T102-blocking_executor_pool --parallel && ./build/bin/T102-blocking_executor_pool`
Expected: build or test failure because `BlockingExecutor` does not yet expose the bounded pool behavior.

**Step 3: Commit**

```bash
git add test/T102-blocking_executor_pool.cc
git commit -m "test: add blocking executor bounded pool regression"
```

### Task 2: Implement the bounded elastic blocking executor

**Files:**
- Modify: `galay-kernel/kernel/BlockingExecutor.h`
- Modify: `galay-kernel/kernel/BlockingExecutor.cc`

**Step 1: Write minimal implementation**

Implement:

- constructor with `min_workers`, `max_workers`, `keep_alive`
- queue-backed `submit(...)`
- worker spawn-on-demand under `max_workers`
- idle timeout shrink above `min_workers`
- destructor that drains and joins workers

Keep `submit(...)` callable from `Runtime::spawnBlocking(...)` without API changes there.

**Step 2: Run bounded-pool regression**

Run: `cmake --build build --target T102-blocking_executor_pool --parallel && ./build/bin/T102-blocking_executor_pool`
Expected: PASS

**Step 3: Run existing `spawnBlocking` regression**

Run: `cmake --build build --target T56-runtime_spawn_blocking --parallel && ./build/bin/T56-runtime_spawn_blocking`
Expected: PASS

**Step 4: Commit**

```bash
git add galay-kernel/kernel/BlockingExecutor.h galay-kernel/kernel/BlockingExecutor.cc test/T102-blocking_executor_pool.cc
git commit -m "feat: replace spawnBlocking detached threads with blocking pool"
```

### Task 3: Align source version metadata to 3.4.0

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `MODULE.bazel`
- Modify: `README.md`

**Step 1: Update version metadata**

Set source-distributed version metadata to `3.4.0` and add a short README note about the blocking executor optimization.

**Step 2: Run a lightweight metadata sanity check**

Run: `rg -n "3\\.3\\.0|v3\\.3\\.0" CMakeLists.txt MODULE.bazel README.md`
Expected: only intentional historical references remain.

**Step 3: Commit**

```bash
git add CMakeLists.txt MODULE.bazel README.md
git commit -m "chore: align source metadata to 3.4.0"
```

### Task 4: Full verification and release tag

**Files:**
- Modify: Git tag namespace only after verification

**Step 1: Run focused verification**

Run: `cmake --build build --target T56-runtime_spawn_blocking T57-runtime_task_api_surface T102-blocking_executor_pool --parallel && ./build/bin/T56-runtime_spawn_blocking && ./build/bin/T57-runtime_task_api_surface && ./build/bin/T102-blocking_executor_pool`
Expected: all PASS

**Step 2: Run broader runtime regression slice**

Run: `cmake --build build --target T25-spawn_simple T52-runtime_block_on_result T53-runtime_block_on_exception T54-runtime_spawn_join_handle T55-runtime_handle_current T56-runtime_spawn_blocking T57-runtime_task_api_surface --parallel && ./build/bin/T25-spawn_simple && ./build/bin/T52-runtime_block_on_result && ./build/bin/T53-runtime_block_on_exception && ./build/bin/T54-runtime_spawn_join_handle && ./build/bin/T55-runtime_handle_current && ./build/bin/T56-runtime_spawn_blocking && ./build/bin/T57-runtime_task_api_surface`
Expected: all PASS

**Step 3: Create the release tag**

Run: `git tag -a v3.4.1 -m "v3.4.1"`
Expected: annotated tag created on the verified final commit.

**Step 4: Final status check**

Run: `git status --short && git rev-parse v3.4.1`
Expected: intended worktree state shown, tag resolves to current verified commit.
