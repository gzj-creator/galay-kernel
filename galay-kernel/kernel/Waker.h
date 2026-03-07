#ifndef GALAY_KERNEL_WAKER_H
#define GALAY_KERNEL_WAKER_H

#include "Coroutine.h"

namespace galay::kernel
{

class Waker
{
public:
    Waker() = default;
    explicit Waker(TaskRef task) noexcept;
    explicit Waker(std::coroutine_handle<Coroutine::promise_type> handle) noexcept;
    Waker(std::coroutine_handle<> handle) noexcept;
    Waker(const Waker& other) = default;
    Waker(Waker&& waker) noexcept = default;
    Waker& operator=(const Waker& other) = default;
    Waker& operator=(Waker&& other) noexcept = default;

    Scheduler* getScheduler();

    void wakeUp();

private:
    TaskRef m_task;
};



}

#endif
