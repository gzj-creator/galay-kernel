#ifndef GALAY_KERNEL_WAKE_COORDINATOR_H
#define GALAY_KERNEL_WAKE_COORDINATOR_H

#include <atomic>
#include <cstdint>
#include <utility>

namespace galay::kernel {

class WakeCoordinator
{
public:
    WakeCoordinator(std::atomic<bool>& sleeping, std::atomic<bool>& wakeup_pending) noexcept
        : m_sleeping(sleeping)
        , m_wakeup_pending(wakeup_pending)
    {
    }

    template <typename NotifyFn>
    bool requestWake(bool queue_was_empty, NotifyFn&& notify_fn) {
        m_wake_requests.fetch_add(1, std::memory_order_relaxed);
        if (!(queue_was_empty || m_sleeping.load(std::memory_order_acquire))) {
            m_coalesced_wakes.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (m_wakeup_pending.exchange(true, std::memory_order_acq_rel)) {
            m_coalesced_wakes.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        std::forward<NotifyFn>(notify_fn)();
        m_wake_emits.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    template <typename NotifyFn>
    void forceWake(NotifyFn&& notify_fn) {
        std::forward<NotifyFn>(notify_fn)();
        m_wake_emits.fetch_add(1, std::memory_order_relaxed);
    }

    void markSleeping() noexcept {
        m_sleeping.store(true, std::memory_order_release);
    }

    void markAwake() noexcept {
        m_sleeping.store(false, std::memory_order_release);
    }

    void cancelPendingWake() noexcept {
        m_wakeup_pending.store(false, std::memory_order_release);
    }

    void onRemoteCollected(size_t drained) noexcept {
        if (drained == 0) {
            return;
        }
        markAwake();
        cancelPendingWake();
    }

    bool isSleeping() const noexcept {
        return m_sleeping.load(std::memory_order_acquire);
    }

    bool hasPendingWake() const noexcept {
        return m_wakeup_pending.load(std::memory_order_acquire);
    }

    uint64_t wakeRequests() const noexcept {
        return m_wake_requests.load(std::memory_order_acquire);
    }

    uint64_t wakeEmits() const noexcept {
        return m_wake_emits.load(std::memory_order_acquire);
    }

    uint64_t coalescedWakes() const noexcept {
        return m_coalesced_wakes.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool>& m_sleeping;
    std::atomic<bool>& m_wakeup_pending;
    std::atomic<uint64_t> m_wake_requests{0};
    std::atomic<uint64_t> m_wake_emits{0};
    std::atomic<uint64_t> m_coalesced_wakes{0};
};

}  // namespace galay::kernel

#endif  // GALAY_KERNEL_WAKE_COORDINATOR_H
