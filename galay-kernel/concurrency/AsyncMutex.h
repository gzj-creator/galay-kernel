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
 * // 或使用 RAII 守卫（推荐）
 * Coroutine task2() {
 *     auto guard = co_await mutex.scopedLock();
 *     // 临界区，自动释放锁
 *     co_return;
 * }
 * @endcode
 */

#ifndef GALAY_KERNEL_ASYNC_MUTEX_H
#define GALAY_KERNEL_ASYNC_MUTEX_H

#include "../kernel/Coroutine.h"
#include "../kernel/Scheduler.h"
#include <concurrentqueue/moodycamel/concurrentqueue.h>
#include <atomic>
#include <coroutine>

namespace galay::kernel
{

class AsyncMutex;
class AsyncLockGuard;

/**
 * @brief 等待者信息
 */
struct MutexWaiter {
    Coroutine coro;
    Scheduler* scheduler = nullptr;
};

/**
 * @brief AsyncMutex 的等待体
 */
class AsyncMutexAwaitable
{
public:
    explicit AsyncMutexAwaitable(AsyncMutex* mutex) : m_mutex(mutex) {}

    bool await_ready() const noexcept;
    bool await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept;
    void await_resume() noexcept {}

private:
    AsyncMutex* m_mutex;
};

/**
 * @brief AsyncMutex scopedLock 的等待体，返回 AsyncLockGuard
 */
class AsyncScopedLockAwaitable
{
public:
    explicit AsyncScopedLockAwaitable(AsyncMutex* mutex) : m_mutex(mutex) {}

    bool await_ready() const noexcept;
    bool await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept;
    AsyncLockGuard await_resume() noexcept;

private:
    AsyncMutex* m_mutex;
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
     * @brief 获取锁并返回 RAII 守卫（推荐使用）
     * @return 可等待对象，await 后返回 AsyncLockGuard
     */
    AsyncScopedLockAwaitable scopedLock() {
        return AsyncScopedLockAwaitable(this);
    }

    /**
     * @brief 释放锁并唤醒一个等待的协程
     * @note 必须由持有锁的协程调用
     */
    void unlock() {
        MutexWaiter waiter;

        // 尝试从队列中取出一个等待者
        if (m_waiters.try_dequeue(waiter)) {
            // 有等待者，唤醒它（锁直接转移，不释放）
            m_waiter_count.fetch_sub(1, std::memory_order_relaxed);
            if (waiter.scheduler && waiter.coro.isValid()) {
                waiter.coro.belongScheduler(waiter.scheduler);
                waiter.scheduler->spawn(std::move(waiter.coro));
            }
        } else {
            // 没有等待者，释放锁
            m_locked.store(false, std::memory_order_release);

            // Double-check：释放锁后可能有新的等待者加入
            // 如果有等待者且能重新获取锁，则唤醒一个
            if (m_waiter_count.load(std::memory_order_acquire) > 0) {
                bool expected = false;
                if (m_locked.compare_exchange_strong(expected, true,
                                                      std::memory_order_acq_rel)) {
                    if (m_waiters.try_dequeue(waiter)) {
                        m_waiter_count.fetch_sub(1, std::memory_order_relaxed);
                        if (waiter.scheduler && waiter.coro.isValid()) {
                            waiter.coro.belongScheduler(waiter.scheduler);
                            waiter.scheduler->spawn(std::move(waiter.coro));
                        }
                    } else {
                        // 没取到，再次释放锁
                        m_locked.store(false, std::memory_order_release);
                    }
                }
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

    /**
     * @brief 获取等待队列中的协程数量（近似值）
     * @return 等待协程数量
     */
    size_t waiterCount() const {
        return m_waiter_count.load(std::memory_order_relaxed);
    }

private:
    friend class AsyncMutexAwaitable;
    friend class AsyncScopedLockAwaitable;

    std::atomic<bool> m_locked{false};                      ///< 锁状态
    std::atomic<size_t> m_waiter_count{0};                  ///< 等待者计数
    moodycamel::ConcurrentQueue<MutexWaiter> m_waiters;     ///< 无锁等待队列
};

/**
 * @brief RAII 异步锁守卫
 *
 * @details 在作用域结束时自动释放锁，防止忘记 unlock 导致死锁。
 *
 * @code
 * Coroutine task() {
 *     auto guard = co_await mutex.scopedLock();
 *     // 临界区操作
 *     // guard 析构时自动释放锁
 * }
 * @endcode
 */
class AsyncLockGuard
{
public:
    /**
     * @brief 构造函数
     * @param mutex 已获取锁的互斥量
     */
    explicit AsyncLockGuard(AsyncMutex* mutex) : m_mutex(mutex) {}

    // 禁止拷贝
    AsyncLockGuard(const AsyncLockGuard&) = delete;
    AsyncLockGuard& operator=(const AsyncLockGuard&) = delete;

    // 允许移动
    AsyncLockGuard(AsyncLockGuard&& other) noexcept : m_mutex(other.m_mutex) {
        other.m_mutex = nullptr;
    }

    AsyncLockGuard& operator=(AsyncLockGuard&& other) noexcept {
        if (this != &other) {
            if (m_mutex) {
                m_mutex->unlock();
            }
            m_mutex = other.m_mutex;
            other.m_mutex = nullptr;
        }
        return *this;
    }

    /**
     * @brief 析构函数，自动释放锁
     */
    ~AsyncLockGuard() {
        if (m_mutex) {
            m_mutex->unlock();
        }
    }

    /**
     * @brief 提前释放锁
     */
    void unlock() {
        if (m_mutex) {
            m_mutex->unlock();
            m_mutex = nullptr;
        }
    }

    /**
     * @brief 检查是否持有锁
     */
    bool ownsLock() const {
        return m_mutex != nullptr;
    }

private:
    AsyncMutex* m_mutex;
};

// AsyncMutexAwaitable 实现
inline bool AsyncMutexAwaitable::await_ready() const noexcept {
    // 尝试快速获取锁
    bool expected = false;
    return m_mutex->m_locked.compare_exchange_strong(expected, true,
                                                      std::memory_order_acq_rel,
                                                      std::memory_order_acquire);
}

inline bool AsyncMutexAwaitable::await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
    // 准备等待者信息
    Coroutine coro = handle.promise().getCoroutine();
    MutexWaiter waiter;
    waiter.scheduler = coro.belongScheduler();
    waiter.coro = coro;

    // 先增加计数，再入队
    m_mutex->m_waiter_count.fetch_add(1, std::memory_order_relaxed);
    m_mutex->m_waiters.enqueue(std::move(waiter));

    // 再次尝试获取锁（可能在入队期间锁被释放了）
    bool expected = false;
    if (m_mutex->m_locked.compare_exchange_strong(expected, true,
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_acquire)) {
        // 获取成功，需要把自己从队列中移除
        // 由于无锁队列不支持精确移除，我们通过 unlock 逻辑处理
        // 这里直接调用 unlock 来唤醒队列中的一个（可能是自己）
        m_mutex->unlock();
        return false;  // 不挂起，已获取锁
    }

    return true;  // 挂起等待
}

// AsyncScopedLockAwaitable 实现
inline bool AsyncScopedLockAwaitable::await_ready() const noexcept {
    bool expected = false;
    return m_mutex->m_locked.compare_exchange_strong(expected, true,
                                                      std::memory_order_acq_rel,
                                                      std::memory_order_acquire);
}

inline bool AsyncScopedLockAwaitable::await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
    Coroutine coro = handle.promise().getCoroutine();
    MutexWaiter waiter;
    waiter.scheduler = coro.belongScheduler();
    waiter.coro = coro;

    m_mutex->m_waiter_count.fetch_add(1, std::memory_order_relaxed);
    m_mutex->m_waiters.enqueue(std::move(waiter));

    bool expected = false;
    if (m_mutex->m_locked.compare_exchange_strong(expected, true,
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_acquire)) {
        m_mutex->unlock();
        return false;
    }

    return true;
}

inline AsyncLockGuard AsyncScopedLockAwaitable::await_resume() noexcept {
    return AsyncLockGuard(m_mutex);
}

} // namespace galay::kernel

#endif // GALAY_KERNEL_ASYNC_MUTEX_H
