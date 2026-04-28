#ifndef GALAY_TIMER_HPP
#define GALAY_TIMER_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "galay-kernel/common/concepts.h"

namespace galay::kernel
{

enum class TimerFlag : int {
    kDone = 1 << 0,
    kCancel = 1 << 1,
    kTimeout = 1 << 2,
};

class Timer
{
public:
    using ptr = std::shared_ptr<Timer>;

    template<concepts::ChronoDuration Duration>
    Timer(Duration duration) {
        // 存储相对时间间隔（纳秒）
        m_delay = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        // 过期时间延迟到 getExpireTime() 时计算
        m_expireTime = 0;
    }

    virtual void handleTimeout() {
        m_flag.fetch_or(static_cast<int>(TimerFlag::kDone), std::memory_order_release);
    }
    bool done() const {
        return (m_flag.load(std::memory_order_acquire) &
                static_cast<int>(TimerFlag::kDone)) != 0;
    }
    void cancel() {
        m_flag.fetch_or(static_cast<int>(TimerFlag::kCancel), std::memory_order_release);
    }
    bool cancelled() const {
        return (m_flag.load(std::memory_order_acquire) &
                static_cast<int>(TimerFlag::kCancel)) != 0;
    }

    uint64_t getDelay() const { return m_delay; }

    // 获取绝对过期时间（纳秒），延迟计算
    uint64_t getExpireTime() const {
        if (m_expireTime == 0) {
            // 第一次调用时计算过期时间
            auto now = std::chrono::steady_clock::now();
            auto expire_time = now + std::chrono::nanoseconds(m_delay);
            m_expireTime = std::chrono::duration_cast<std::chrono::nanoseconds>(
                expire_time.time_since_epoch()).count();
        }
        return m_expireTime;
    }

protected:
    std::atomic<int> m_flag{0};
    uint64_t m_delay = 0;                  // 相对时间延迟（纳秒）
    mutable uint64_t m_expireTime = 0;     // 绝对到期时间（纳秒，延迟计算）
};

class CBTimer final: public Timer
{
public:
    template<concepts::ChronoDuration Duration>
    CBTimer(Duration duration, std::function<void()>&& callback)
        : Timer(duration), m_callback(std::move(callback)) {}

    void handleTimeout() override {
        if(!cancelled() && !done()) {
            m_callback();
        }
        Timer::handleTimeout();
    }
private:
    std::function<void()> m_callback;
};

}

#endif
