/**
 * @file MpscChannel.h
 * @brief 多生产者单消费者异步通道
 */

#ifndef GALAY_KERNEL_MPSC_CHANNEL_H
#define GALAY_KERNEL_MPSC_CHANNEL_H

#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/Scheduler.hpp"
#include "galay-kernel/kernel/Timeout.hpp"
#include "galay-kernel/kernel/Waker.h"
#include "galay-kernel/common/Error.h"
#include <concurrentqueue/moodycamel/concurrentqueue.h>
#include <atomic>
#include <concepts>
#include <coroutine>
#include <optional>
#include <expected>
#include <vector>
#include <thread>

namespace galay::kernel
{

template <typename T>
concept MpscValue = std::movable<T> && std::default_initializable<T>;

template <typename T>
class MpscChannel;

/**
 * @brief 单条接收的等待体
 */
template <typename T>
class MpscRecvAwaitable : public TimeoutSupport<MpscRecvAwaitable<T>>
{
public:
    static_assert(MpscValue<T>, "MpscRecvAwaitable requires movable and default initializable T");
    explicit MpscRecvAwaitable(MpscChannel<T>* channel) : m_channel(channel) {}

    bool await_ready() noexcept;
    bool await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept;
    std::expected<T, IOError> await_resume() noexcept;

private:
    friend struct WithTimeout<MpscRecvAwaitable<T>>;
    bool tryReceiveNow() noexcept;

    MpscChannel<T>* m_channel;
    std::optional<T> m_readyValue;
    void* m_waiterAddress = nullptr;
};

/**
 * @brief 批量接收的等待体
 */
template <typename T>
class MpscRecvBatchAwaitable : public TimeoutSupport<MpscRecvBatchAwaitable<T>>
{
public:
    static_assert(MpscValue<T>, "MpscRecvBatchAwaitable requires movable and default initializable T");
    explicit MpscRecvBatchAwaitable(MpscChannel<T>* channel, size_t max_count)
        : m_channel(channel), m_maxCount(max_count){}

    bool await_ready() noexcept;
    bool await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept;
    std::expected<std::vector<T>, IOError> await_resume() noexcept;

private:
    friend struct WithTimeout<MpscRecvBatchAwaitable<T>>;
    bool tryReceiveNow() noexcept;

    MpscChannel<T>* m_channel;
    size_t m_maxCount;
    std::optional<std::vector<T>> m_readyValues;
    void* m_waiterAddress = nullptr;
};

/**
 * @brief 多生产者单消费者异步通道
 */
template <typename T>
class MpscChannel
{
public:
    static_assert(MpscValue<T>, "MpscChannel requires movable and default initializable T");
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
        uint32_t prevSize = m_size.fetch_add(1, std::memory_order_acq_rel);
        if (!m_queue.enqueue(std::forward<T>(value))) {
            m_size.fetch_sub(1, std::memory_order_acq_rel);
            return false;
        }
        if (prevSize == 0 || hasWaiter()) {
            wakePublishedWaiter();
        }
        return true;
    }

    bool send(const T& value) requires std::copy_constructible<T> {
        T copy = value;
        return send(std::move(copy));
    }

    /**
     * @brief 批量发送数据
     */
    bool sendBatch(const std::vector<T>& values) requires std::copy_constructible<T> {
        if (values.empty()) return true;
        uint32_t count = static_cast<uint32_t>(values.size());
        uint32_t prevSize = m_size.fetch_add(count, std::memory_order_acq_rel);
        if (!m_queue.enqueue_bulk(values.data(), values.size())) {
            m_size.fetch_sub(count, std::memory_order_acq_rel);
            return false;
        }
        if (prevSize == 0 || hasWaiter()) {
            wakePublishedWaiter();
        }
        return true;
    }

    bool sendBatch(std::vector<T>&& values) {
        if (values.empty()) return true;
        uint32_t count = static_cast<uint32_t>(values.size());
        uint32_t prevSize = m_size.fetch_add(count, std::memory_order_acq_rel);
        if (!m_queue.enqueue_bulk(std::make_move_iterator(values.begin()), count)) {
            m_size.fetch_sub(count, std::memory_order_acq_rel);
            return false;
        }
        if (prevSize == 0 || hasWaiter()) {
            wakePublishedWaiter();
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
        if (m_size.load(std::memory_order_acquire) == 0) {
            return std::nullopt;
        }
        T value;
        if (m_queue.try_dequeue(value)) {
            m_size.fetch_sub(1, std::memory_order_acq_rel);
            return value;
        }
        return std::nullopt;
    }

    std::optional<std::vector<T>> tryRecvBatch(size_t maxCount = DEFAULT_BATCH_SIZE) {
        if (m_size.load(std::memory_order_acquire) == 0) {
            return std::nullopt;
        }
        std::vector<T> values(maxCount);
        size_t count = m_queue.try_dequeue_bulk(values.data(), maxCount);
        if (count == 0) {
            return std::nullopt;
        }
        m_size.fetch_sub(static_cast<uint32_t>(count), std::memory_order_acq_rel);
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

    using WaiterHandle = std::coroutine_handle<Coroutine::promise_type>;

    bool hasWaiter() const noexcept {
        return m_waiter.load(std::memory_order_acquire) != nullptr;
    }

    void publishWaiter(void* waiterAddress) noexcept {
        m_waiter.store(waiterAddress, std::memory_order_release);
    }

    bool clearWaiter(void* waiterAddress) noexcept {
        return m_waiter.compare_exchange_strong(waiterAddress,
                                                nullptr,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire);
    }

    void wakePublishedWaiter() noexcept {
        void* waiterAddress = m_waiter.exchange(nullptr, std::memory_order_acq_rel);
        if (!waiterAddress) {
            return;
        }
        Waker(WaiterHandle::from_address(waiterAddress)).wakeUp();
    }


    alignas(64) std::atomic<uint32_t> m_size{0};
    std::atomic<void*> m_waiter{nullptr};
    moodycamel::ConcurrentQueue<T> m_queue;
};

// ============================================================================
// MpscRecvAwaitable 实现
// ============================================================================

template <typename T>
inline bool MpscRecvAwaitable<T>::tryReceiveNow() noexcept {
    if (m_readyValue.has_value()) {
        return true;
    }
    if (auto value = m_channel->tryRecv(); value.has_value()) {
        m_readyValue = std::move(*value);
        return true;
    }
    return false;
}

template <typename T>
inline bool MpscRecvAwaitable<T>::await_ready() noexcept {
    return tryReceiveNow();
}

template <typename T>
inline bool MpscRecvAwaitable<T>::await_suspend(
    std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
    if (tryReceiveNow()) {
        return false;
    }

    m_waiterAddress = handle.address();
    m_channel->publishWaiter(m_waiterAddress);

    if (!tryReceiveNow()) {
        return true;
    }

    if (m_channel->clearWaiter(m_waiterAddress)) {
        return false;
    }

    return true;
}

template <typename T>
inline std::expected<T, IOError> MpscRecvAwaitable<T>::await_resume() noexcept {
    if (m_waiterAddress != nullptr) {
        void* waiterAddress = m_waiterAddress;
        m_channel->clearWaiter(waiterAddress);
        m_waiterAddress = nullptr;
    }

    if (m_readyValue.has_value()) {
        return std::move(*m_readyValue);
    }

    if (auto value = m_channel->tryRecv(); value.has_value()) {
        return std::move(*value);
    }

    return std::unexpected(IOError(kTimeout, 0));
}

// ============================================================================
// MpscRecvBatchAwaitable 实现
// ============================================================================

template <typename T>
inline bool MpscRecvBatchAwaitable<T>::tryReceiveNow() noexcept {
    if (m_readyValues.has_value()) {
        return true;
    }
    if (auto values = m_channel->tryRecvBatch(m_maxCount); values.has_value()) {
        m_readyValues = std::move(*values);
        return true;
    }
    return false;
}

template <typename T>
inline bool MpscRecvBatchAwaitable<T>::await_ready() noexcept {
    return tryReceiveNow();
}

template <typename T>
inline bool MpscRecvBatchAwaitable<T>::await_suspend(
    std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
    if (tryReceiveNow()) {
        return false;
    }

    m_waiterAddress = handle.address();
    m_channel->publishWaiter(m_waiterAddress);

    if (!tryReceiveNow()) {
        return true;
    }

    if (m_channel->clearWaiter(m_waiterAddress)) {
        return false;
    }

    return true;
}

template <typename T>
inline std::expected<std::vector<T>, IOError> MpscRecvBatchAwaitable<T>::await_resume() noexcept {
    if (m_waiterAddress != nullptr) {
        void* waiterAddress = m_waiterAddress;
        m_channel->clearWaiter(waiterAddress);
        m_waiterAddress = nullptr;
    }

    if (m_readyValues.has_value()) {
        return std::move(*m_readyValues);
    }

    if (auto values = m_channel->tryRecvBatch(m_maxCount); values.has_value()) {
        return std::move(*values);
    }
    return std::unexpected(IOError(kTimeout, 0));
}

} // namespace galay::kernel

#endif // GALAY_KERNEL_MPSC_CHANNEL_H
