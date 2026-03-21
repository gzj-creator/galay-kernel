#ifndef GALAY_KERNEL_KQUEUE_REACTOR_H
#define GALAY_KERNEL_KQUEUE_REACTOR_H

#include "BackendReactor.h"
#include "IOScheduler.hpp"
#include "WakeCoordinator.h"

#ifdef USE_KQUEUE

#include <sys/event.h>

#include <atomic>
#include <vector>

namespace galay::kernel {

class KqueueReactor : public BackendReactor
{
public:
    KqueueReactor(int max_events, std::atomic<uint64_t>& last_error_code);
    ~KqueueReactor() override;

    KqueueReactor(const KqueueReactor&) = delete;
    KqueueReactor& operator=(const KqueueReactor&) = delete;

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

    void poll(const struct timespec& timeout, WakeCoordinator& wake_coordinator);

private:
    void processEvent(struct kevent& ev);
    int syncSequenceRegistration(IOController* controller);
    int applySequenceInterest(IOController* controller, uint8_t desired_mask);

    int m_kqueue_fd = -1;
    int m_notify_pipe[2] = {-1, -1};
    int m_max_events = 0;
    std::vector<struct kevent> m_events;
    std::atomic<uint64_t>& m_last_error_code;
};

}  // namespace galay::kernel

#endif  // USE_KQUEUE

#endif  // GALAY_KERNEL_KQUEUE_REACTOR_H
