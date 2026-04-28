#include "waker.h"
#include "scheduler.hpp"

namespace galay::kernel {


Waker::Waker(TaskRef task) noexcept
    : m_task(std::move(task))
{
}

Scheduler* Waker::getScheduler()
{
    return m_task.belongScheduler();
}

void Waker::wakeUp()
{
    detail::requestTaskResume(m_task);
}

}
