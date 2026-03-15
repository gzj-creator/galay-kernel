#include "IOUringScheduler.h"

#ifdef USE_IOURING

namespace galay::kernel
{

IOUringScheduler::IOUringScheduler(int queue_depth, int batch_size)
    : m_running(false)
    , m_queue_depth(queue_depth)
    , m_batch_size(batch_size)
    , m_worker(static_cast<size_t>(batch_size))
    , m_wake_coordinator(m_sleeping, m_wakeup_pending)
    , m_core(m_worker, static_cast<size_t>(batch_size))
    , m_reactor(queue_depth, m_last_error_code)
{
}

IOUringScheduler::~IOUringScheduler()
{
    stop();
}

void IOUringScheduler::start()
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

void IOUringScheduler::stop()
{
    if (!m_running.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    m_wake_coordinator.forceWake([this]() { notify(); });

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void IOUringScheduler::notify()
{
    m_reactor.notify();
}

int IOUringScheduler::addAccept(IOController* controller)
{
    return m_reactor.addAccept(controller);
}

int IOUringScheduler::addConnect(IOController* controller)
{
    return m_reactor.addConnect(controller);
}

int IOUringScheduler::addRecv(IOController* controller)
{
    return m_reactor.addRecv(controller);
}

int IOUringScheduler::addSend(IOController* controller)
{
    return m_reactor.addSend(controller);
}

int IOUringScheduler::addReadv(IOController* controller)
{
    return m_reactor.addReadv(controller);
}

int IOUringScheduler::addWritev(IOController* controller)
{
    return m_reactor.addWritev(controller);
}

int IOUringScheduler::addClose(IOController* controller)
{
    return m_reactor.addClose(controller);
}

int IOUringScheduler::addFileRead(IOController* controller)
{
    return m_reactor.addFileRead(controller);
}

int IOUringScheduler::addFileWrite(IOController* controller)
{
    return m_reactor.addFileWrite(controller);
}

int IOUringScheduler::addRecvFrom(IOController* controller)
{
    return m_reactor.addRecvFrom(controller);
}

int IOUringScheduler::addSendTo(IOController* controller)
{
    return m_reactor.addSendTo(controller);
}

int IOUringScheduler::addFileWatch(IOController* controller)
{
    return m_reactor.addFileWatch(controller);
}

int IOUringScheduler::addSendFile(IOController* controller)
{
    return m_reactor.addSendFile(controller);
}

int IOUringScheduler::addSequence(IOController* controller)
{
    return m_reactor.addSequence(controller);
}

int IOUringScheduler::remove(IOController* controller)
{
    return m_reactor.remove(controller);
}

std::optional<IOError> IOUringScheduler::lastError() const
{
    return detail::loadBackendError(m_last_error_code);
}

bool IOUringScheduler::spawn(Coroutine co)
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

bool IOUringScheduler::schedule(TaskRef task)
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

bool IOUringScheduler::spawnDeferred(Coroutine co)
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

bool IOUringScheduler::scheduleDeferred(TaskRef task)
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

bool IOUringScheduler::spawnImmidiately(Coroutine co)
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

void IOUringScheduler::processPendingCoroutines()
{
    (void)m_core.runReadyPass(
        [this](TaskRef& next) { Scheduler::resume(next); },
        [this](size_t drained) { m_wake_coordinator.onRemoteCollected(drained); });
}

void IOUringScheduler::eventLoop()
{
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

        m_reactor.poll(GALAY_IOURING_WAIT_TIMEOUT_NS, m_wake_coordinator);
        m_wake_coordinator.markAwake();
    }
}

}

#endif // USE_IOURING
