/**
 * @file MpscChannel.h
 * @brief 多生产者单消费者异步通道
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details 提供线程安全的 MPSC (Multi-Producer Single-Consumer) 通道。
 * 支持多个生产者线程/协程同时发送数据，单个消费者协程异步接收。
 *
 * 特性：
 * - 无锁队列（基于 moodycamel::ConcurrentQueue）
 * - 协程友好，支持 co_await 接收
 * - 支持单条和批量发送/接收
 * - 线程安全
 *
 * 使用方式：
 * @code
 * MpscChannel<int> channel;
 *
 * // 生产者（可以是多个线程/协程）
 * channel.send(42);
 * channel.send(std::vector<int>{1, 2, 3});  // 批量发送
 *
 * // 消费者协程
 * Coroutine consumer() {
 *     auto value = co_await channel.recv();
 *     if (value) {
 *         // 处理 *value
 *     }
 *
 *     auto batch = co_await channel.recv_batch();
 *     if (batch) {
 *         // 处理 *batch (std::vector<int>)
 *     }
 *     co_return;
 * }
 * @endcode
 */

#ifndef GALAY_KERNEL_MPSC_CHANNEL_H
#define GALAY_KERNEL_MPSC_CHANNEL_H

#include "../kernel/Coroutine.h"
#include "../kernel/Waker.h"
#include <concurrentqueue/moodycamel/concurrentqueue.h>
#include <atomic>
#include <coroutine>
#include <optional>
#include <vector>

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
        : m_channel(channel), m_max_count(max_count) {}

    bool await_ready() const noexcept;
    bool await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept;
    std::optional<std::vector<T>> await_resume() noexcept;

private:
    MpscChannel<T>* m_channel;
    size_t m_max_count;
};

/**
 * @brief 多生产者单消费者异步通道
 *
 * @tparam T 数据类型
 *
 * @details 线程安全的 MPSC 通道，支持多个生产者同时发送，
 * 单个消费者协程异步接收。当通道为空时，消费者会挂起等待。
 *
 * @note
 * - send() 是线程安全的，可以从任意线程调用
 * - recv() 应该只在单个消费者协程中调用
 * - 通道没有容量限制（无界队列）
 */
template <typename T>
class MpscChannel
{
public:
    /// 默认批量接收大小
    static constexpr size_t DEFAULT_BATCH_SIZE = 1024;

    /**
     * @brief 构造函数
     * @param initial_capacity 队列初始容量，默认 32
     */
    explicit MpscChannel(size_t initial_capacity = 32)
        : m_queue(initial_capacity) {}

    // 禁止拷贝和移动
    MpscChannel(const MpscChannel&) = delete;
    MpscChannel& operator=(const MpscChannel&) = delete;
    MpscChannel(MpscChannel&&) = delete;
    MpscChannel& operator=(MpscChannel&&) = delete;

    /**
     * @brief 发送单条数据
     * @param value 要发送的数据（右值引用）
     * @return true 发送成功，false 发送失败
     * @note 线程安全，可从任意线程调用
     */
    bool send(T&& value) {
        if (!m_queue.enqueue(std::forward<T>(value))) {
            return false;
        }
        uint32_t prev_size = m_size.fetch_add(1, std::memory_order_acq_rel);
        if (prev_size == 0) {
            m_waker.wakeUp();
        }
        return true;
    }

    /**
     * @brief 发送单条数据（左值版本）
     * @param value 要发送的数据
     * @return true 发送成功，false 发送失败
     */
    bool send(const T& value) {
        T copy = value;
        return send(std::move(copy));
    }

    /**
     * @brief 批量发送数据
     * @param values 要发送的数据数组
     * @return true 发送成功，false 发送失败
     * @note 线程安全，可从任意线程调用
     */
    bool send_batch(const std::vector<T>& values) {
        if (values.empty()) return true;
        if (!m_queue.enqueue_bulk(values.data(), values.size())) {
            return false;
        }
        uint32_t prev_size = m_size.fetch_add(static_cast<uint32_t>(values.size()),
                                               std::memory_order_acq_rel);
        if (prev_size == 0) {
            m_waker.wakeUp();
        }
        return true;
    }

    /**
     * @brief 批量发送数据（移动版本）
     * @param values 要发送的数据数组
     * @return true 发送成功，false 发送失败
     */
    bool send_batch(std::vector<T>&& values) {
        if (values.empty()) return true;
        size_t count = values.size();
        if (!m_queue.enqueue_bulk(std::make_move_iterator(values.begin()), count)) {
            return false;
        }
        uint32_t prev_size = m_size.fetch_add(static_cast<uint32_t>(count),
                                               std::memory_order_acq_rel);
        if (prev_size == 0) {
            m_waker.wakeUp();
        }
        return true;
    }

    /**
     * @brief 异步接收单条数据
     * @return 可等待对象，await 后返回 std::optional<T>
     */
    MpscRecvAwaitable<T> recv() {
        return MpscRecvAwaitable<T>(this);
    }

    /**
     * @brief 异步批量接收数据
     * @param max_count 最大接收数量，默认 DEFAULT_BATCH_SIZE
     * @return 可等待对象，await 后返回 std::optional<std::vector<T>>
     */
    MpscRecvBatchAwaitable<T> recv_batch(size_t max_count = DEFAULT_BATCH_SIZE) {
        return MpscRecvBatchAwaitable<T>(this, max_count);
    }

    /**
     * @brief 尝试同步接收单条数据（非阻塞）
     * @return 数据（如果有），否则 std::nullopt
     */
    std::optional<T> try_recv() {
        T value;
        if (m_queue.try_dequeue(value)) {
            m_size.fetch_sub(1, std::memory_order_acq_rel);
            return value;
        }
        return std::nullopt;
    }

    /**
     * @brief 尝试同步批量接收数据（非阻塞）
     * @param max_count 最大接收数量
     * @return 数据数组（如果有），否则 std::nullopt
     */
    std::optional<std::vector<T>> try_recv_batch(size_t max_count = DEFAULT_BATCH_SIZE) {
        std::vector<T> values(max_count);
        size_t count = m_queue.try_dequeue_bulk(values.data(), max_count);
        if (count == 0) {
            return std::nullopt;
        }
        // 饱和减法
        uint32_t current = m_size.load(std::memory_order_acquire);
        if (count >= current) {
            m_size.store(0, std::memory_order_release);
        } else {
            m_size.fetch_sub(static_cast<uint32_t>(count), std::memory_order_acq_rel);
        }
        values.resize(count);
        return values;
    }

    /**
     * @brief 获取通道中的数据数量（近似值）
     * @return 数据数量
     */
    size_t size() const {
        return m_size.load(std::memory_order_relaxed);
    }

    /**
     * @brief 检查通道是否为空
     * @return true 如果通道为空
     */
    bool empty() const {
        return m_size.load(std::memory_order_acquire) == 0;
    }

private:
    template <typename U>
    friend class MpscRecvAwaitable;
    template <typename U>
    friend class MpscRecvBatchAwaitable;

    // 缓存行对齐，避免 false sharing
    alignas(64) std::atomic<uint32_t> m_size{0};              ///< 队列大小
    moodycamel::ConcurrentQueue<T> m_queue;                   ///< 无锁队列
    Waker m_waker;                                            ///< 唤醒器
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
    // Double-check：先检查是否有数据
    if (m_channel->m_size.load(std::memory_order_acquire) > 0) {
        return false;  // 不挂起
    }
    // 设置 waker
    m_channel->m_waker = Waker(handle);
    // 再次检查，防止在设置 waker 期间有数据到达
    if (m_channel->m_size.load(std::memory_order_acquire) > 0) {
        return false;  // 不挂起
    }
    return true;  // 挂起等待
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
    if (m_channel->m_size.load(std::memory_order_acquire) > 0) {
        return false;
    }
    m_channel->m_waker = Waker(handle);
    if (m_channel->m_size.load(std::memory_order_acquire) > 0) {
        return false;
    }
    return true;
}

template <typename T>
inline std::optional<std::vector<T>> MpscRecvBatchAwaitable<T>::await_resume() noexcept {
    std::vector<T> values(m_max_count);
    size_t count = m_channel->m_queue.try_dequeue_bulk(values.data(), m_max_count);

    if (count == 0) {
        return std::nullopt;
    }

    // 饱和减法：防止下溢
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
