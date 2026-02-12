#ifndef GALAY_KERNEL_KQUEUE_SCHEDULER_H
#define GALAY_KERNEL_KQUEUE_SCHEDULER_H

#include "Coroutine.h"
#include "IOScheduler.hpp"

#ifdef USE_KQUEUE

#include <sys/event.h>
#include <vector>
#include <atomic>
#include <thread>
#include <concurrentqueue/moodycamel/concurrentqueue.h>

// Scheduler configuration macros
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

#define  OK 1


class KqueueScheduler: public IOScheduler
{
public:
    KqueueScheduler(int max_events = GALAY_SCHEDULER_MAX_EVENTS,
                    int batch_size = GALAY_SCHEDULER_BATCH_SIZE,
                    int check_interval_ms = GALAY_SCHEDULER_CHECK_INTERVAL_MS);
    ~KqueueScheduler();

    KqueueScheduler(const KqueueScheduler&) = delete;
    KqueueScheduler& operator=(const KqueueScheduler&) = delete;

    // Lifecycle
    void start() override;
    void stop() override;

    // Notify to wake up the scheduler
    void notify();

    // 返回1表示不用阻塞
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
    int addSendFile(IOController* controller) override;
    int addCustom(IOController* controller) override;

    int remove(IOController* controller) override;
    // Coroutine scheduling
    bool spawn(Coroutine coro) override;

    bool spawnImmidiately(Coroutine co) override;
protected:
    int m_kqueue_fd;
    std::atomic<bool> m_running;
    std::thread m_thread;

    // Configuration
    int m_max_events;
    int m_batch_size;
    int m_check_interval_ms;

    // Pipe for notification (kqueue uses pipe)
    int m_notify_pipe[2];
    // Lock-free queue for coroutines
    moodycamel::ConcurrentQueue<Coroutine> m_coro_queue;
    // Event buffer
    std::vector<struct kevent> m_events;
    // Coroutine buffer for batch processing
    std::vector<Coroutine> m_coro_buffer;

private:
    // Main controller loop
    void eventLoop();
    void processPendingCoroutines();
    void processEvent(struct kevent& ev);

    int processCustom(IOEventType type, IOController* controller);
};

}

#endif // USE_KQUEUE

#endif
