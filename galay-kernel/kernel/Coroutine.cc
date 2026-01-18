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

void PromiseType::return_void() const noexcept {
    if( m_coroutine.m_data->m_next.has_value() ) {
        m_coroutine.m_data->m_next->m_data->m_handle.resume();
    }
}

bool WaitResult::await_suspend(std::coroutine_handle<> handle)
{
    auto wait_co = std::coroutine_handle<Coroutine::promise_type>::from_address(handle.address()).promise().getCoroutine();
    m_co.m_data->m_next = wait_co;
    wait_co.belongScheduler()->spawn(std::move(m_co));
    return true;
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
    if (m_data && m_data->m_handle && !m_data->m_handle.done() && m_data->m_scheduler) {
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

WaitResult Coroutine::wait()
{
    return WaitResult(*this);
}

}