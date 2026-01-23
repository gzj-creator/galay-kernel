#include "Coroutine.h"
#include "Scheduler.hpp"

namespace galay::kernel
{

// PromiseType implementation
Coroutine PromiseType::get_return_object() noexcept
{
    m_coroutine = Coroutine(std::coroutine_handle<PromiseType>::from_promise(*this));
    return m_coroutine;
}

std::suspend_always PromiseType::yield_value(ReSchedulerType flag) noexcept
{
    if(flag) {
        m_coroutine.m_data->m_scheduler->spawn(m_coroutine);
    }
    return {};
}

void PromiseType::return_void() noexcept {
    m_coroutine.m_data->m_done.store(true, std::memory_order_relaxed);

    if( m_coroutine.m_data->m_next.has_value() ) {
        m_coroutine.m_data->m_next->m_data->m_handle.resume();
    }
}

bool WaitResult::await_suspend(std::coroutine_handle<> handle)
{
    auto wait_co = std::coroutine_handle<Coroutine::promise_type>::from_address(handle.address()).promise().getCoroutine();
    wait_co.belongScheduler()->spawnImmidiately(m_co);
    if(m_co.done()) return false;
    m_co.m_data->m_next = wait_co;
    return true;
}

bool SpawnAwaitable::await_suspend(std::coroutine_handle<PromiseType> handle) noexcept
{
    // 从当前协程获取调度器
    auto& promise = handle.promise();
    auto current_coro = promise.getCoroutine();
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
    : m_data(std::make_shared<CoroutineData>())
{
    m_data->m_handle = handle;
}

Coroutine::Coroutine(Coroutine&& other) noexcept
    : m_data(std::move(other.m_data))
{
    other.m_data.reset();
}

Coroutine::Coroutine(const Coroutine& other) noexcept
    : m_data(other.m_data)
{
}

Coroutine& Coroutine::operator=(Coroutine&& other) noexcept
{
    if (this != &other) {
        m_data = std::move(other.m_data);
        other.m_data.reset();
    }
    return *this;
}

Coroutine& Coroutine::operator=(const Coroutine& other) noexcept
{
    if (this != &other) {
        m_data = other.m_data;
    }
    return *this;
}

void Coroutine::resume()
{
    if (m_data && m_data->m_handle && m_data->m_scheduler) {
        m_data->m_scheduler->spawn(*this);
    }
}

Scheduler *Coroutine::belongScheduler() const
{
    return m_data->m_scheduler;
}

void Coroutine::belongScheduler(Scheduler* scheduler)
{
    m_data->m_scheduler = scheduler;
}

std::thread::id Coroutine::threadId() const
{
    return m_data->m_threadId;
}

void Coroutine::threadId(std::thread::id id)
{
    m_data->m_threadId = id;
}

bool Coroutine::done() const
{
    if(m_data) {
        return m_data->m_done.load(std::memory_order_relaxed);
    }
    return true;
}

WaitResult Coroutine::wait()
{
    return WaitResult(*this);
}

}