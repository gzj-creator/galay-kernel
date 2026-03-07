#include "Waker.h"
#include "Scheduler.hpp"

namespace galay::kernel {


Waker::Waker(TaskRef task) noexcept
    : m_task(std::move(task))
{
}

Waker::Waker(std::coroutine_handle<Coroutine::promise_type> handle) noexcept
    : m_task(handle.promise().taskRefView())
{
}

Waker::Waker(std::coroutine_handle<> handle) noexcept
    : Waker(std::coroutine_handle<Coroutine::promise_type>::from_address(handle.address()))
{
}

Scheduler* Waker::getScheduler()
{
    return m_task.belongScheduler();
}

void Waker::wakeUp()
{
    auto* state = m_task.state();
    if (!state || !state->m_handle || !state->m_scheduler || state->m_done.load(std::memory_order_relaxed)) {
        return;
    }

    if (!state->m_queued.exchange(true, std::memory_order_acq_rel)) {
        state->m_scheduler->spawn(Coroutine(m_task));
    }
}

}
