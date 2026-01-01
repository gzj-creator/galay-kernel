#ifndef GALAY_KERNEL_IOURING_SCHEDULER_H
#define GALAY_KERNEL_IOURING_SCHEDULER_H

#include "Coroutine.h"
#include "galay-kernel/common/Defn.hpp"
#include "Scheduler.h"

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

    int addAccept(IOController* event) override;
    int addConnect(IOController* event) override;
    int addRecv(IOController* event) override;
    int addSend(IOController* event) override;
    int addClose(int fd) override;
    int addFileRead(IOController* event) override;
    int addFileWrite(IOController* event) override;

    int remove(int fd);

    void spawn(Coroutine coro) override;

private:
    struct io_uring m_ring;
    std::atomic<bool> m_running;
    std::thread m_thread;

    int m_queue_depth;
    int m_batch_size;
    int m_event_fd;

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
