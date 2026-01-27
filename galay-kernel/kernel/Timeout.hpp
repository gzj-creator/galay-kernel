#ifndef GALAY_KERNEL_TIMEOUT_HPP
#define GALAY_KERNEL_TIMEOUT_HPP

#include "galay-kernel/common/Error.h"
#include "galay-kernel/common/Timer.hpp"
#include "Waker.h"
#include "Scheduler.hpp"
#include <memory>
#include <expected>

namespace galay::kernel 
{

class TimeoutTimer final: public Timer
{
public:
    using ptr = std::shared_ptr<TimeoutTimer>;

    template<typename Rep, typename Period>
    TimeoutTimer(std::chrono::duration<Rep, Period> duration)
        : Timer(duration) {}

    void setWaker(Waker waker) { m_waker = waker; }

    void handleTimeout() override {
        if(!cancelled() && !done()) {
            m_flag |= TIMEOUT;
            m_waker.wakeUp();
        }
        Timer::handleTimeout();
    }

    bool timeouted() { return m_flag & TIMEOUT; }

private:
    Waker m_waker;
};

template<typename Awaitable>
struct WithTimeout;

/**
* @brief CRTP 基类，为 Awaitable 提供 timeout() 方法
*/
template<typename Derived>
struct TimeoutSupport {
    auto timeout(std::chrono::milliseconds t) && {
        return WithTimeout<Derived>{std::move(static_cast<Derived&>(*this)), t};
    }

    auto timeout(std::chrono::milliseconds t) & {
        return WithTimeout<Derived>{static_cast<Derived&>(*this), t};
    }
};

/**
 * @brief 超时包装器
 *
 * @details 对于 io_uring，使用独立的 timeout 操作；对于 epoll/kqueue，使用 timerfd。
 * 定时器状态存储在 IOController 中，生命周期与 TcpSocket 绑定。
 */
template<typename Awaitable>
struct WithTimeout {
    Awaitable m_inner;
    TimeoutTimer::ptr m_timer;

    WithTimeout(Awaitable&& inner, std::chrono::milliseconds timeout)
        : m_inner(std::move(inner)), m_timer(std::make_shared<TimeoutTimer>(timeout)) {}

    WithTimeout(Awaitable& inner, std::chrono::milliseconds timeout)
        : m_inner(std::move(inner)), m_timer(std::make_shared<TimeoutTimer>(timeout)) {}

    bool await_ready() { return m_inner.await_ready(); }

    bool await_suspend(std::coroutine_handle<> handle) {
        bool suspended = m_inner.await_suspend(handle);
        if (!suspended) {
            return false;
        }
        auto waker = Waker(handle);
        m_timer->setWaker(Waker(handle));
        waker.getScheduler()->addTimer(m_timer);
        return true;
    }

    auto await_resume() -> decltype(m_inner.await_resume()) {
        using ResultType = decltype(m_inner.await_resume());
        // 检查是否超时
        if (m_timer->timeouted()) [[unlikely]] {
            // 所有等待体都有 m_result 成员，设置为超时错误
            if constexpr (requires { m_inner.m_result; }) {
                m_inner.m_result = std::unexpected(IOError(kTimeout, 0));
            }
        } else {
            m_timer->cancel();
        }
        return m_inner.await_resume();
    }

};

}



#endif