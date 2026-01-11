#include "TimerScheduler.h"
#include <chrono>

namespace galay::kernel
{

TimerScheduler* TimerScheduler::getInstance()
{
    static TimerScheduler instance;
    return &instance;
}

TimerScheduler::TimerScheduler()
{
}

TimerScheduler::~TimerScheduler()
{
    stop();
}

void TimerScheduler::start()
{
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;  // 已经在运行
    }

    m_stopFlag.store(false, std::memory_order_release);

    m_thread = std::thread([this]() {
        timerLoop();
    });
}

void TimerScheduler::stop()
{
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;  // 已经停止
    }

    m_stopFlag.store(true, std::memory_order_release);

    // 等待线程结束
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool TimerScheduler::addTimer(Timer::ptr timer)
{
    if (!timer || !m_running.load(std::memory_order_acquire)) {
        return false;
    }

    // 直接使用线程安全的 push（无锁）
    return m_timerManager.push(std::move(timer));
}

size_t TimerScheduler::addTimerBatch(const std::vector<Timer::ptr>& timers)
{
    if (!m_running.load(std::memory_order_acquire)) {
        return 0;
    }

    return m_timerManager.pushBatch(timers);
}

void TimerScheduler::timerLoop()
{
    // tick 间隔（纳秒转毫秒，至少 1ms）
    auto tickMs = std::max(1ULL, m_timerManager.during() / 1000000ULL);
    auto tickDuration = std::chrono::milliseconds(tickMs);

    while (!m_stopFlag.load(std::memory_order_acquire)) {
        // 驱动定时轮（内部会处理待添加的定时器）
        m_timerManager.tick();

        // 休眠一个 tick
        std::this_thread::sleep_for(tickDuration);
    }

    // 退出前最后处理一次
    m_timerManager.tick();
}

} // namespace galay::kernel
