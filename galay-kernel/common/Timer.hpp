#ifndef GALAY_TIMER_HPP
#define GALAY_TIMER_HPP

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "galay-kernel/common/Concepts.h"

namespace galay::kernel
{

#define DONE        1 << 0
#define CANCEL      1 << 1

#define TIMEOUT     1 << 2      //用于TimeoutTimer判断IO超时

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

    virtual void handleTimeout() { m_flag |= DONE; }
    bool done() { return m_flag & DONE; }
    void cancel() { m_flag |= CANCEL; }
    bool cancelled() { return m_flag & CANCEL; }

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
    int m_flag = 0;
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
