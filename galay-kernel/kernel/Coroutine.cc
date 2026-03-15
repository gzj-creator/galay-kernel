#include "Coroutine.h"
#include "Scheduler.hpp"

namespace galay::kernel
{

namespace
{

thread_local Runtime* g_currentRuntime = nullptr;

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

Runtime* currentRuntime() noexcept
{
    return g_currentRuntime;
}

Runtime* swapCurrentRuntime(Runtime* runtime) noexcept
{
    Runtime* previous = g_currentRuntime;
    g_currentRuntime = runtime;
    return previous;
}

bool scheduleTask(const TaskRef& task) noexcept
{
    auto* scheduler = task.belongScheduler();
    return scheduler != nullptr && scheduler->schedule(task);
}

bool scheduleTaskDeferred(const TaskRef& task) noexcept
{
    auto* scheduler = task.belongScheduler();
    return scheduler != nullptr && scheduler->scheduleDeferred(task);
}

bool spawnCoroutine(Scheduler* scheduler, Coroutine co) noexcept
{
    return scheduler != nullptr && scheduler->spawn(std::move(co));
}

bool spawnCoroutineImmediately(Scheduler* scheduler, Coroutine co) noexcept
{
    return scheduler != nullptr && scheduler->spawnImmidiately(std::move(co));
}

std::thread::id schedulerThreadId(Scheduler* scheduler) noexcept
{
    return scheduler ? scheduler->threadId() : std::thread::id{};
}

void completeTaskState(const TaskRef& task) noexcept
{
    auto* state = task.state();
    if (!state) {
        return;
    }

    state->m_done.store(true, std::memory_order_release);

    if (state->m_then.has_value()) {
        TaskRef next_then = std::move(*state->m_then);
        state->m_then.reset();
        if (auto* scheduler = next_then.belongScheduler()) {
            scheduler->schedule(std::move(next_then));
        }
    }

    if (state->m_next.has_value()) {
        TaskRef next = std::move(*state->m_next);
        state->m_next.reset();
        if (auto* scheduler = next.belongScheduler()) {
            scheduler->schedule(std::move(next));
        }
    }
}

} // namespace detail

TaskRef::TaskRef(TaskState* state, bool retainRef) noexcept
    : m_state(state)
{
    if (retainRef) {
        retain();
    }
}

TaskRef::TaskRef(const TaskRef& other) noexcept
    : m_state(other.m_state)
{
    retain();
}

TaskRef::TaskRef(TaskRef&& other) noexcept
    : m_state(other.m_state)
{
    other.m_state = nullptr;
}

TaskRef::~TaskRef()
{
    release();
}

TaskRef& TaskRef::operator=(const TaskRef& other) noexcept
{
    if (this != &other) {
        release();
        m_state = other.m_state;
        retain();
    }
    return *this;
}

TaskRef& TaskRef::operator=(TaskRef&& other) noexcept
{
    if (this != &other) {
        release();
        m_state = other.m_state;
        other.m_state = nullptr;
    }
    return *this;
}

Scheduler* TaskRef::belongScheduler() const noexcept
{
    return m_state ? m_state->m_scheduler : nullptr;
}

void TaskRef::retain() noexcept
{
    if (m_state) {
        m_state->m_refs.fetch_add(1, std::memory_order_relaxed);
    }
}

void TaskRef::release() noexcept
{
    if (!m_state) {
        return;
    }

    auto* state = m_state;
    m_state = nullptr;
    if (state->m_refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete state;
    }
}

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
