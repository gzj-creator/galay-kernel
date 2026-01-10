/**
 * @file MpscChannel.h
 * @brief 多生产者单消费者异步通道
 */

#ifndef GALAY_KERNEL_MPSC_CHANNEL_H
#define GALAY_KERNEL_MPSC_CHANNEL_H

#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/Scheduler.hpp"
#include "galay-kernel/kernel/Waker.h"
#include <concurrentqueue/moodycamel/concurrentqueue.h>
#include <atomic>
#include <coroutine>
#include <optional>
#include <vector>
#include <thread>

namespace galay::kernel
{

template <typename T>
class MpscChannel;

/**
 * @brief 单条接收的等待体
 */
template <typename T>
class MpscRecvAwaitable
{
public:
    explicit MpscRecvAwaitable(MpscChannel<T>* channel) : m_channel(channel) {}

    bool await_ready() const noexcept;
    bool await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept;
    std::optional<T> await_resume() noexcept;

private:
    MpscChannel<T>* m_channel;
};

/**
 * @brief 批量接收的等待体
 */
template <typename T>
class MpscRecvBatchAwaitable
{
public:
    explicit MpscRecvBatchAwaitable(MpscChannel<T>* channel, size_t max_count)
        : m_channel(channel), m_maxCount(max_count){}

    bool await_ready() const noexcept;
    bool await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept;
    std::optional<std::vector<T>> await_resume() noexcept;

private:
    MpscChannel<T>* m_channel;
    size_t m_maxCount;
};

/**
 * @brief 多生产者单消费者异步通道
 */
template <typename T>
class MpscChannel
{
public:
    using MpscToken = std::thread::id;

    static constexpr size_t DEFAULT_BATCH_SIZE = 1024;


    MpscChannel() {}
    MpscChannel(const MpscChannel&) = delete;
    MpscChannel& operator=(const MpscChannel&) = delete;
    MpscChannel(MpscChannel&&) = delete;
    MpscChannel& operator=(MpscChannel&&) = delete;

    /**
     * @brief 发送单条数据（线程安全，支持跨调度器）,保证一定有对端在recv
     */
    bool send(T&& value) {
        if (!m_queue.enqueue(std::forward<T>(value))) {
            return false;
        }
        uint32_t prevSize = m_size.fetch_add(1, std::memory_order_acq_rel);
        if (prevSize == 0 ) {
            m_waker.wakeUp();
        }
        return true;
    }

    bool send(const T& value) {
        T copy = value;
        return send(std::move(copy));
    }

    /**
     * @brief 批量发送数据
     */
    bool sendBatch(const std::vector<T>& values) {
        if (values.empty()) return true;
        if (!m_queue.enqueue_bulk(values.data(), values.size())) {
            return false;
        }
        uint32_t prevSize = m_size.fetch_add(static_cast<uint32_t>(values.size()),
                                              std::memory_order_acq_rel);
        if (prevSize == 0) {
            m_waker.wakeUp();
        }
        return true;
    }

    bool sendBatch(std::vector<T>&& values) {
        if (values.empty()) return true;
        size_t count = values.size();
        if (!m_queue.enqueue_bulk(std::make_move_iterator(values.begin()), count)) {
            return false;
        }
        uint32_t prevSize = m_size.fetch_add(static_cast<uint32_t>(count),
                                              std::memory_order_acq_rel);
        if (prevSize == 0) {
            m_waker.wakeUp();
        }
        return true;
    }

    MpscRecvAwaitable<T> recv() {
        return MpscRecvAwaitable<T>(this);
    }

    MpscRecvBatchAwaitable<T> recvBatch(size_t maxCount = DEFAULT_BATCH_SIZE) {
        return MpscRecvBatchAwaitable<T>(this, maxCount);
    }

    std::optional<T> tryRecv() {
        T value;
        if (m_queue.try_dequeue(value)) {
            m_size.fetch_sub(1, std::memory_order_acq_rel);
            return value;
        }
        return std::nullopt;
    }

    std::optional<std::vector<T>> tryRecvBatch(size_t maxCount = DEFAULT_BATCH_SIZE) {
        std::vector<T> values(maxCount);
        size_t count = m_queue.try_dequeue_bulk(values.data(), maxCount);
        if (count == 0) {
            return std::nullopt;
        }
        uint32_t current = m_size.load(std::memory_order_acquire);
        if (count >= current) {
            m_size.store(0, std::memory_order_release);
        } else {
            m_size.fetch_sub(static_cast<uint32_t>(count), std::memory_order_acq_rel);
        }
        values.resize(count);
        return values;
    }

    size_t size() const {
        return m_size.load(std::memory_order_relaxed);
    }

    bool empty() const {
        return m_size.load(std::memory_order_acquire) == 0;
    }

private:
    template <typename U>
    friend class MpscRecvAwaitable;
    template <typename U>
    friend class MpscRecvBatchAwaitable;


    alignas(64) std::atomic<uint32_t> m_size{0};
    moodycamel::ConcurrentQueue<T> m_queue;
    Waker m_waker;
};

// ============================================================================
// MpscRecvAwaitable 实现
// ============================================================================

template <typename T>
inline bool MpscRecvAwaitable<T>::await_ready() const noexcept {
    return m_channel->m_size.load(std::memory_order_acquire) > 0;
}

template <typename T>
inline bool MpscRecvAwaitable<T>::await_suspend(
    std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
    // 设置 waker
    m_channel->m_waker = Waker(handle);

    // 再次检查队列状态，防止竞态条件
    // 如果在设置 waker 之前已经有数据入队，需要立即返回
    if (m_channel->m_size.load(std::memory_order_acquire) > 0) {
        return false;  // 不挂起，直接返回
    }

    return true;  // 挂起
}

template <typename T>
inline std::optional<T> MpscRecvAwaitable<T>::await_resume() noexcept {
    T value;
    if (m_channel->m_queue.try_dequeue(value)) {
        m_channel->m_size.fetch_sub(1, std::memory_order_acq_rel);
        return value;
    }
    return std::nullopt;
}

// ============================================================================
// MpscRecvBatchAwaitable 实现
// ============================================================================

template <typename T>
inline bool MpscRecvBatchAwaitable<T>::await_ready() const noexcept {
    return m_channel->m_size.load(std::memory_order_acquire) > 0;
}

template <typename T>
inline bool MpscRecvBatchAwaitable<T>::await_suspend(
    std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
    // 设置 waker
    m_channel->m_waker = Waker(handle);

    // 再次检查队列状态，防止竞态条件
    // 如果在设置 waker 之前已经有数据入队，需要立即返回
    if (m_channel->m_size.load(std::memory_order_acquire) > 0) {
        return false;  // 不挂起，直接返回
    }

    return true;  // 挂起
}

template <typename T>
inline std::optional<std::vector<T>> MpscRecvBatchAwaitable<T>::await_resume() noexcept {
    std::vector<T> values(m_maxCount);
    size_t count = m_channel->m_queue.try_dequeue_bulk(values.data(), m_maxCount);

    if (count == 0) {
        return std::nullopt;
    }

    uint32_t current = m_channel->m_size.load(std::memory_order_acquire);
    if (count >= current) {
        m_channel->m_size.store(0, std::memory_order_release);
    } else {
        m_channel->m_size.fetch_sub(static_cast<uint32_t>(count), std::memory_order_acq_rel);
    }

    values.resize(count);
    return values;
}

} // namespace galay::kernel

#endif // GALAY_KERNEL_MPSC_CHANNEL_H
