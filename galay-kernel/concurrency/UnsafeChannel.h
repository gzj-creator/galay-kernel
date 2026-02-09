/**
 * @file UnsafeChannel.h
 * @brief 单调度器内部使用的轻量级异步通道（非线程安全）
 *
 * 警告：此通道仅供同一调度器内的协程使用，不支持跨线程/跨调度器通信。
 * 如需跨调度器通信，请使用 MpscChannel。
 */

#ifndef GALAY_KERNEL_UNSAFE_CHANNEL_H
#define GALAY_KERNEL_UNSAFE_CHANNEL_H

#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/Timeout.hpp"
#include "galay-kernel/common/Error.h"
#include <concepts>
#include <deque>
#include <optional>
#include <expected>
#include <vector>
#include <cassert>

namespace galay::kernel
{

template <typename T>
concept UnsafeChannelValue = std::movable<T>;

template <typename T>
class UnsafeChannel;

/**
 * @brief 单条接收的等待体
 */
template <typename T>
class UnsafeRecvAwaitable : public TimeoutSupport<UnsafeRecvAwaitable<T>>
{
public:
    static_assert(UnsafeChannelValue<T>, "UnsafeRecvAwaitable requires movable T");
    explicit UnsafeRecvAwaitable(UnsafeChannel<T>* channel) : m_channel(channel) {}

    bool await_ready() const noexcept;
    bool await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept;
    std::expected<T, IOError> await_resume() noexcept;

private:
    friend struct WithTimeout<UnsafeRecvAwaitable<T>>;
    UnsafeChannel<T>* m_channel;
    std::expected<T, IOError> m_result;
};

/**
 * @brief 批量接收的等待体
 */
template <typename T>
class UnsafeRecvBatchAwaitable : public TimeoutSupport<UnsafeRecvBatchAwaitable<T>>
{
public:
    static_assert(UnsafeChannelValue<T>, "UnsafeRecvBatchAwaitable requires movable T");
    explicit UnsafeRecvBatchAwaitable(UnsafeChannel<T>* channel, size_t maxCount)
        : m_channel(channel), m_maxCount(maxCount) {}

    bool await_ready() const noexcept;
    bool await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept;
    std::expected<std::vector<T>, IOError> await_resume() noexcept;

private:
    friend struct WithTimeout<UnsafeRecvBatchAwaitable<T>>;
    UnsafeChannel<T>* m_channel;
    size_t m_maxCount;
    std::expected<std::vector<T>, IOError> m_result;
};

/**
 * @brief 攒批接收的等待体
 *
 * 等待队列积累到 limit 条数据才唤醒，配合 .timeout() 可实现"达到 limit 或超时"的攒批语义。
 * 超时时若队列中有部分数据（< limit），返回已有数据而非错误。
 */
template <typename T>
class UnsafeRecvBatchedAwaitable : public TimeoutSupport<UnsafeRecvBatchedAwaitable<T>>
{
public:
    static_assert(UnsafeChannelValue<T>, "UnsafeRecvBatchedAwaitable requires movable T");
    explicit UnsafeRecvBatchedAwaitable(UnsafeChannel<T>* channel, size_t limit)
        : m_channel(channel), m_limit(limit) {}

    bool await_ready() const noexcept;
    bool await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept;
    std::expected<std::vector<T>, IOError> await_resume() noexcept;

private:
    friend struct WithTimeout<UnsafeRecvBatchedAwaitable<T>>;
    UnsafeChannel<T>* m_channel;
    size_t m_limit;
    std::expected<std::vector<T>, IOError> m_result;
};

/**
 * @brief 单调度器内部使用的轻量级异步通道
 *
 * 特点：
 * - 非线程安全，仅供同一调度器内的协程使用
 * - 无锁设计，性能优于 MpscChannel
 * - 使用 std::deque 作为底层容器，支持高效的头尾操作
 * - 适用于同一调度器内的生产者-消费者模式
 *
 * 使用场景：
 * - 同一调度器内的协程间通信
 * - 不需要跨线程的场景
 * - 对性能要求较高的场景
 *
 * 警告：
 * - 不要在不同调度器的协程间使用此通道
 * - 不要在普通线程中调用 send（除非该线程就是调度器线程）
 */
template <typename T>
class UnsafeChannel
{
public:
    static_assert(UnsafeChannelValue<T>, "UnsafeChannel requires movable T");
    static constexpr size_t DEFAULT_BATCH_SIZE = 1024;

    UnsafeChannel() = default;

    UnsafeChannel(const UnsafeChannel&) = delete;
    UnsafeChannel& operator=(const UnsafeChannel&) = delete;
    UnsafeChannel(UnsafeChannel&&) = delete;
    UnsafeChannel& operator=(UnsafeChannel&&) = delete;

    /**
     * @brief 发送单条数据
     * @warning 仅在调度器线程内调用
     */
    bool send(T&& value) {
        m_queue.push_back(std::forward<T>(value));
        ++m_size;
        if (m_hasWaiter && m_size >= m_wakeThreshold) {
            wakeUpWaiter();
        }
        return true;
    }

    bool send(const T& value) requires std::copy_constructible<T> {
        m_queue.push_back(value);
        ++m_size;
        if (m_hasWaiter && m_size >= m_wakeThreshold) {
            wakeUpWaiter();
        }
        return true;
    }

    /**
     * @brief 批量发送数据
     * @warning 仅在调度器线程内调用
     */
    bool sendBatch(const std::vector<T>& values) requires std::copy_constructible<T> {
        if (values.empty()) return true;
        for (const auto& value : values) {
            m_queue.push_back(value);
        }
        m_size += values.size();
        if (m_hasWaiter && m_size >= m_wakeThreshold) {
            wakeUpWaiter();
        }
        return true;
    }

    bool sendBatch(std::vector<T>&& values) {
        if (values.empty()) return true;
        for (auto& value : values) {
            m_queue.push_back(std::move(value));
        }
        m_size += values.size();
        if (m_hasWaiter && m_size >= m_wakeThreshold) {
            wakeUpWaiter();
        }
        return true;
    }

    /**
     * @brief 异步接收单条数据
     */
    UnsafeRecvAwaitable<T> recv() {
        return UnsafeRecvAwaitable<T>(this);
    }

    /**
     * @brief 异步批量接收数据
     */
    UnsafeRecvBatchAwaitable<T> recvBatch(size_t maxCount = DEFAULT_BATCH_SIZE) {
        return UnsafeRecvBatchAwaitable<T>(this, maxCount);
    }

    /**
     * @brief 异步攒批接收数据
     *
     * 等待队列积累到 limit 条数据才唤醒消费者，配合 .timeout() 使用：
     *   co_await channel.recvBatched(100).timeout(std::chrono::milliseconds(50));
     * 超时时若队列中有部分数据，返回已有数据而非错误。
     */
    UnsafeRecvBatchedAwaitable<T> recvBatched(size_t limit) {
        return UnsafeRecvBatchedAwaitable<T>(this, limit);
    }

    /**
     * @brief 非阻塞接收单条数据
     */
    std::optional<T> tryRecv() {
        if (m_size == 0) {
            return std::nullopt;
        }
        T value = std::move(m_queue.front());
        m_queue.pop_front();
        --m_size;
        return value;
    }

    /**
     * @brief 非阻塞批量接收数据
     */
    std::optional<std::vector<T>> tryRecvBatch(size_t maxCount = DEFAULT_BATCH_SIZE) {
        if (m_size == 0) {
            return std::nullopt;
        }
        std::vector<T> values;
        size_t count = std::min(maxCount, m_size);
        values.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            values.push_back(std::move(m_queue.front()));
            m_queue.pop_front();
        }
        m_size -= count;
        return values;
    }

    /**
     * @brief 获取队列大小
     */
    size_t size() const {
        return m_size;
    }

    /**
     * @brief 检查队列是否为空
     */
    bool empty() const {
        return m_size == 0;
    }

private:
    template <typename U>
    friend class UnsafeRecvAwaitable;
    template <typename U>
    friend class UnsafeRecvBatchAwaitable;
    template <typename U>
    friend class UnsafeRecvBatchedAwaitable;

    void wakeUpWaiter() {
        if (m_hasWaiter) {
            m_hasWaiter = false;
            m_wakeThreshold = 1;
            if (m_waiterHandle) {
                // 同调度器内，直接恢复协程
                m_waiterHandle.resume();
            }
        }
    }

    std::deque<T> m_queue;
    size_t m_size = 0;
    size_t m_wakeThreshold = 1;
    bool m_hasWaiter = false;
    std::coroutine_handle<Coroutine::promise_type> m_waiterHandle;
};

// ============================================================================
// UnsafeRecvAwaitable 实现
// ============================================================================

template <typename T>
inline bool UnsafeRecvAwaitable<T>::await_ready() const noexcept {
    return m_channel->m_size > 0;
}

template <typename T>
inline bool UnsafeRecvAwaitable<T>::await_suspend(
    std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
    // 再次检查，避免竞态
    if (m_channel->m_size > 0) {
        return false;
    }

    m_channel->m_waiterHandle = handle;
    m_channel->m_hasWaiter = true;

    return true;
}

template <typename T>
inline std::expected<T, IOError> UnsafeRecvAwaitable<T>::await_resume() noexcept {
    if (m_channel->m_size == 0) {
        return std::unexpected(IOError(kTimeout, 0));
    }
    T value = std::move(m_channel->m_queue.front());
    m_channel->m_queue.pop_front();
    --m_channel->m_size;
    return value;
}

// ============================================================================
// UnsafeRecvBatchAwaitable 实现
// ============================================================================

template <typename T>
inline bool UnsafeRecvBatchAwaitable<T>::await_ready() const noexcept {
    return m_channel->m_size > 0;
}

template <typename T>
inline bool UnsafeRecvBatchAwaitable<T>::await_suspend(
    std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
    // 再次检查，避免竞态
    if (m_channel->m_size > 0) {
        return false;
    }

    m_channel->m_waiterHandle = handle;
    m_channel->m_hasWaiter = true;

    return true;
}

template <typename T>
inline std::expected<std::vector<T>, IOError> UnsafeRecvBatchAwaitable<T>::await_resume() noexcept {
    if (m_channel->m_size == 0) {
        return std::unexpected(IOError(kTimeout, 0));
    }

    std::vector<T> values;
    size_t count = std::min(m_maxCount, m_channel->m_size);
    values.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        values.push_back(std::move(m_channel->m_queue.front()));
        m_channel->m_queue.pop_front();
    }
    m_channel->m_size -= count;

    return values;
}

// ============================================================================
// UnsafeRecvBatchedAwaitable 实现
// ============================================================================

template <typename T>
inline bool UnsafeRecvBatchedAwaitable<T>::await_ready() const noexcept {
    return m_channel->m_size >= m_limit;
}

template <typename T>
inline bool UnsafeRecvBatchedAwaitable<T>::await_suspend(
    std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
    // 再次检查，避免竞态
    if (m_channel->m_size >= m_limit) {
        return false;
    }

    m_channel->m_waiterHandle = handle;
    m_channel->m_hasWaiter = true;
    m_channel->m_wakeThreshold = m_limit;

    return true;
}

template <typename T>
inline std::expected<std::vector<T>, IOError> UnsafeRecvBatchedAwaitable<T>::await_resume() noexcept {
    m_channel->m_wakeThreshold = 1;

    if (m_channel->m_size == 0) {
        return std::unexpected(IOError(kTimeout, 0));
    }

    std::vector<T> values;
    values.reserve(m_channel->m_size);
    while (m_channel->m_size > 0) {
        values.push_back(std::move(m_channel->m_queue.front()));
        m_channel->m_queue.pop_front();
        --m_channel->m_size;
    }
    return values;
}

} // namespace galay::kernel

#endif // GALAY_KERNEL_UNSAFE_CHANNEL_H
