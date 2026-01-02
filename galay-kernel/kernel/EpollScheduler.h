#ifndef GALAY_KERNEL_EPOLL_SCHEDULER_H
#define GALAY_KERNEL_EPOLL_SCHEDULER_H

#include "Coroutine.h"
#include "galay-kernel/common/Defn.hpp"
#include "Scheduler.h"

#ifdef USE_EPOLL

#include <sys/epoll.h>
#include <libaio.h>
#include <vector>
#include <atomic>
#include <thread>
#include <concurrentqueue/moodycamel/concurrentqueue.h>

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

#define OK 1

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
    int addAccept(IOController* event) override;
    int addConnect(IOController* event) override;
    int addRecv(IOController* event) override;
    int addSend(IOController* event) override;
    int addClose(int fd) override;

    // 文件 IO (通过 libaio + eventfd)
    int addFileRead(IOController* event) override;
    int addFileWrite(IOController* event) override;

    // UDP IO
    int addRecvFrom(IOController* event) override;
    int addSendTo(IOController* event) override;

    // 文件监控 (通过 inotify)
    int addFileWatch(IOController* event) override;

    int remove(int fd);

    void spawn(Coroutine coro) override;

private:
    int m_epoll_fd;
    std::atomic<bool> m_running;
    std::thread m_thread;

    int m_max_events;
    int m_batch_size;
    int m_check_interval_ms;

    int m_event_fd;

    moodycamel::ConcurrentQueue<Coroutine> m_coro_queue;
    std::vector<struct epoll_event> m_events;
    std::vector<Coroutine> m_coro_buffer;

private:
    bool handleAccept(IOController* controller);
    bool handleConnect(IOController* controller);
    bool handleRecv(IOController* controller);
    bool handleSend(IOController* controller);
    void handleFileRead(IOController* controller);
    void handleFileWrite(IOController* controller);
    bool handleRecvFrom(IOController* controller);
    bool handleSendTo(IOController* controller);

    void eventLoop();
    void processPendingCoroutines();
    void processEvent(struct epoll_event& ev);
};

}

#endif // USE_EPOLL

#endif
