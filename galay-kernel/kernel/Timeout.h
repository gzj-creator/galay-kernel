/**
 * @file Timeout.h
 * @brief 超时支持模块
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details 提供 Awaitable 的超时包装器和 CRTP 基类。
 * 使用方式：
 * @code
 * auto result = co_await socket.recv(buffer, size).timeout(5s);
 * if (!result && IOError::contains(result.error().code(), kTimeout)) {
 *     // 处理超时
 * }
 * @endcode
 *
 * 核心逻辑：
 * - Timer 先触发 → 标记 IOController 超时 → 唤醒协程
 * - IO 先完成 → 取消 Timer → 正常唤醒
 */

#ifndef GALAY_KERNEL_TIMEOUT_H
#define GALAY_KERNEL_TIMEOUT_H

#include "galay-kernel/common/Defn.hpp"
#include "galay-kernel/common/Error.h"
#include "Scheduler.h"
#include "Waker.h"
#include <chrono>
#include <coroutine>
#include <expected>

#if defined(USE_EPOLL) || defined(USE_IOURING)
#include <sys/timerfd.h>
#include <unistd.h>
#include <cstring>
#endif

#ifdef USE_KQUEUE
#include <sys/event.h>
#endif

namespace galay::kernel
{

// 前置声明
template<typename Awaitable>
struct WithTimeout;
class IOScheduler;

/**
 * @brief 定时器控制器（用于调度器识别定时器事件）
 */
struct TimerController {
    IOController* m_io_controller = nullptr;  ///< 关联的 IO 控制器
    Waker* m_waker = nullptr;                 ///< 协程唤醒器
    uint64_t m_generation = 0;                ///< 操作代数
    bool m_cancelled = false;                 ///< 是否已取消
};

/**
 * @brief 定时器类（内部使用）
 */
class Timer
{
public:
    Timer() = default;

    ~Timer() {
#if defined(USE_EPOLL) || defined(USE_IOURING)
        if (m_fd >= 0) {
            close(m_fd);
        }
#endif
    }

    // 禁止拷贝
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    // 允许移动
    Timer(Timer&& other) noexcept
        : m_fd(other.m_fd), m_timer_ctrl(std::move(other.m_timer_ctrl)) {
        other.m_fd = -1;
    }

    Timer& operator=(Timer&& other) noexcept {
        if (this != &other) {
#if defined(USE_EPOLL) || defined(USE_IOURING)
            if (m_fd >= 0) close(m_fd);
#endif
            m_fd = other.m_fd;
            m_timer_ctrl = std::move(other.m_timer_ctrl);
            other.m_fd = -1;
        }
        return *this;
    }

    int start(std::chrono::milliseconds timeout, IOController* controller,
              Waker* waker, uint64_t generation) {
        m_timer_ctrl.m_io_controller = controller;
        m_timer_ctrl.m_waker = waker;
        m_timer_ctrl.m_generation = generation;
        m_timer_ctrl.m_cancelled = false;

#if defined(USE_EPOLL) || defined(USE_IOURING)
        m_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (m_fd < 0) {
            return -1;
        }

        struct itimerspec its{};

        // 直接分别转换，避免大数除法
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(timeout);
        auto nsecs = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - secs);
        its.it_value.tv_sec = secs.count();
        its.it_value.tv_nsec = nsecs.count();
        if (its.it_value.tv_sec == 0 && its.it_value.tv_nsec < 1) {
            its.it_value.tv_nsec = 1;
        }

        if (timerfd_settime(m_fd, 0, &its, nullptr) < 0) {
            close(m_fd);
            m_fd = -1;
            return -1;
        }
#elif defined(USE_KQUEUE)
        // kqueue 使用 EVFILT_TIMER，不需要 fd
        m_timeout_ms = timeout.count();
#else
        (void)timeout;
#endif
        return 0;
    }

    void cancel() { m_timer_ctrl.m_cancelled = true; }
    bool isCancelled() const { return m_timer_ctrl.m_cancelled; }
    int fd() const { return m_fd; }
    TimerController* timerController() { return &m_timer_ctrl; }

#ifdef USE_KQUEUE
    int64_t timeoutMs() const { return m_timeout_ms; }
#endif

private:
    int m_fd = -1;
    TimerController m_timer_ctrl;
#ifdef USE_KQUEUE
    int64_t m_timeout_ms = 0;
#endif
};

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
    std::chrono::milliseconds m_timeout;

    WithTimeout(Awaitable&& inner, std::chrono::milliseconds timeout)
        : m_inner(std::move(inner)), m_timeout(timeout) {}

    WithTimeout(Awaitable& inner, std::chrono::milliseconds timeout)
        : m_inner(std::move(inner)), m_timeout(timeout) {}

    bool await_ready() { return m_inner.await_ready(); }

    bool await_suspend(std::coroutine_handle<> handle) {
        bool suspended = m_inner.await_suspend(handle);
        if (!suspended) {
            return false;
        }

        // 设置超时时间到 IOController
        auto* controller = m_inner.m_controller;
        controller->m_timer_cancelled = false;

#ifdef USE_IOURING
        // io_uring: 使用独立的 timeout 操作
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(m_timeout);
        auto nsecs = std::chrono::duration_cast<std::chrono::nanoseconds>(m_timeout - secs);
        controller->m_timeout_ts.tv_sec = secs.count();
        controller->m_timeout_ts.tv_nsec = nsecs.count();
        if (controller->m_timeout_ts.tv_sec == 0 && controller->m_timeout_ts.tv_nsec < 1) {
            controller->m_timeout_ts.tv_nsec = 1;
        }
        // 注册独立超时（使用特殊标记 3 表示 IO 超时）
        m_inner.m_scheduler->addTimer(-1, reinterpret_cast<TimerController*>(controller));
#elif defined(USE_EPOLL)
        // epoll: 创建 timerfd 并存储在 IOController 中
        controller->m_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (controller->m_timer_fd < 0) {
            // 定时器创建失败，继续等待 IO（不超时）
            return true;
        }

        struct itimerspec its{};
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(m_timeout);
        auto nsecs = std::chrono::duration_cast<std::chrono::nanoseconds>(m_timeout - secs);
        its.it_value.tv_sec = secs.count();
        its.it_value.tv_nsec = nsecs.count();
        if (its.it_value.tv_sec == 0 && its.it_value.tv_nsec < 1) {
            its.it_value.tv_nsec = 1;
        }

        if (timerfd_settime(controller->m_timer_fd, 0, &its, nullptr) < 0) {
            close(controller->m_timer_fd);
            controller->m_timer_fd = -1;
            return true;
        }

        // 创建 TimerController 用于注册（存储在 WithTimeout 中，生命周期足够）
        m_timer_ctrl.m_io_controller = controller;
        m_timer_ctrl.m_waker = &m_inner.m_waker;
        m_timer_ctrl.m_generation = controller->m_generation;
        m_timer_ctrl.m_cancelled = false;

        m_inner.m_scheduler->addTimer(controller->m_timer_fd, &m_timer_ctrl);
#elif defined(USE_KQUEUE)
        // kqueue: 使用 EVFILT_TIMER，不需要 timerfd
        m_timer_ctrl.m_io_controller = controller;
        m_timer_ctrl.m_waker = &m_inner.m_waker;
        m_timer_ctrl.m_generation = controller->m_generation;
        m_timer_ctrl.m_cancelled = false;

        // 存储超时时间（毫秒）
        controller->m_timeout_ms = m_timeout.count();

        m_inner.m_scheduler->addTimer(controller->m_timeout_ms, &m_timer_ctrl);
#endif
        return true;
    }

    auto await_resume() -> decltype(m_inner.await_resume()) {
        using ResultType = decltype(m_inner.await_resume());

        auto* controller = m_inner.m_controller;

        // 标记定时器已取消
        controller->m_timer_cancelled = true;
#ifdef USE_EPOLL
        m_timer_ctrl.m_cancelled = true;
        // 关闭 timerfd
        if (controller->m_timer_fd >= 0) {
            close(controller->m_timer_fd);
            controller->m_timer_fd = -1;
        }
#elif defined(USE_KQUEUE)
        m_timer_ctrl.m_cancelled = true;
        // kqueue 定时器会自动删除（EV_ONESHOT），无需手动清理
#endif

        // 检查是否超时
        if (controller->isTimedOut()) [[unlikely]] {
            controller->removeAwaitable();
            return ResultType(std::unexpected(IOError(kTimeout, 0)));
        }

        return m_inner.await_resume();
    }

#ifndef USE_IOURING
    TimerController m_timer_ctrl;
#endif
};

/**
 * @brief Sleep 等待体
 *
 * @details 用于协程休眠指定时间。
 * 使用方式：
 * @code
 * co_await galay::kernel::sleep(scheduler, 1000ms);
 * @endcode
 */
class SleepAwaitable {
public:
    SleepAwaitable(IOScheduler* scheduler, std::chrono::milliseconds duration)
        : m_scheduler(scheduler), m_duration(duration) {}

    bool await_ready() { return m_duration.count() <= 0; }

    bool await_suspend(std::coroutine_handle<> handle) {
        m_waker = Waker(handle);
        m_controller.fillAwaitable(IOEventType::SLEEP, this);

#ifdef USE_IOURING
        // io_uring: 使用原生 timeout，通过 addTimer(-1, ...) 触发
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(m_duration);
        auto nsecs = std::chrono::duration_cast<std::chrono::nanoseconds>(m_duration - secs);
        m_controller.m_timeout_ts.tv_sec = secs.count();
        m_controller.m_timeout_ts.tv_nsec = nsecs.count();
        if (m_controller.m_timeout_ts.tv_sec == 0 && m_controller.m_timeout_ts.tv_nsec < 1) {
            m_controller.m_timeout_ts.tv_nsec = 1;
        }
        // timer_fd = -1 表示使用 io_uring 原生超时，传递 IOController*
        return m_scheduler->addTimer(-1, reinterpret_cast<TimerController*>(&m_controller)) == 0;
#elif defined(USE_EPOLL)
        // epoll: 使用 timerfd
        if (m_timer.start(m_duration, &m_controller, &m_waker, m_controller.m_generation) < 0) {
            return false;
        }
        m_scheduler->addTimer(m_timer.fd(), m_timer.timerController());
        return true;
#elif defined(USE_KQUEUE)
        // kqueue: 使用 EVFILT_TIMER
        if (m_timer.start(m_duration, &m_controller, &m_waker, m_controller.m_generation) < 0) {
            return false;
        }
        m_scheduler->addTimer(m_timer.timeoutMs(), m_timer.timerController());
        return true;
#else
        return false;
#endif
    }

    void await_resume() {
        // Sleep 完成，无返回值
    }

    // 供调度器访问
    Waker m_waker;

private:
    IOScheduler* m_scheduler;
    std::chrono::milliseconds m_duration;
    IOController m_controller;
#ifndef USE_IOURING
    Timer m_timer;
#endif
};

/**
 * @brief 创建 sleep 等待体
 * @param scheduler IO调度器
 * @param duration 休眠时间
 * @return SleepAwaitable 等待体
 *
 * @code
 * co_await galay::kernel::sleep(scheduler, 1s);
 * co_await galay::kernel::sleep(scheduler, 500ms);
 * @endcode
 */
inline SleepAwaitable sleep(IOScheduler* scheduler, std::chrono::milliseconds duration) {
    return SleepAwaitable(scheduler, duration);
}

} // namespace galay::kernel

#endif // GALAY_KERNEL_TIMEOUT_H
