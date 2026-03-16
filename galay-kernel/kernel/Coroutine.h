/**
 * @file Coroutine.h
 * @brief Legacy coroutine compatibility wrappers for galay-kernel
 */

#ifndef GALAY_KERNEL_COROUTINE_H
#define GALAY_KERNEL_COROUTINE_H

#include "Task.h"
#include <coroutine>
#include <utility>

namespace galay::kernel
{

class Coroutine;
class Waker;
class PromiseType;
struct WaitResult;
struct SpawnAwaitable;

namespace detail
{

bool spawnCoroutine(Scheduler* scheduler, Coroutine co) noexcept;
bool spawnCoroutineImmediately(Scheduler* scheduler, Coroutine co) noexcept;
struct CoroutineAccess;

} // namespace detail

class Coroutine
{
    friend struct WaitResult;

public:
    using promise_type = PromiseType;

    friend class PromiseType;
    friend class Waker;
    friend class Scheduler;
    template <typename T>
    friend class MpscChannel;
    friend struct detail::CoroutineAccess;

    Coroutine() noexcept = default;
    explicit Coroutine(std::coroutine_handle<promise_type> handle) noexcept;
    Coroutine(Coroutine&& other) noexcept;
    Coroutine(const Coroutine& other) noexcept;

    Coroutine& operator=(Coroutine&& other) noexcept;
    Coroutine& operator=(const Coroutine& other) noexcept;

    bool isValid() const { return m_task.isValid(); }
    bool done() const;
    Coroutine& then(Coroutine co) &;
    Coroutine&& then(Coroutine co) &&;
    WaitResult wait();

private:
    explicit Coroutine(TaskRef task) noexcept;

    TaskRef m_task;
};

namespace detail
{

struct CoroutineAccess
{
    static Scheduler* belongScheduler(const Coroutine& co)
    {
        auto* state = co.m_task.state();
        return state ? state->m_scheduler : nullptr;
    }

    static void setScheduler(Coroutine& co, Scheduler* scheduler)
    {
        auto* state = co.m_task.state();
        if (!state) {
            return;
        }
        state->m_scheduler = scheduler;
        if (state->m_then.has_value() && state->m_then->belongScheduler() == nullptr) {
            detail::setTaskScheduler(*state->m_then, scheduler);
        }
    }

    static TaskRef taskRef(const Coroutine& co) noexcept
    {
        return co.m_task;
    }

    static TaskRef detachTask(Coroutine&& co) noexcept
    {
        return std::move(co.m_task);
    }

    static std::thread::id threadId(const Coroutine& co)
    {
        return detail::schedulerThreadId(belongScheduler(co));
    }

    static void resume(Coroutine& co)
    {
        auto* state = co.m_task.state();
        if (state && state->m_handle && state->m_scheduler &&
            !state->m_done.load(std::memory_order_relaxed)) {
            if (!state->m_queued.exchange(true, std::memory_order_acq_rel)) {
                detail::scheduleTask(co.m_task);
            }
        }
    }
};

} // namespace detail

struct WaitResult
{
public:
    explicit WaitResult(Coroutine co)
        : m_co(std::move(co))
    {
    }

    bool await_ready()
    {
        return false;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle)
    {
        TaskRef waitingTask = handle.promise().taskRefView();
        detail::inheritTaskRuntime(m_co.m_task, detail::taskRuntime(waitingTask));
        auto* scheduler = waitingTask.belongScheduler();
        if (!detail::spawnCoroutineImmediately(scheduler, m_co)) {
            return false;
        }
        if (m_co.done()) {
            return false;
        }
        m_co.m_task.state()->m_next = std::move(waitingTask);
        return true;
    }

    void await_resume() {}

private:
    Coroutine m_co;
};

struct SpawnAwaitable
{
public:
    explicit SpawnAwaitable(TaskRef task)
        : m_task(std::move(task))
    {
    }

    bool await_ready() const noexcept
    {
        return false;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept
    {
        TaskRef currentTask = handle.promise().taskRefView();
        detail::inheritTaskRuntime(m_task, detail::taskRuntime(currentTask));
        if (m_task.belongScheduler() == nullptr) {
            detail::setTaskScheduler(m_task, currentTask.belongScheduler());
        }
        detail::scheduleTask(m_task);
        return false;
    }

    void await_resume() noexcept {}

private:
    TaskRef m_task;
};

inline SpawnAwaitable spawn(Coroutine co)
{
    return SpawnAwaitable(detail::CoroutineAccess::detachTask(std::move(co)));
}

template <typename T>
inline SpawnAwaitable spawn(Task<T> task)
{
    return SpawnAwaitable(detail::TaskAccess::taskRef(task));
}

class PromiseType
{
public:
    using ReSchedulerType = bool;

    int get_return_object_on_alloaction_failure() noexcept { return -1; }
    Coroutine get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always yield_value(ReSchedulerType flag) noexcept;
    std::suspend_never final_suspend() noexcept { return {}; }
    void unhandled_exception() noexcept;
    void return_void() noexcept;

    Coroutine getCoroutine() { return m_coroutine; }
    Coroutine& coroutineRef() noexcept { return m_coroutine; }
    const Coroutine& coroutineRef() const noexcept { return m_coroutine; }
    const TaskRef& taskRefView() const noexcept { return m_coroutine.m_task; }

private:
    Coroutine m_coroutine;
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_COROUTINE_H
