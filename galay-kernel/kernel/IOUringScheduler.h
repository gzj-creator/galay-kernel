#ifndef GALAY_KERNEL_IOURING_SCHEDULER_H
#define GALAY_KERNEL_IOURING_SCHEDULER_H

#include "Coroutine.h"
#include "IOScheduler.hpp"

#ifdef USE_IOURING

#include <liburing.h>

// liburing/io_uring.h 定义了 BLOCK_SIZE 宏，与 concurrentqueue 冲突
#ifdef BLOCK_SIZE
#undef BLOCK_SIZE
#endif

#include <vector>
#include <atomic>
#include <thread>
#include <concurrentqueue/moodycamel/concurrentqueue.h>

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

#define OK 1

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

    // 通知事件（用于SSL等自定义IO处理）
    int addRecvNotify(IOController* controller) override;
    int addSendNotify(IOController* controller) override;

    int remove(IOController* controller) override;

    bool spawn(Coroutine coro) override;

    bool spawnImmidiately(Coroutine co) override;

private:
    struct io_uring m_ring;
    std::atomic<bool> m_running;
    std::thread m_thread;

    int m_queue_depth;
    int m_batch_size;
    int m_event_fd;
    uint64_t m_eventfd_buf;  // eventfd 读取缓冲区

    moodycamel::ConcurrentQueue<Coroutine> m_coro_queue;
    std::vector<Coroutine> m_coro_buffer;

private:
    void eventLoop();
    void processPendingCoroutines();
    void processCompletion(struct io_uring_cqe* cqe);
};

}

#endif // USE_IOURING

#endif
