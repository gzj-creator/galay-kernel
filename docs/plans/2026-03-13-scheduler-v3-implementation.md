# Scheduler V3 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 在不改动 owner-thread reactor 基本模型的前提下，完成 Scheduler v3 的三项内核增强：统一 continuation 恢复路径、合并跨线程唤醒、加入 ready 预算，并用基线分支对比验证性能与 IO 表现。

**Architecture:** 继续沿用 `TaskRef + Scheduler + epoll/kqueue/io_uring` 结构，只调整调度纪律。实现顺序按正确性优先：先修 `wait()/continuation` 路径，再优化 remote wakeup，再引入 ready budget，最后补 benchmark 和 baseline/v3 对比。

**Tech Stack:** C++23、C++20 协程、moodycamel concurrent queue、epoll/kqueue/io_uring、CMake、现有 `test/` 与 `benchmark/` 基础设施。

---

### Task 1: 固定 continuation 语义的回归测试

**Files:**
- Modify: `test/T36-task_core_wake_path.cc`
- Modify: `test/T41-task_ref_schedule_path.cc`
- Create: `test/T43-wait_continuation_scheduler_path.cc`

**Step 1: 写 failing test，覆盖 wait 完成后必须回 owner scheduler**

```cpp
class CaptureScheduler final : public Scheduler {
public:
    bool schedule(TaskRef task) override {
        scheduled = true;
        last_task = task;
        return true;
    }
    // 其余纯虚接口做最小桩实现
    bool scheduled = false;
    TaskRef last_task;
};
```

补两个场景：

- child 完成后 waiter 通过 `schedule(TaskRef)` 恢复
- 不再允许直接依赖 `handle.resume()` 完成 waiter 恢复

**Step 2: 运行新增测试，确认当前实现失败**

Run: `cmake --build build --target T43-wait_continuation_scheduler_path -j4 && ./build/bin/T43-wait_continuation_scheduler_path`

Expected: FAIL，表现为 continuation 未经过 `schedule(TaskRef)` 路径。

**Step 3: 跑现有关联测试，记录基线**

Run: `cmake --build build --target T36-task_core_wake_path T41-task_ref_schedule_path -j4 && ./build/bin/T36-task_core_wake_path && ./build/bin/T41-task_ref_schedule_path`

Expected: PASS，作为后续回归基线。

**Step 4: Commit**

```bash
git add test/T36-task_core_wake_path.cc test/T41-task_ref_schedule_path.cc test/T43-wait_continuation_scheduler_path.cc
git commit -m "test: add scheduler continuation path regression coverage"
```

说明：实际执行时仅在用户明确要求提交时进行。

### Task 2: 将 continuation 从 `Coroutine` 收敛到 `TaskRef`

**Files:**
- Modify: `galay-kernel/kernel/Coroutine.h`
- Modify: `galay-kernel/kernel/Coroutine.cc`

**Step 1: 修改 `TaskState`，将 continuation 存储改为 `TaskRef`**

```cpp
struct alignas(64) TaskState {
    std::optional<TaskRef> m_continuation;
};
```

保留单 waiter 语义，不在此任务引入队列。

**Step 2: 修改 `WaitResult::await_suspend(...)`，挂接 waiter 的 `TaskRef`**

```cpp
bool WaitResult::await_suspend(std::coroutine_handle<> handle) {
    auto typed = std::coroutine_handle<Coroutine::promise_type>::from_address(handle.address());
    const TaskRef& waiter = typed.promise().taskRefView();
    // 启动 child，若未完成则登记 continuation
}
```

**Step 3: 修改 child 完成路径，统一走 owner scheduler**

```cpp
void PromiseType::return_void() noexcept {
    if (state->m_continuation.has_value()) {
        TaskRef continuation = std::move(*state->m_continuation);
        state->m_continuation.reset();
        if (auto* scheduler = continuation.belongScheduler()) {
            scheduler->schedule(std::move(continuation));
        }
    }
}
```

**Step 4: 运行 continuation 相关测试**

Run: `cmake --build build --target T36-task_core_wake_path T41-task_ref_schedule_path T43-wait_continuation_scheduler_path -j4 && ./build/bin/T36-task_core_wake_path && ./build/bin/T41-task_ref_schedule_path && ./build/bin/T43-wait_continuation_scheduler_path`

Expected: 全部 PASS。

**Step 5: Commit**

```bash
git add galay-kernel/kernel/Coroutine.h galay-kernel/kernel/Coroutine.cc
git commit -m "refactor: route wait continuations through task refs"
```

说明：实际执行时仅在用户明确要求提交时进行。

### Task 3: 明确单 waiter 约束并补轻量防护

**Files:**
- Modify: `galay-kernel/kernel/Coroutine.h`
- Modify: `docs/02-API参考.md`
- Modify: `docs/11-协程.md`

**Step 1: 在 `wait()` continuation 挂接处增加最小约束**

```cpp
GALAY_ASSERT(!state->m_continuation.has_value() && "single waiter only");
```

如果项目没有统一断言宏，则采用最小条件分支并在注释/文档明确单 waiter。

**Step 2: 更新 API 文档**

补充说明：

- `Coroutine::wait()` 当前是单 waiter 语义
- continuation 恢复统一经所属 scheduler

**Step 3: 运行关联测试**

Run: `cmake --build build --target T36-task_core_wake_path T43-wait_continuation_scheduler_path -j4 && ./build/bin/T36-task_core_wake_path && ./build/bin/T43-wait_continuation_scheduler_path`

Expected: PASS。

**Step 4: Commit**

```bash
git add galay-kernel/kernel/Coroutine.h docs/02-API参考.md docs/11-协程.md
git commit -m "docs: clarify single-waiter continuation semantics"
```

说明：实际执行时仅在用户明确要求提交时进行。

### Task 4: 为 remote wakeup 合并补测试桩

**Files:**
- Create: `test/T44-scheduler_wakeup_coalescing.cc`
- Modify: `test/CMakeLists.txt`

**Step 1: 写 failing test，模拟 injected queue 多次提交只触发有限唤醒**

```cpp
class WakeTrackingScheduler final : public Scheduler {
public:
    void notifyForTest() { ++notify_calls; }
    std::atomic<int> notify_calls{0};
};
```

测试目标：

- 多次 remote schedule 在单轮睡眠内只产生一次有效 wakeup
- loop 消费 wakeup 后可再次接受新的 wakeup

**Step 2: 运行测试确认失败**

Run: `cmake --build build --target T44-scheduler_wakeup_coalescing -j4 && ./build/bin/T44-scheduler_wakeup_coalescing`

Expected: FAIL，当前实现缺少 wakeup pending 状态。

**Step 3: Commit**

```bash
git add test/T44-scheduler_wakeup_coalescing.cc test/CMakeLists.txt
git commit -m "test: add wakeup coalescing regression"
```

说明：实际执行时仅在用户明确要求提交时进行。

### Task 5: 为 IO schedulers 引入 wakeup 合并状态机

**Files:**
- Modify: `galay-kernel/kernel/EpollScheduler.h`
- Modify: `galay-kernel/kernel/EpollScheduler.cc`
- Modify: `galay-kernel/kernel/KqueueScheduler.h`
- Modify: `galay-kernel/kernel/KqueueScheduler.cc`
- Modify: `galay-kernel/kernel/IOUringScheduler.h`
- Modify: `galay-kernel/kernel/IOUringScheduler.cc`

**Step 1: 增加状态字段**

```cpp
std::atomic<bool> m_sleeping{false};
std::atomic<bool> m_wakeup_pending{false};
```

**Step 2: remote enqueue 路径只在需要时 notify**

```cpp
if (m_sleeping.load(std::memory_order_acquire) &&
    !m_wakeup_pending.exchange(true, std::memory_order_acq_rel)) {
    notify();
}
```

**Step 3: loop 线程在进入/离开阻塞等待时维护状态**

```cpp
m_sleeping.store(true, std::memory_order_release);
// re-check pending work
m_sleeping.store(false, std::memory_order_release);
m_wakeup_pending.store(false, std::memory_order_release);
```

需保证 `stop()` 始终能唤醒 loop。

**Step 4: 运行 wakeup 测试与热路径测试**

Run: `cmake --build build --target T39-awaitable_hot_path_helpers T44-scheduler_wakeup_coalescing -j4 && ./build/bin/T39-awaitable_hot_path_helpers && ./build/bin/T44-scheduler_wakeup_coalescing`

Expected: PASS。

**Step 5: Commit**

```bash
git add galay-kernel/kernel/EpollScheduler.h galay-kernel/kernel/EpollScheduler.cc galay-kernel/kernel/KqueueScheduler.h galay-kernel/kernel/KqueueScheduler.cc galay-kernel/kernel/IOUringScheduler.h galay-kernel/kernel/IOUringScheduler.cc
git commit -m "perf: coalesce scheduler wakeups on injected tasks"
```

说明：实际执行时仅在用户明确要求提交时进行。

### Task 6: 给 ready loop 加预算控制

**Files:**
- Modify: `galay-kernel/kernel/IOScheduler.hpp`
- Modify: `galay-kernel/kernel/EpollScheduler.cc`
- Modify: `galay-kernel/kernel/KqueueScheduler.cc`
- Modify: `galay-kernel/kernel/IOUringScheduler.cc`
- Create: `test/T45-scheduler_ready_budget.cc`

**Step 1: 在 worker 或 scheduler 层引入 `ready_budget`**

```cpp
size_t ready_budget = GALAY_SCHEDULER_BATCH_SIZE;
```

**Step 2: 改写 `processPendingCoroutines()`，每轮只消费有限 ready tasks**

```cpp
size_t ran = 0;
while (ran < ready_budget && popNext(next)) {
    Scheduler::resume(next);
    ++ran;
}
```

**Step 3: 写测试覆盖 injected task 不被长期饿死**

测试目标：

- 本地 self-reschedule 任务存在时，injected task 仍能在预算轮转内被执行

**Step 4: 跑测试**

Run: `cmake --build build --target T45-scheduler_ready_budget -j4 && ./build/bin/T45-scheduler_ready_budget`

Expected: PASS。

**Step 5: Commit**

```bash
git add galay-kernel/kernel/IOScheduler.hpp galay-kernel/kernel/EpollScheduler.cc galay-kernel/kernel/KqueueScheduler.cc galay-kernel/kernel/IOUringScheduler.cc test/T45-scheduler_ready_budget.cc
git commit -m "perf: add ready-task budget to io schedulers"
```

说明：实际执行时仅在用户明确要求提交时进行。

### Task 7: 补 injected / wakeup benchmark

**Files:**
- Create: `benchmark/B14-scheduler_injected_wakeup.cc`
- Modify: `benchmark/CMakeLists.txt`
- Modify: `docs/05-性能测试.md`

**Step 1: 写 benchmark**

覆盖两类数据：

- remote enqueue 吞吐
- wakeup-heavy 场景下的平均任务完成延迟

**Step 2: 构建 benchmark**

Run: `cmake --build build --target B14-SchedulerInjectedWakeup -j4`

Expected: PASS。

**Step 3: 本地试跑一轮，确认输出格式稳定**

Run: `./build/bin/B14-SchedulerInjectedWakeup`

Expected: 输出可比较的 throughput / avg latency 指标。

**Step 4: Commit**

```bash
git add benchmark/B14-scheduler_injected_wakeup.cc benchmark/CMakeLists.txt docs/05-性能测试.md
git commit -m "bench: add injected wakeup scheduler benchmark"
```

说明：实际执行时仅在用户明确要求提交时进行。

### Task 8: 跑正确性与基础性能回归

**Files:**
- Modify: `docs/05-性能测试.md`
- Create: `docs/plans/2026-03-13-scheduler-v3-benchmark-results.md`

**Step 1: 跑正确性回归**

Run: `cmake --build build --target T1-coroutine_chain T25-spawn_simple T27-runtime_stress T36-task_core_wake_path T39-awaitable_hot_path_helpers T41-task_ref_schedule_path T43-wait_continuation_scheduler_path T44-scheduler_wakeup_coalescing T45-scheduler_ready_budget -j4`

Expected: 构建成功。

Run:

```bash
./build/bin/T1-coroutine_chain
./build/bin/T25-spawn_simple
./build/bin/T27-runtime_stress
./build/bin/T36-task_core_wake_path
./build/bin/T39-awaitable_hot_path_helpers
./build/bin/T41-task_ref_schedule_path
./build/bin/T43-wait_continuation_scheduler_path
./build/bin/T44-scheduler_wakeup_coalescing
./build/bin/T45-scheduler_ready_budget
```

Expected: 全部 PASS。

**Step 2: 跑 `v3` benchmark**

Run:

```bash
cmake --build build --target B1-ComputeScheduler B2-TcpServer B3-TcpClient B6-Udp B8-MpscChannel B11-TcpIovServer B12-TcpIovClient B13-Sendfile B14-SchedulerInjectedWakeup -j4
```

Expected: 构建成功。

**Step 3: 记录 `v3` 结果**

每项运行 5 轮，保存原始输出到结果文档或独立日志目录。

**Step 4: Commit**

```bash
git add docs/05-性能测试.md docs/plans/2026-03-13-scheduler-v3-benchmark-results.md
git commit -m "docs: record v3 scheduler benchmark results"
```

说明：实际执行时仅在用户明确要求提交时进行。

### Task 9: 对比基线分支与 `v3`

**Files:**
- Modify: `docs/plans/2026-03-13-scheduler-v3-benchmark-results.md`

**Step 1: 在基线分支 `cde3da1` 上创建独立 build 目录**

Run:

```bash
git worktree add .worktrees/v3-baseline cde3da1
cmake -S .worktrees/v3-baseline -B .worktrees/v3-baseline/build
```

Expected: baseline 构建目录创建成功。

**Step 2: 跑同一套 benchmark**

Run 与 Task 8 相同的 benchmark 命令和运行轮次。

Expected: 生成 baseline 原始结果。

**Step 3: 汇总对比**

文档中按以下格式记录：

```markdown
| Benchmark | Baseline | V3 | Delta |
|---|---:|---:|---:|
| B14 injected throughput | 1.00x | 1.18x | +18% |
```

同时记录：

- 平均值 / median
- 延迟类指标
- 任何退化点和推测原因

**Step 4: Commit**

```bash
git add docs/plans/2026-03-13-scheduler-v3-benchmark-results.md
git commit -m "docs: compare scheduler v3 against baseline"
```

说明：实际执行时仅在用户明确要求提交时进行。
