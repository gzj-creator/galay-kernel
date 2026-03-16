#include "Coroutine.h"
#include "Scheduler.hpp"

namespace galay::kernel
{

namespace
{

void attachThenContinuation(TaskState* state, Coroutine co)
{
    if (state == nullptr) {
        return;
    }

    TaskRef next = detail::CoroutineAccess::taskRef(co);
    detail::inheritTaskRuntime(next, state->m_runtime);
    if (next.belongScheduler() == nullptr && state->m_scheduler != nullptr) {
        detail::setTaskScheduler(next, state->m_scheduler);
    }
    state->m_then = std::move(next);
}

} // namespace

namespace detail
{

bool spawnCoroutine(Scheduler* scheduler, Coroutine co) noexcept
{
    return scheduler != nullptr && scheduler->schedule(detail::CoroutineAccess::detachTask(std::move(co)));
}

bool spawnCoroutineImmediately(Scheduler* scheduler, Coroutine co) noexcept
{
    return scheduler != nullptr && scheduler->scheduleImmediately(detail::CoroutineAccess::detachTask(std::move(co)));
}

} // namespace detail

Coroutine PromiseType::get_return_object() noexcept
{
    m_coroutine = Coroutine(std::coroutine_handle<PromiseType>::from_promise(*this));
    if (Runtime* runtime = detail::currentRuntime()) {
        detail::inheritTaskRuntime(m_coroutine.m_task, runtime);
    }
    return m_coroutine;
}

std::suspend_always PromiseType::yield_value(ReSchedulerType flag) noexcept
{
    if (flag) {
        detail::scheduleTaskDeferred(m_coroutine.m_task);
    }
    return {};
}

void PromiseType::unhandled_exception() noexcept
{
    detail::completeTaskState(m_coroutine.m_task);
}

void PromiseType::return_void() noexcept
{
    detail::completeTaskState(m_coroutine.m_task);
}

Coroutine::Coroutine(std::coroutine_handle<promise_type> handle) noexcept
    : m_task(new TaskState(handle), false)
{
}

Coroutine::Coroutine(TaskRef task) noexcept
    : m_task(std::move(task))
{
}

Coroutine::Coroutine(Coroutine&& other) noexcept
    : m_task(std::move(other.m_task))
{
}

Coroutine::Coroutine(const Coroutine& other) noexcept
    : m_task(other.m_task)
{
}

Coroutine& Coroutine::operator=(Coroutine&& other) noexcept
{
    if (this != &other) {
        m_task = std::move(other.m_task);
    }
    return *this;
}

Coroutine& Coroutine::operator=(const Coroutine& other) noexcept
{
    if (this != &other) {
        m_task = other.m_task;
    }
    return *this;
}

bool Coroutine::done() const
{
    auto* state = m_task.state();
    return !state || state->m_done.load(std::memory_order_acquire);
}

Coroutine& Coroutine::then(Coroutine co) &
{
    attachThenContinuation(m_task.state(), std::move(co));
    return *this;
}

Coroutine&& Coroutine::then(Coroutine co) &&
{
    attachThenContinuation(m_task.state(), std::move(co));
    return std::move(*this);
}

WaitResult Coroutine::wait()
{
    return WaitResult(*this);
}

} // namespace galay::kernel
