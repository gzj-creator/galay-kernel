#include "Coroutine.h"
#include "Scheduler.hpp"

namespace galay::kernel
{

TaskRef::TaskRef(TaskState* state, bool retain_ref) noexcept
    : m_state(state)
{
    if (retain_ref) {
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

// PromiseType implementation
Coroutine PromiseType::get_return_object() noexcept
{
    m_coroutine = Coroutine(std::coroutine_handle<PromiseType>::from_promise(*this));
    return m_coroutine;
}

std::suspend_always PromiseType::yield_value(ReSchedulerType flag) noexcept
{
    if(flag) {
        m_coroutine.m_task.belongScheduler()->spawnDeferred(m_coroutine);
    }
    return {};
}

void PromiseType::return_void() noexcept {
    auto* state = m_coroutine.m_task.state();
    state->m_done.store(true, std::memory_order_relaxed);

    if (state->m_next.has_value()) {
        state->m_next->m_task.state()->m_handle.resume();
    }
}

bool WaitResult::await_suspend(std::coroutine_handle<> handle)
{
    auto typed_handle = std::coroutine_handle<Coroutine::promise_type>::from_address(handle.address());
    auto& wait_co = typed_handle.promise().coroutineRef();
    wait_co.belongScheduler()->spawnImmidiately(m_co);
    if(m_co.done()) return false;
    m_co.m_task.state()->m_next = wait_co;
    return true;
}

bool SpawnAwaitable::await_suspend(std::coroutine_handle<PromiseType> handle) noexcept
{
    // 从当前协程获取调度器
    auto& promise = handle.promise();
    auto& current_coro = promise.coroutineRef();
    auto scheduler = current_coro.belongScheduler();

    if (scheduler) {
        // 将新协程spawn到当前调度器
        scheduler->spawn(std::move(m_co));
    }

    // 不挂起当前协程，立即继续执行
    return false;
}


// Coroutine implementation
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

void Coroutine::resume()
{
    auto* state = m_task.state();
    if (state && state->m_handle && state->m_scheduler) {
        if (!state->m_queued.exchange(true, std::memory_order_acq_rel)) {
            state->m_scheduler->spawn(*this);
        }
    }
}

Scheduler *Coroutine::belongScheduler() const
{
    auto* state = m_task.state();
    return state ? state->m_scheduler : nullptr;
}

void Coroutine::belongScheduler(Scheduler* scheduler)
{
    auto* state = m_task.state();
    if (state) {
        state->m_scheduler = scheduler;
    }
}

std::thread::id Coroutine::threadId() const
{
    auto* state = m_task.state();
    return (state && state->m_scheduler) ? state->m_scheduler->threadId() : std::thread::id{};
}

bool Coroutine::done() const
{
    auto* state = m_task.state();
    if(state) {
        return state->m_done.load(std::memory_order_relaxed);
    }
    return true;
}

WaitResult Coroutine::wait()
{
    return WaitResult(*this);
}

}
