#ifndef GALAY_KERNEL_WAIT_REGISTRATION_H
#define GALAY_KERNEL_WAIT_REGISTRATION_H

#include <atomic>
#include <cstdint>

namespace galay::kernel {

class WaitRegistration
{
public:
    bool arm(void* waiter_address) noexcept {
        if (!waiter_address) {
            return false;
        }
        m_waiter.store(waiter_address, std::memory_order_release);
        m_generation.fetch_add(1, std::memory_order_acq_rel);
        return true;
    }

    bool clear(void* waiter_address) noexcept {
        return waiter_address &&
               m_waiter.compare_exchange_strong(waiter_address,
                                               nullptr,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire);
    }

    void* consumeWake() noexcept {
        return m_waiter.exchange(nullptr, std::memory_order_acq_rel);
    }

    bool hasWaiter() const noexcept {
        return m_waiter.load(std::memory_order_acquire) != nullptr;
    }

    uint64_t generation() const noexcept {
        return m_generation.load(std::memory_order_acquire);
    }

private:
    std::atomic<void*> m_waiter{nullptr};
    std::atomic<uint64_t> m_generation{0};
};

}  // namespace galay::kernel

#endif  // GALAY_KERNEL_WAIT_REGISTRATION_H
