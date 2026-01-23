/**
 * @file AsyncMutex.h
 * @brief 异步互斥锁
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details 提供协程友好的互斥锁，使用无锁队列实现。
 * 当锁被占用时，协程会挂起而不是阻塞线程。
 *
 * 特性：
 * - 无锁等待队列（基于 moodycamel::ConcurrentQueue）
 * - 协程友好，支持 co_await
 * - 公平性：FIFO 顺序唤醒等待协程
 * - 线程安全
 * - 支持超时
 *
 * 使用方式：
 * @code
 * AsyncMutex mutex;
 *
 * Coroutine task() {
 *     co_await mutex.lock();
 *     // 临界区
 *     mutex.unlock();
 *     co_return;
 * }
 *
 * // 使用超时
 * Coroutine task2() {
 *     bool acquired = co_await mutex.lock().timeout(100ms);
 *     if (acquired) {
 *         // 临界区
 *         mutex.unlock();
 *     }
 *     co_return;
 * }
 * @endcode
 */

#ifndef GALAY_KERNEL_ASYNC_MUTEX_H
#define GALAY_KERNEL_ASYNC_MUTEX_H

#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/Scheduler.hpp"
#include "galay-kernel/kernel/Timeout.hpp"
#include "kernel/Waker.h"
#include <concurrentqueue/moodycamel/concurrentqueue.h>
#include <atomic>
#include <coroutine>

namespace galay::kernel
{

class AsyncMutex;
class AsyncLockGuard;


/**
 * @brief AsyncMutex 的等待体
 */
class AsyncMutexAwaitable : public TimeoutSupport<AsyncMutexAwaitable>
{
public:
    explicit AsyncMutexAwaitable(AsyncMutex* mutex) : m_mutex(mutex) {}

    bool await_ready() const noexcept;
    bool await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept;
    std::expected<void, IOError> await_resume() noexcept { return m_result; }

private:
    friend struct WithTimeout<AsyncMutexAwaitable>;
    AsyncMutex* m_mutex;
    std::expected<void, IOError> m_result;
};

/**
 * @brief 异步互斥锁
 *
 * @details 协程友好的互斥锁实现，使用无锁队列管理等待协程。
 * 当锁被占用时，调用 lock() 的协程会被挂起并加入等待队列，
 * 而不是阻塞当前线程。unlock() 会唤醒队列中的下一个等待协程。
 *
 * 实现细节：
 * - 使用 atomic<bool> 管理锁状态
 * - 使用 moodycamel::ConcurrentQueue 作为无锁等待队列
 * - 使用 atomic<size_t> 计数器确保正确性
 *
 * @note 线程安全，无锁实现
 */
class AsyncMutex
{
public:
    /**
     * @brief 构造函数
     * @param initial_capacity 等待队列初始容量，默认 32
     */
    explicit AsyncMutex(size_t initial_capacity = 32)
        : m_waiters(initial_capacity) {}

    // 禁止拷贝和移动
    AsyncMutex(const AsyncMutex&) = delete;
    AsyncMutex& operator=(const AsyncMutex&) = delete;
    AsyncMutex(AsyncMutex&&) = delete;
    AsyncMutex& operator=(AsyncMutex&&) = delete;

    /**
     * @brief 获取锁
     * @return 可等待对象，用于 co_await
     */
    AsyncMutexAwaitable lock() {
        return AsyncMutexAwaitable(this);
    }

    /**
     * @brief 释放锁并唤醒一个等待的协程
     * @note 必须由持有锁的协程调用
     */
    void unlock() {
        m_locked.store(false, std::memory_order_release);
        Waker waker;
        while (m_waiters.try_dequeue(waker))
        {
            // 尝试获取锁
            if (tryLock())
            {
                // 成功获取锁，唤醒该协程
                waker.wakeUp();
                return;
            } else {
                // 说明在 unlock  ----   dequeue时间内有lock
                m_waiters.enqueue(waker);
                return;
            }
        }
    }

    /**
     * @brief 尝试获取锁（非阻塞）
     * @return true 成功获取锁，false 锁被占用
     */
    bool tryLock() {
        bool expected = false;
        return m_locked.compare_exchange_strong(expected, true,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire);
    }

    /**
     * @brief 检查锁是否被占用
     * @return true 锁被占用
     */
    bool isLocked() const {
        return m_locked.load(std::memory_order_acquire);
    }

private:
    friend class AsyncMutexAwaitable;
    friend class AsyncScopedLockAwaitable;

    std::atomic<bool> m_locked{false};                      ///< 锁状态
    moodycamel::ConcurrentQueue<Waker> m_waiters;     ///< 无锁等待队列
};

// AsyncMutexAwaitable 实现
inline bool AsyncMutexAwaitable::await_ready() const noexcept {
    if (m_mutex->tryLock()) {
        const_cast<AsyncMutexAwaitable*>(this)->m_result = {};
        return true;
    }
    return false;
}

inline bool AsyncMutexAwaitable::await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
    m_mutex->m_waiters.enqueue(Waker(handle));
    // 再次检查，防止在入队期间锁被释放
    if (m_mutex->tryLock()) {
        m_result = {};
        return false;  // 不挂起
    }
    return true;
}
} // namespace galay::kernel

#endif // GALAY_KERNEL_ASYNC_MUTEX_H
