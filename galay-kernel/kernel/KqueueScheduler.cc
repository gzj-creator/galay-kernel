#include "KqueueScheduler.h"
#include "common/Defn.hpp"
#include "kernel/Awaitable.h"
#include "kernel/IOController.hpp"

#ifdef USE_KQUEUE

#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <stdexcept>

namespace galay::kernel
{

KqueueScheduler::KqueueScheduler(int max_events, int batch_size, int check_interval_ms)
    : m_running(false)
    , m_max_events(max_events)
    , m_batch_size(batch_size)
    , m_check_interval_ms(check_interval_ms)
    , m_last_error_code(0)
    , m_sleeping(true)
    , m_wakeup_pending(false)
    , m_worker(static_cast<size_t>(batch_size))
    , m_wake_coordinator(m_sleeping, m_wakeup_pending)
    , m_core(m_worker, static_cast<size_t>(batch_size))
    , m_reactor(max_events, m_last_error_code) {}

KqueueScheduler::~KqueueScheduler()
{
    stop();
}

void KqueueScheduler::start()
{
    if (m_running.exchange(true, std::memory_order_acq_rel)) {
        return; // Already running
    }
    m_last_error_code.store(0, std::memory_order_release);

    m_thread = std::thread([this]() {
        m_threadId = std::this_thread::get_id();  // 设置调度器线程ID
        (void)applyConfiguredAffinity();
        eventLoop();
    });
}

void KqueueScheduler::stop()
{
    if (!m_running.exchange(false, std::memory_order_acq_rel)) {
        return; // Already stopped
    }

    m_wake_coordinator.forceWake([this]() { notify(); });

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void KqueueScheduler::notify()
{
    m_reactor.notify();
}

int KqueueScheduler::addAccept(IOController* controller)
{
    return m_reactor.addAccept(controller);
}

int KqueueScheduler::addConnect(IOController* controller)
{
    return m_reactor.addConnect(controller);
}

int KqueueScheduler::addRecv(IOController* controller)
{
    return m_reactor.addRecv(controller);
}

int KqueueScheduler::addSend(IOController* controller)
{
    return m_reactor.addSend(controller);
}

int KqueueScheduler::addReadv(IOController* controller)
{
    return m_reactor.addReadv(controller);
}

int KqueueScheduler::addWritev(IOController* controller)
{
    return m_reactor.addWritev(controller);
}

int KqueueScheduler::addClose(IOController* contoller)
{
    return m_reactor.addClose(contoller);
}

int KqueueScheduler::addFileRead(IOController* controller)
{
    return m_reactor.addFileRead(controller);
}

int KqueueScheduler::addFileWrite(IOController* controller)
{
    return m_reactor.addFileWrite(controller);
}

int KqueueScheduler::addSendFile(IOController* controller)
{
    return m_reactor.addSendFile(controller);
}

int KqueueScheduler::addSequence(IOController* controller)
{
    return m_reactor.addSequence(controller);
}

int KqueueScheduler::remove(IOController* controller)
{
    return m_reactor.remove(controller);
}

std::optional<IOError> KqueueScheduler::lastError() const
{
    return detail::loadBackendError(m_last_error_code);
}

bool KqueueScheduler::schedule(TaskRef task)
{
    if (!bindTask(task)) {
        return false;
    }

    if (std::this_thread::get_id() == m_threadId) {
        m_worker.scheduleLocal(std::move(task));
        return true;
    }

    const bool queue_was_empty = m_worker.scheduleInjected(std::move(task));
    m_wake_coordinator.requestWake(queue_was_empty, [this]() { notify(); });
    return true;
}

bool KqueueScheduler::scheduleDeferred(TaskRef task)
{
    if (!bindTask(task)) {
        return false;
    }

    if (std::this_thread::get_id() == m_threadId) {
        m_worker.scheduleLocalDeferred(std::move(task));
        return true;
    }

    const bool queue_was_empty = m_worker.scheduleInjected(std::move(task));
    m_wake_coordinator.requestWake(queue_was_empty, [this]() { notify(); });
    return true;
}

bool KqueueScheduler::scheduleImmediately(TaskRef task)
{
    if (!bindTask(task)) {
        return false;
    }
    resume(task);
    return true;
}

void KqueueScheduler::processPendingCoroutines()
{
    (void)m_core.runReadyPass(
        [this](TaskRef& next) { Scheduler::resume(next); },
        [this](size_t drained) { m_wake_coordinator.onRemoteCollected(drained); });
}



void KqueueScheduler::eventLoop()
{
    // kevent 超时时间公式：timeout = tickDuration / 2
    // 使用时间轮精度的一半作为超时，确保定时器最大误差不超过半个 tick
    // 例如：tickDuration = 50ms 时，timeout = 25ms，最大误差 ≤ 25ms
    uint64_t tick_duration_ns = m_timer_manager.during();
    uint64_t timeout_ns = tick_duration_ns / 2;
    const size_t batch_size = m_batch_size > 0 ? static_cast<size_t>(m_batch_size) : 1;
    size_t local_followup_pass_limit = 4096 / batch_size;
    if (local_followup_pass_limit == 0) {
        local_followup_pass_limit = 1;
    }
    if (local_followup_pass_limit > 16) {
        local_followup_pass_limit = 16;
    }
    struct timespec timeout;
    timeout.tv_sec = timeout_ns / 1000000000ULL;
    timeout.tv_nsec = timeout_ns % 1000000000ULL;

    while (m_running.load(std::memory_order_relaxed)) {
        // Process pending coroutines
        (void)m_core.runLocalFollowupPasses(
            local_followup_pass_limit,
            [this](TaskRef& next) { Scheduler::resume(next); },
            [this](size_t drained) { m_wake_coordinator.onRemoteCollected(drained); });
        m_timer_manager.tick();
        m_wake_coordinator.markSleeping();
        if (m_core.hasPendingWork()) {
            m_wake_coordinator.markAwake();
            continue;
        }
        m_reactor.poll(timeout, m_wake_coordinator);
        m_wake_coordinator.markAwake();
    }
}

int KqueueScheduler::addRecvFrom(IOController* controller)
{
    return m_reactor.addRecvFrom(controller);
}

int KqueueScheduler::addSendTo(IOController* controller)
{
    return m_reactor.addSendTo(controller);
}

int KqueueScheduler::addFileWatch(IOController* controller)
{
    return m_reactor.addFileWatch(controller);
}


}

#endif // USE_KQUEUE
