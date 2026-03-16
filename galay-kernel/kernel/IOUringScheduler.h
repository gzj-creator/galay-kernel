#ifndef GALAY_KERNEL_IOURING_SCHEDULER_H
#define GALAY_KERNEL_IOURING_SCHEDULER_H

#include "Coroutine.h"
#include "IOScheduler.hpp"
#include "IOUringReactor.h"
#include "SchedulerCore.h"
#include "WakeCoordinator.h"

#ifdef USE_IOURING

#include <atomic>
#include <thread>
#include <cstdint>

#ifndef GALAY_SCHEDULER_QUEUE_DEPTH
#define GALAY_SCHEDULER_QUEUE_DEPTH 4096
#endif

#ifndef GALAY_SCHEDULER_BATCH_SIZE
#define GALAY_SCHEDULER_BATCH_SIZE 256
#endif

#ifndef GALAY_IOURING_WAIT_TIMEOUT_NS
#define GALAY_IOURING_WAIT_TIMEOUT_NS 10000000  // 10ms
#endif

namespace galay::kernel
{

struct SchedulerTestAccess;

class IOUringScheduler: public IOScheduler
{
public:
    IOUringScheduler(int queue_depth = GALAY_SCHEDULER_QUEUE_DEPTH,
                     int batch_size = GALAY_SCHEDULER_BATCH_SIZE);
    ~IOUringScheduler();

    IOUringScheduler(const IOUringScheduler&) = delete;
    IOUringScheduler& operator=(const IOUringScheduler&) = delete;

    void start();
    void stop();
    void notify();

    int addAccept(IOController* controller) override;
    int addConnect(IOController* controller) override;
    int addRecv(IOController* controller) override;
    int addSend(IOController* controller) override;
    int addReadv(IOController* controller) override;
    int addWritev(IOController* controller) override;
    int addClose(IOController* controller) override;
    int addFileRead(IOController* controller) override;
    int addFileWrite(IOController* controller) override;
    int addRecvFrom(IOController* controller) override;
    int addSendTo(IOController* controller) override;
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

private:
    std::atomic<bool> m_running;
    std::thread m_thread;

    int m_queue_depth;
    int m_batch_size;
    std::atomic<bool> m_sleeping{true};
    std::atomic<bool> m_wakeup_pending{false};

    IOSchedulerWorkerState m_worker;
    WakeCoordinator m_wake_coordinator;
    SchedulerCore m_core;
    IOUringReactor m_reactor;
    std::atomic<uint64_t> m_last_error_code{0};

private:
    void eventLoop();
    void processPendingCoroutines();
};

}

#endif // USE_IOURING

#endif
