#ifndef GALAY_KERNEL_WAKER_H
#define GALAY_KERNEL_WAKER_H

#include "Coroutine.h"

namespace galay::kernel
{

class Waker
{
public:
    Waker() {}
    Waker(std::coroutine_handle<> handle);
    Waker(const Waker& other);
    Waker(Waker&& waker);
    Waker& operator=(const Waker& other);
    Waker& operator=(Waker&& other);

    Scheduler* getScheduler();

    void wakeUp();

private:
    Coroutine m_coroutine;
};



}

#endif