#ifndef GALAY_KERNEL_EPOLL_SCHEDULER_H
#define GALAY_KERNEL_EPOLL_SCHEDULER_H

#include "EpollReactor.h"
#include "IOScheduler.hpp"
#include "SchedulerCore.h"
#include "WakeCoordinator.h"

#ifdef USE_EPOLL

#include <atomic>
#include <thread>
#include <cstdint>

#ifndef GALAY_SCHEDULER_MAX_EVENTS
#define GALAY_SCHEDULER_MAX_EVENTS 1024
#endif

#ifndef GALAY_SCHEDULER_BATCH_SIZE
#define GALAY_SCHEDULER_BATCH_SIZE 256
#endif

#ifndef GALAY_SCHEDULER_CHECK_INTERVAL_MS
#define GALAY_SCHEDULER_CHECK_INTERVAL_MS 1
#endif

namespace galay::kernel
{

struct SchedulerTestAccess;

/**
 * @brief Epoll 调度器 (Linux)
 */
class EpollScheduler: public IOScheduler
{
public:
    EpollScheduler(int max_events = GALAY_SCHEDULER_MAX_EVENTS,
                   int batch_size = GALAY_SCHEDULER_BATCH_SIZE,
                   int check_interval_ms = GALAY_SCHEDULER_CHECK_INTERVAL_MS);
    ~EpollScheduler();

    EpollScheduler(const EpollScheduler&) = delete;
    EpollScheduler& operator=(const EpollScheduler&) = delete;

    void start();
    void stop();
    void notify();

    // 网络 IO
    int addAccept(IOController* controller) override;
    int addConnect(IOController* controller) override;
    int addRecv(IOController* controller) override;
    int addSend(IOController* controller) override;
    int addReadv(IOController* controller) override;
    int addWritev(IOController* controller) override;
    int addClose(IOController* controller) override;

    // 文件 IO (通过 libaio + eventfd)
    int addFileRead(IOController* controller) override;
    int addFileWrite(IOController* controller) override;

    // UDP IO
    int addRecvFrom(IOController* controller) override;
    int addSendTo(IOController* controller) override;

    // 文件监控 (通过 inotify)
    int addFileWatch(IOController* controller) override;

    // 零拷贝发送文件
    int addSendFile(IOController* controller) override;

    int addSequence(IOController* controller) override;

    int remove(IOController* controller) override;
    std::optional<IOError> lastError() const override;

    bool schedule(TaskRef task) override;
    bool scheduleDeferred(TaskRef task) override;
    bool scheduleImmediately(TaskRef task) override;

    friend struct SchedulerTestAccess;

protected:
    std::atomic<bool> m_running;
    std::thread m_thread;

    int m_max_events;
    int m_batch_size;
    int m_check_interval_ms;

    std::atomic<uint64_t> m_last_error_code{0};
    std::atomic<bool> m_sleeping{true};
    std::atomic<bool> m_wakeup_pending{false};

    IOSchedulerWorkerState m_worker;
    WakeCoordinator m_wake_coordinator;
    SchedulerCore m_core;
    EpollReactor m_reactor;

private:
    void eventLoop();
    void processPendingTasks();
};

}

#endif // USE_EPOLL

#endif
