#ifndef GALAY_KERNEL_KQUEUE_SCHEDULER_H
#define GALAY_KERNEL_KQUEUE_SCHEDULER_H

#include "Coroutine.h"
#include "galay-kernel/common/Defn.hpp"
#include "Scheduler.h"

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
    void start();
    void stop();

    // Notify to wake up the scheduler
    void notify();

    // 返回1表示不用阻塞
    int addAccept(IOController* event) override;
    int addConnect(IOController* event) override;
    int addRecv(IOController* event) override;
    int addSend(IOController* event) override;
    int addClose(int fd) override;
    int addFileRead(IOController* event) override;
    int addFileWrite(IOController* event) override;
    int addRecvFrom(IOController* event) override;
    int addSendTo(IOController* event) override;
    int addFileWatch(IOController* event) override;

    // Remove events (only need fd)
    int remove(int fd);

    // Coroutine scheduling
    void spawn(Coroutine coro) override;

private:
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

    bool handleAccept(IOController* controller);
    bool handleConnect(IOController* controller);
    bool handleRecv(IOController* controller);
    bool handleSend(IOController* controller);
    bool handleFileRead(IOController* controller);
    bool handleFileWrite(IOController* controller);
    bool handleRecvFrom(IOController* controller);
    bool handleSendTo(IOController* controller);

    // Main event loop
    void eventLoop();
    void processPendingCoroutines();
    void processEvent(struct kevent& ev);
};

}

#endif // USE_KQUEUE

#endif
