#ifndef GALAY_KERNEL_SLEEP_HPP
#define GALAY_KERNEL_SLEEP_HPP

#include "Timer.hpp"
#include <memory>
#include "galay-kernel/common/Concepts.h"
#include "galay-kernel/kernel/Waker.h"
#include "galay-kernel/kernel/TimerScheduler.h"

namespace galay::kernel
{

class SleepTimer final: public Timer
{
public:
    using ptr = std::shared_ptr<SleepTimer>;
    template<concepts::ChronoDuration Duration>
    SleepTimer(Duration duration)
        :Timer(duration) {}
    void setWaker(Waker waker) { m_waker = waker; }
    void handleTimeout() override {  m_waker.wakeUp(); Timer::handleTimeout(); }

private:
    Waker m_waker;
};

struct SleepAwaitable
{
    SleepTimer::ptr m_timer;

    template<concepts::ChronoDuration Duration>
    SleepAwaitable(Duration duration)
        :m_timer(std::make_shared<SleepTimer>(duration)) {}

    bool await_ready() { return false; }

    bool await_suspend(std::coroutine_handle<> handle) {
        m_timer->setWaker(Waker(handle));
        // 使用全局 TimerScheduler 而不是 IOScheduler 的定时器
        if(!TimerScheduler::getInstance()->addTimer(m_timer)) {
            return false;
        }
        return true;
    }

    void await_resume() {}

};

template<concepts::ChronoDuration Duration>
SleepAwaitable sleep(Duration duration) {
    return SleepAwaitable(duration);
}

}
#endif
