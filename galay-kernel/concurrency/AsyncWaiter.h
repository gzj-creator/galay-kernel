/**
 * @file AsyncWaiter.h
 * @brief 异步等待器
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details 提供跨线程的协程等待机制，支持返回结果。
 * 典型用于 ComputeScheduler 计算任务完成后通知 IO 协程。
 *
 * 使用方式：
 * @code
 * // IO 协程中
 * AsyncWaiter<int> waiter;
 * computeScheduler.spawn(computeTask(&waiter));
 * int result = co_await waiter.wait();  // 挂起等待
 *
 * // 计算协程中
 * Coroutine computeTask(AsyncWaiter<int>* waiter) {
 *     int result = heavyCompute();
 *     waiter->notify(result);  // 唤醒等待的协程
 *     co_return;
 * }
 * @endcode
 */

#ifndef GALAY_KERNEL_ASYNC_WAITER_H
#define GALAY_KERNEL_ASYNC_WAITER_H

#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/Scheduler.hpp"
#include "galay-kernel/kernel/Waker.h"
#include <atomic>
#include <optional>
#include <coroutine>

namespace galay::kernel
{

template<typename T>
class AsyncWaiter;

/**
 * @brief AsyncWaiter 的等待体
 */
template<typename T>
class AsyncWaiterAwaitable
{
public:
    explicit AsyncWaiterAwaitable(AsyncWaiter<T>* waiter) : m_waiter(waiter) {}

    bool await_ready() const noexcept;
    bool await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept;
    T await_resume() noexcept;

private:
    AsyncWaiter<T>* m_waiter;
};

/**
 * @brief void 特化的等待体
 */
template<>
class AsyncWaiterAwaitable<void>
{
public:
    explicit AsyncWaiterAwaitable(AsyncWaiter<void>* waiter) : m_waiter(waiter) {}

    bool await_ready() const noexcept;
    bool await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept;
    void await_resume() noexcept {}

private:
    AsyncWaiter<void>* m_waiter;
};

/**
 * @brief 异步等待器
 *
 * @tparam T 结果类型
 *
 * @details 用于跨线程协程同步。一个协程调用 wait() 挂起，
 * 另一个线程/协程调用 notify() 设置结果并唤醒。
 *
 * @note 线程安全，每个 AsyncWaiter 实例只能使用一次
 */
template<typename T>
class AsyncWaiter
{
public:
    AsyncWaiter() = default;

    // 禁止拷贝和移动
    AsyncWaiter(const AsyncWaiter&) = delete;
    AsyncWaiter& operator=(const AsyncWaiter&) = delete;
    AsyncWaiter(AsyncWaiter&&) = delete;
    AsyncWaiter& operator=(AsyncWaiter&&) = delete;

    /**
     * @brief 等待结果
     * @return 可等待对象，用于 co_await
     */
    AsyncWaiterAwaitable<T> wait() {
        return AsyncWaiterAwaitable<T>(this);
    }

    /**
     * @brief 设置结果并唤醒等待的协程
     * @param result 结果值
     * @return true 成功通知，false 已经通知过
     * @note 会将协程 spawn 回原调度器
     */
    bool notify(T result) {
        // 防止重复通知
        bool expected = false;
        if (!m_ready.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
            return false;  // 已经通知过
        }

        m_result = std::move(result);

        // 唤醒等待的协程（如果有）
        expected = true;
        if (m_waiting.compare_exchange_strong(expected, false,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
            m_waker.wakeUp();
        }
        return true;
    }

    /**
     * @brief 检查是否正在等待
     * @return true 如果有协程在等待
     */
    bool isWaiting() const {
        return m_waiting.load(std::memory_order_acquire);
    }

    /**
     * @brief 检查结果是否就绪
     * @return true 如果结果已就绪
     */
    bool isReady() const {
        return m_ready.load(std::memory_order_acquire);
    }

private:
    friend class AsyncWaiterAwaitable<T>;

    std::optional<T> m_result;                  ///< 结果
    std::atomic<bool> m_waiting{false};         ///< 是否有协程在等待
    std::atomic<bool> m_ready{false};           ///< 结果是否就绪
    Waker m_waker;
};

/**
 * @brief void 特化版本
 */
template<>
class AsyncWaiter<void>
{
public:
    AsyncWaiter() = default;

    AsyncWaiter(const AsyncWaiter&) = delete;
    AsyncWaiter& operator=(const AsyncWaiter&) = delete;
    AsyncWaiter(AsyncWaiter&&) = delete;
    AsyncWaiter& operator=(AsyncWaiter&&) = delete;

    AsyncWaiterAwaitable<void> wait() {
        return AsyncWaiterAwaitable<void>(this);
    }

    bool notify() {
        // 防止重复通知
        bool expected = false;
        if (!m_ready.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
            return false;
        }

        // 唤醒等待的协程（如果有）
        expected = true;
        if (m_waiting.compare_exchange_strong(expected, false,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
            m_waker.wakeUp();
        }
        return true;
    }

    bool isWaiting() const {
        return m_waiting.load(std::memory_order_acquire);
    }

    bool isReady() const {
        return m_ready.load(std::memory_order_acquire);
    }

private:
    friend class AsyncWaiterAwaitable<void>;

    std::atomic<bool> m_waiting{false};
    std::atomic<bool> m_ready{false};
    Waker m_waker;
};

// AsyncWaiterAwaitable<T> 实现
template<typename T>
bool AsyncWaiterAwaitable<T>::await_ready() const noexcept {
    return m_waiter->m_ready.load(std::memory_order_acquire);
}

template<typename T>
bool AsyncWaiterAwaitable<T>::await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
    m_waiter->m_waker = Waker(handle);

    // 设置等待状态
    bool expected = false;
    if (!m_waiter->m_waiting.compare_exchange_strong(expected, true,
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_acquire)) {
        // 已经有人在等待，或者已经 notify 过
        return false;
    }

    // 再次检查，防止在设置等待状态期间已经 notify
    if (m_waiter->m_ready.load(std::memory_order_acquire)) {
        m_waiter->m_waiting.store(false, std::memory_order_release);
        return false;  // 不挂起，直接返回
    }
    return true;  // 挂起
}

template<typename T>
T AsyncWaiterAwaitable<T>::await_resume() noexcept {
    return std::move(m_waiter->m_result.value());
}

// AsyncWaiterAwaitable<void> 实现
inline bool AsyncWaiterAwaitable<void>::await_ready() const noexcept {
    return m_waiter->m_ready.load(std::memory_order_acquire);
}

inline bool AsyncWaiterAwaitable<void>::await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
    m_waiter->m_waker = Waker(handle);
    bool expected = false;
    if (!m_waiter->m_waiting.compare_exchange_strong(expected, true,
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_acquire)) {
        return false;
    }

    if (m_waiter->m_ready.load(std::memory_order_acquire)) {
        m_waiter->m_waiting.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

} // namespace galay::kernel

#endif // GALAY_KERNEL_ASYNC_WAITER_H
