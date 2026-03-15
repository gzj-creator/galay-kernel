#include "EpollScheduler.h"

#ifdef USE_EPOLL

namespace galay::kernel
{

EpollScheduler::EpollScheduler(int max_events, int batch_size, int check_interval_ms)
    : m_running(false)
    , m_max_events(max_events)
    , m_batch_size(batch_size)
    , m_check_interval_ms(check_interval_ms)
    , m_worker(static_cast<size_t>(batch_size))
    , m_wake_coordinator(m_sleeping, m_wakeup_pending)
    , m_core(m_worker, static_cast<size_t>(batch_size))
    , m_reactor(max_events, m_last_error_code)
{
}

EpollScheduler::~EpollScheduler()
{
    stop();
}

void EpollScheduler::start()
{
    if (m_running.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    m_last_error_code.store(0, std::memory_order_release);

    m_thread = std::thread([this]() {
        m_threadId = std::this_thread::get_id();
        (void)applyConfiguredAffinity();
        eventLoop();
    });
}

void EpollScheduler::stop()
{
    if (!m_running.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    m_wake_coordinator.forceWake([this]() { notify(); });

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void EpollScheduler::notify()
{
    m_reactor.notify();
}

int EpollScheduler::addAccept(IOController* controller)
{
    return m_reactor.addAccept(controller);
}

int EpollScheduler::addConnect(IOController* controller)
{
    return m_reactor.addConnect(controller);
}

int EpollScheduler::addRecv(IOController* controller)
{
    return m_reactor.addRecv(controller);
}

int EpollScheduler::addSend(IOController* controller)
{
    return m_reactor.addSend(controller);
}

int EpollScheduler::addReadv(IOController* controller)
{
    return m_reactor.addReadv(controller);
}

int EpollScheduler::addWritev(IOController* controller)
{
    return m_reactor.addWritev(controller);
}

int EpollScheduler::addClose(IOController* controller)
{
    return m_reactor.addClose(controller);
}

int EpollScheduler::addFileRead(IOController* controller)
{
    return m_reactor.addFileRead(controller);
}

int EpollScheduler::addFileWrite(IOController* controller)
{
    return m_reactor.addFileWrite(controller);
}

int EpollScheduler::addRecvFrom(IOController* controller)
{
    return m_reactor.addRecvFrom(controller);
}

int EpollScheduler::addSendTo(IOController* controller)
{
    return m_reactor.addSendTo(controller);
}

int EpollScheduler::addFileWatch(IOController* controller)
{
    return m_reactor.addFileWatch(controller);
}

int EpollScheduler::addSendFile(IOController* controller)
{
    return m_reactor.addSendFile(controller);
}

int EpollScheduler::addCustom(IOController* controller)
{
    return m_reactor.addCustom(controller);
}

int EpollScheduler::remove(IOController* controller)
{
    return m_reactor.remove(controller);
}

std::optional<IOError> EpollScheduler::lastError() const
{
    return detail::loadBackendError(m_last_error_code);
}

bool EpollScheduler::spawn(Coroutine co)
{
    auto* scheduler = detail::CoroutineAccess::belongScheduler(co);
    if (!scheduler) {
        detail::CoroutineAccess::setScheduler(co, this);
    } else if (scheduler != this) {
        return false;
    }

    TaskRef task = detail::CoroutineAccess::detachTask(std::move(co));
    if (std::this_thread::get_id() == m_threadId) {
        m_worker.scheduleLocal(std::move(task));
        return true;
    }

    const bool queue_was_empty = m_worker.scheduleInjected(std::move(task));
    m_wake_coordinator.requestWake(queue_was_empty, [this]() { notify(); });
    return true;
}

bool EpollScheduler::schedule(TaskRef task)
{
    auto* state = task.state();
    if (!state || state->m_scheduler != this) {
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

bool EpollScheduler::spawnDeferred(Coroutine co)
{
    auto* scheduler = detail::CoroutineAccess::belongScheduler(co);
    if (!scheduler) {
        detail::CoroutineAccess::setScheduler(co, this);
    } else if (scheduler != this) {
        return false;
    }

    TaskRef task = detail::CoroutineAccess::detachTask(std::move(co));
    if (std::this_thread::get_id() == m_threadId) {
        m_worker.scheduleLocalDeferred(std::move(task));
        return true;
    }

    const bool queue_was_empty = m_worker.scheduleInjected(std::move(task));
    m_wake_coordinator.requestWake(queue_was_empty, [this]() { notify(); });
    return true;
}

bool EpollScheduler::scheduleDeferred(TaskRef task)
{
    auto* state = task.state();
    if (!state || state->m_scheduler != this) {
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

bool EpollScheduler::spawnImmidiately(Coroutine co)
{
    auto* scheduler = detail::CoroutineAccess::belongScheduler(co);
    if (scheduler) {
        return false;
    }
    detail::CoroutineAccess::setScheduler(co, this);
    TaskRef task = detail::CoroutineAccess::detachTask(std::move(co));
    resume(task);
    return true;
}

void EpollScheduler::processPendingCoroutines()
{
    (void)m_core.runReadyPass(
        [this](TaskRef& next) { Scheduler::resume(next); },
        [this](size_t drained) { m_wake_coordinator.onRemoteCollected(drained); });
}

void EpollScheduler::eventLoop()
{
    uint64_t tick_duration_ns = m_timer_manager.during();
    int timeout_ms = static_cast<int>(tick_duration_ns / 2000000ULL);
    if (timeout_ms < 1) {
        timeout_ms = 1;
    }
    const size_t batch_size = m_batch_size > 0 ? static_cast<size_t>(m_batch_size) : 1;
    size_t local_followup_pass_limit = 4096 / batch_size;
    if (local_followup_pass_limit == 0) {
        local_followup_pass_limit = 1;
    }
    if (local_followup_pass_limit > 16) {
        local_followup_pass_limit = 16;
    }

    while (m_running.load(std::memory_order_acquire)) {
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

        m_reactor.poll(timeout_ms, m_wake_coordinator);
        m_wake_coordinator.markAwake();
    }
}

}

#endif // USE_EPOLL
