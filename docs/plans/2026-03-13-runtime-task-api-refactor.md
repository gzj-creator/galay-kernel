# Runtime Task API Refactor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 为 `v3` runtime 引入 Tokio 风格的 `Task<T>`、`JoinHandle<T>`、`RuntimeHandle`、`blockOn`、`spawn` 和 `spawnBlocking`，同时保留现有 scheduler 内核与 `Coroutine` 兼容路径。

**Architecture:** 先用回归测试锁定返回值、异常、detach、runtime context 和 blocking task 语义，再把当前 `Coroutine`/`PromiseType`/`TaskState` 泛型化为 `Task<T>` 体系。`Runtime::blockOn(...)` 通过现有 scheduler 调度根任务，并用同步完成观察器在调用线程阻塞等结果；`spawnBlocking(...)` 则落到独立 blocking executor，而不是复用 compute scheduler。

**Tech Stack:** C++23、C++20 协程、现有 `TaskRef` intrusive refcount 内核、`std::exception_ptr`、`std::condition_variable`、独立 blocking executor、CMake。

---

### Task 1: 用 failing tests 锁定新 runtime API 语义

**Files:**
- Create: `test/T52-runtime_block_on_result.cc`
- Create: `test/T53-runtime_block_on_exception.cc`
- Create: `test/T54-runtime_spawn_join_handle.cc`
- Create: `test/T55-runtime_handle_current.cc`
- Create: `test/T56-runtime_spawn_blocking.cc`
- Modify: `test/CMakeLists.txt`

**Step 1: 写 `blockOn` 返回值测试**

```cpp
Task<int> answer() {
    co_return 42;
}

int main() {
    Runtime runtime = RuntimeBuilder().ioSchedulerCount(1).computeSchedulerCount(1).build();
    int value = runtime.blockOn(answer());
    assert(value == 42);
}
```

**Step 2: 写 `blockOn` 异常传播测试**

```cpp
Task<void> explode() {
    throw std::runtime_error("boom");
    co_return;
}
```

验证 `runtime.blockOn(explode())` 会把异常重新抛回调用方。

**Step 3: 写 `spawn` / `JoinHandle` / detach 语义测试**

- `Runtime::spawn(Task<int>)` 返回 `JoinHandle<int>`
- `JoinHandle<int>` 能拿到结果
- drop handle 后任务仍会继续跑完

**Step 4: 写 `RuntimeHandle::current()` / `tryCurrent()` 语义测试**

- runtime 上下文内 `current()` 成功
- 无上下文时 `tryCurrent()` 返回失败

**Step 5: 写 `spawnBlocking` 语义测试**

- 返回值回传正确
- 多个 blocking 任务能并发执行

**Step 6: 运行 tests 验证正确失败**

Run: `cmake --build build --target T52-runtime_block_on_result T53-runtime_block_on_exception T54-runtime_spawn_join_handle T55-runtime_handle_current T56-runtime_spawn_blocking -j4 && ./build/bin/T52-runtime_block_on_result && ./build/bin/T53-runtime_block_on_exception && ./build/bin/T54-runtime_spawn_join_handle && ./build/bin/T55-runtime_handle_current && ./build/bin/T56-runtime_spawn_blocking`

Expected: 至少有一项因缺少 `Task<T>` / `blockOn` / `JoinHandle` / `RuntimeHandle` 能力而 FAIL。

**Step 7: Commit**

```bash
git add test/T52-runtime_block_on_result.cc test/T53-runtime_block_on_exception.cc test/T54-runtime_spawn_join_handle.cc test/T55-runtime_handle_current.cc test/T56-runtime_spawn_blocking.cc test/CMakeLists.txt
git commit -m "test: add runtime task api coverage"
```

说明：仅在用户明确要求提交时执行。

### Task 2: 泛型化任务状态并保留 `Coroutine` 兼容层

**Files:**
- Modify: `galay-kernel/kernel/Coroutine.h`
- Modify: `galay-kernel/kernel/Coroutine.cc`
- Optionally Create: `galay-kernel/kernel/Task.h`
- Optionally Create: `galay-kernel/kernel/Task.cc`
- Modify: `galay-kernel/module/galay.kernel.cppm`

**Step 1: 写 `TaskStateBase` / `TaskState<T>` / `Promise<T>` 的最小类型定义**

- scheduler 继续只依赖无类型 task core
- 结果、异常、同步 waiter 状态放入泛型任务状态

**Step 2: 将 `Task<void>` 映射到现有 `Coroutine` 行为**

- `Coroutine` 先作为 `Task<void>` 的兼容别名或等价兼容层
- 现有 awaitable 上的 `Coroutine::promise_type` 继续可用

**Step 3: 实现 `return_value(...)` / `return_void()` / 异常捕获**

```cpp
template <typename T>
void return_value(T value) noexcept;

void return_void() noexcept;

void unhandled_exception() noexcept;
```

**Step 4: 运行 `T52` / `T53` 验证行为仍未完全通过但泛型任务骨架可编译**

Run: `cmake --build build --target T52-runtime_block_on_result T53-runtime_block_on_exception -j4`

Expected: 编译进入 `Runtime` 缺失或结果提取缺失阶段，而不是 `Task<T>` 类型本身无法定义。

### Task 3: 为任务结果提取增加同步等待基础设施

**Files:**
- Modify: `galay-kernel/kernel/Coroutine.h`
- Modify: `galay-kernel/kernel/Coroutine.cc`
- Create: `galay-kernel/kernel/TaskWaiter.h`
- Create: `galay-kernel/kernel/TaskWaiter.cc`

**Step 1: 写同步完成观察器**

- 支持等待任务完成
- 支持读取结果或重抛异常
- 不要求任务在调用线程 poll

**Step 2: 把根任务和 `JoinHandle<T>` 的结果提取统一到同一套观察器上**

**Step 3: 跑 `T52` / `T53`，确认只差 `Runtime::blockOn(...)` 接线**

Run: `cmake --build build --target T52-runtime_block_on_result T53-runtime_block_on_exception -j4`

Expected: 编译成功，运行失败点转移到 `Runtime::blockOn(...)` 未接线或未提交任务。

### Task 4: 增加 `RuntimeHandle` 与 runtime 上下文

**Files:**
- Modify: `galay-kernel/kernel/Runtime.h`
- Modify: `galay-kernel/kernel/Runtime.cc`
- Create: `galay-kernel/kernel/RuntimeHandle.h`
- Create: `galay-kernel/kernel/RuntimeHandle.cc`
- Modify: `galay-kernel/module/galay.kernel.cppm`

**Step 1: 写线程局部 runtime context 与 `RuntimeHandle`**

- `Runtime::handle()`
- `RuntimeHandle::current()`
- `RuntimeHandle::tryCurrent()`
- `RuntimeHandle::spawn(...)`
- `RuntimeHandle::spawnBlocking(...)`

**Step 2: 在 scheduler worker 进入 runtime 执行前设置 context**

**Step 3: 运行 `T55-runtime_handle_current` 验证上下文语义**

Run: `cmake --build build --target T55-runtime_handle_current -j4 && ./build/bin/T55-runtime_handle_current`

Expected: PASS。

### Task 5: 实现 `Runtime::blockOn(Task<T>)`

**Files:**
- Modify: `galay-kernel/kernel/Runtime.h`
- Modify: `galay-kernel/kernel/Runtime.cc`
- Modify: `galay-kernel/kernel/RuntimeHandle.h`
- Modify: `galay-kernel/kernel/RuntimeHandle.cc`

**Step 1: 写根任务提交逻辑**

- runtime 未启动时先确保已就绪
- 根任务提交到 runtime 默认 scheduler
- 调用线程通过同步观察器阻塞等待

**Step 2: 让 `blockOn(...)` 返回结果或重抛异常**

**Step 3: 运行 `T52` / `T53`**

Run: `cmake --build build --target T52-runtime_block_on_result T53-runtime_block_on_exception -j4 && ./build/bin/T52-runtime_block_on_result && ./build/bin/T53-runtime_block_on_exception`

Expected: PASS。

### Task 6: 实现 `JoinHandle<T>` 与 `Runtime::spawn(Task<T>)`

**Files:**
- Create: `galay-kernel/kernel/JoinHandle.h`
- Create: `galay-kernel/kernel/JoinHandle.cc`
- Modify: `galay-kernel/kernel/Runtime.h`
- Modify: `galay-kernel/kernel/Runtime.cc`
- Modify: `galay-kernel/kernel/RuntimeHandle.h`
- Modify: `galay-kernel/kernel/RuntimeHandle.cc`
- Modify: `galay-kernel/module/galay.kernel.cppm`

**Step 1: 写 `JoinHandle<T>` 的最小实现**

- 持有任务完成观察器
- `result()` / `join()` / 等价同步提取接口
- drop 默认 detach

**Step 2: 实现 `Runtime::spawn(...)` 与 `RuntimeHandle::spawn(...)`**

**Step 3: 跑 `T54-runtime_spawn_join_handle`**

Run: `cmake --build build --target T54-runtime_spawn_join_handle -j4 && ./build/bin/T54-runtime_spawn_join_handle`

Expected: PASS。

### Task 7: 引入独立 blocking executor 并实现 `spawnBlocking(...)`

**Files:**
- Create: `galay-kernel/kernel/BlockingExecutor.h`
- Create: `galay-kernel/kernel/BlockingExecutor.cc`
- Modify: `galay-kernel/kernel/Runtime.h`
- Modify: `galay-kernel/kernel/Runtime.cc`
- Modify: `galay-kernel/kernel/RuntimeHandle.h`
- Modify: `galay-kernel/kernel/RuntimeHandle.cc`
- Modify: `galay-kernel/module/galay.kernel.cppm`

**Step 1: 写最小 blocking executor**

- 支持提交 `FnOnce`
- 独立线程或线程池执行
- 结果写回共享状态

**Step 2: 实现 `Runtime::spawnBlocking(...)` / `RuntimeHandle::spawnBlocking(...)`**

**Step 3: 跑 `T56-runtime_spawn_blocking`**

Run: `cmake --build build --target T56-runtime_spawn_blocking -j4 && ./build/bin/T56-runtime_spawn_blocking`

Expected: PASS。

### Task 8: 更新示例、回归旧 API，并完成全量验证

**Files:**
- Modify: `test/T1-coroutine_chain.cc`
- Modify: `test/T25-spawn_simple.cc`
- Modify: `examples/import/E4-coroutine_basic.cc`
- Modify: `docs/18-运行时Runtime.md`
- Modify: `README.md`

**Step 1: 将至少一个测试和一个示例迁移到新 API**

- `Runtime::blockOn(...)`
- `Runtime::spawn(...)`

**Step 2: 确保旧 `Coroutine` 路径仍然兼容**

**Step 3: 运行新旧目标一起验证**

Run: `cmake --build build --target T1-coroutine_chain T25-spawn_simple T52-runtime_block_on_result T53-runtime_block_on_exception T54-runtime_spawn_join_handle T55-runtime_handle_current T56-runtime_spawn_blocking -j4 && ./build/bin/T1-coroutine_chain && ./build/bin/T25-spawn_simple && ./build/bin/T52-runtime_block_on_result && ./build/bin/T53-runtime_block_on_exception && ./build/bin/T54-runtime_spawn_join_handle && ./build/bin/T55-runtime_handle_current && ./build/bin/T56-runtime_spawn_blocking`

Expected: PASS。

### Task 9: 完整验证与结果收口

**Files:**
- Modify: `docs/plans/2026-03-13-runtime-task-api-refactor-design.md`

**Step 1: 运行全量 build 验证**

Run: `cmake --build build -j4`

Expected: 构建成功。

**Step 2: 运行关键 runtime / scheduler 回归**

Run: `./build/bin/T27-runtime_stress && ./build/bin/T42-runtime_strict_scheduler_counts && ./build/bin/T52-runtime_block_on_result && ./build/bin/T53-runtime_block_on_exception && ./build/bin/T54-runtime_spawn_join_handle && ./build/bin/T55-runtime_handle_current && ./build/bin/T56-runtime_spawn_blocking`

Expected: PASS。

**Step 3: Commit**

```bash
git add galay-kernel/kernel/Coroutine.h galay-kernel/kernel/Coroutine.cc galay-kernel/kernel/Runtime.h galay-kernel/kernel/Runtime.cc galay-kernel/kernel/RuntimeHandle.h galay-kernel/kernel/RuntimeHandle.cc galay-kernel/kernel/JoinHandle.h galay-kernel/kernel/JoinHandle.cc galay-kernel/kernel/BlockingExecutor.h galay-kernel/kernel/BlockingExecutor.cc galay-kernel/module/galay.kernel.cppm test/T1-coroutine_chain.cc test/T25-spawn_simple.cc test/T52-runtime_block_on_result.cc test/T53-runtime_block_on_exception.cc test/T54-runtime_spawn_join_handle.cc test/T55-runtime_handle_current.cc test/T56-runtime_spawn_blocking.cc examples/import/E4-coroutine_basic.cc docs/18-运行时Runtime.md README.md docs/plans/2026-03-13-runtime-task-api-refactor-design.md
git commit -m "feat(runtime): add tokio-style task and runtime api"
```

说明：仅在用户明确要求提交时执行。
