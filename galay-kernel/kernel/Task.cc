#include "Task.h"
#include "Scheduler.hpp"

namespace galay::kernel
{

namespace
{

thread_local Runtime* g_currentRuntime = nullptr;

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

bool scheduleTaskImmediately(const TaskRef& task) noexcept
{
    auto* scheduler = task.belongScheduler();
    return scheduler != nullptr && scheduler->scheduleImmediately(task);
}

bool requestTaskResume(const TaskRef& task) noexcept
{
    auto* state = task.state();
    if (!state || !state->m_handle || !state->m_scheduler ||
        state->m_done.load(std::memory_order_relaxed)) {
        return false;
    }

    if (state->m_queued.exchange(true, std::memory_order_acq_rel)) {
        return false;
    }
    return state->m_scheduler->schedule(task);
}

std::thread::id schedulerThreadId(Scheduler* scheduler) noexcept
{
    return scheduler ? scheduler->threadId() : std::thread::id{};
}

void attachTaskContinuation(const TaskRef& task, TaskRef next) noexcept
{
    auto* state = task.state();
    if (state == nullptr) {
        return;
    }

    inheritTaskRuntime(next, state->m_runtime);
    if (next.belongScheduler() == nullptr && state->m_scheduler != nullptr) {
        setTaskScheduler(next, state->m_scheduler);
    }
    state->m_then = std::move(next);
}

void completeTaskState(const TaskRef& task) noexcept
{
    auto* state = task.state();
    if (!state) {
        return;
    }

    state->m_done.store(true, std::memory_order_release);

    if (state->m_then.has_value()) {
        TaskRef nextThen = std::move(*state->m_then);
        state->m_then.reset();
        if (auto* scheduler = nextThen.belongScheduler()) {
            scheduler->schedule(std::move(nextThen));
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

} // namespace galay::kernel
