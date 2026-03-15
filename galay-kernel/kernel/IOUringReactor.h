#ifndef GALAY_KERNEL_IOURING_REACTOR_H
#define GALAY_KERNEL_IOURING_REACTOR_H

#include "BackendReactor.h"
#include "IOScheduler.hpp"
#include "WakeCoordinator.h"

#ifdef USE_IOURING

#include <liburing.h>

// liburing/io_uring.h 定义了 BLOCK_SIZE 宏，与 concurrentqueue 冲突
#ifdef BLOCK_SIZE
#undef BLOCK_SIZE
#endif

#include <atomic>
#include <cstdint>

namespace galay::kernel {

class IOUringReactor : public BackendReactor
{
public:
    IOUringReactor(int queue_depth, std::atomic<uint64_t>& last_error_code);
    ~IOUringReactor() override;

    IOUringReactor(const IOUringReactor&) = delete;
    IOUringReactor& operator=(const IOUringReactor&) = delete;

    void notify() override;
    int wakeReadFdForTest() const override;

    int addAccept(IOController* controller);
    int addConnect(IOController* controller);
    int addRecv(IOController* controller);
    int addSend(IOController* controller);
    int addReadv(IOController* controller);
    int addWritev(IOController* controller);
    int addClose(IOController* controller);
    int addFileRead(IOController* controller);
    int addFileWrite(IOController* controller);
    int addRecvFrom(IOController* controller);
    int addSendTo(IOController* controller);
    int addFileWatch(IOController* controller);
    int addSendFile(IOController* controller);
    int addSequence(IOController* controller);
    int remove(IOController* controller);

    void poll(uint64_t timeout_ns, WakeCoordinator& wake_coordinator);

private:
    int submitSequenceSqe(IOEventType type, IOContextBase* ctx, IOController* controller);
    void processCompletion(struct io_uring_cqe* cqe);
    void ensureWakeReadArmed();

    struct io_uring m_ring {};
    int m_queue_depth = 0;
    int m_event_fd = -1;
    uint64_t m_eventfd_buf = 0;
    bool m_wake_read_armed = false;
    std::atomic<uint64_t>& m_last_error_code;
};

}  // namespace galay::kernel

#endif  // USE_IOURING

#endif  // GALAY_KERNEL_IOURING_REACTOR_H
