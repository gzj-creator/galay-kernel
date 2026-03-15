#ifndef GALAY_KERNEL_EPOLL_REACTOR_H
#define GALAY_KERNEL_EPOLL_REACTOR_H

#include "BackendReactor.h"
#include "IOScheduler.hpp"
#include "WakeCoordinator.h"

#ifdef USE_EPOLL

#include <sys/epoll.h>

#include <atomic>
#include <vector>

namespace galay::kernel {

class EpollReactor : public BackendReactor
{
public:
    EpollReactor(int max_events, std::atomic<uint64_t>& last_error_code);
    ~EpollReactor() override;

    EpollReactor(const EpollReactor&) = delete;
    EpollReactor& operator=(const EpollReactor&) = delete;

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

    void poll(int timeout_ms, WakeCoordinator& wake_coordinator);

private:
    uint32_t buildEvents(IOController* controller) const;
    int applyEvents(IOController* controller, uint32_t events);
    int processSequence(IOEventType type, IOController* controller);
    void processEvent(struct epoll_event& ev);
    void syncEvents(IOController* controller);

    int m_epoll_fd = -1;
    int m_event_fd = -1;
    int m_max_events = 0;
    std::vector<struct epoll_event> m_events;
    std::atomic<uint64_t>& m_last_error_code;
};

}  // namespace galay::kernel

#endif  // USE_EPOLL

#endif  // GALAY_KERNEL_EPOLL_REACTOR_H
