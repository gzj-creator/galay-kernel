#ifndef GALAY_KERNEL_WAKER_H
#define GALAY_KERNEL_WAKER_H

#include <concepts>
#include "Task.h"

namespace galay::kernel
{

class Waker
{
public:
    Waker() = default;
    explicit Waker(TaskRef task) noexcept;
    template <typename Promise>
    requires requires(const Promise& promise) {
        { promise.taskRefView() } -> std::same_as<const TaskRef&>;
    }
    explicit Waker(std::coroutine_handle<Promise> handle) noexcept
        : m_task(handle.promise().taskRefView())
    {
    }
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
