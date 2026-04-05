#include "Awaitable.h"
#include "galay-kernel/common/Error.h"
#include "IOScheduler.hpp"
#include <cerrno>

namespace galay::kernel
{

namespace detail
{

int registerIOSchedulerEvent(Scheduler* scheduler,
                             IOEventType event,
                             IOController* controller) noexcept
{
    auto* io_scheduler = static_cast<IOScheduler*>(scheduler);
    switch (event) {
    case ACCEPT:
        return io_scheduler->addAccept(controller);
    case CONNECT:
        return io_scheduler->addConnect(controller);
    case RECV:
        return io_scheduler->addRecv(controller);
    case SEND:
        return io_scheduler->addSend(controller);
    case READV:
        return io_scheduler->addReadv(controller);
    case WRITEV:
        return io_scheduler->addWritev(controller);
    case SENDFILE:
        return io_scheduler->addSendFile(controller);
    case FILEREAD:
        return io_scheduler->addFileRead(controller);
    case FILEWRITE:
        return io_scheduler->addFileWrite(controller);
    case FILEWATCH:
        return io_scheduler->addFileWatch(controller);
    case RECVFROM:
        return io_scheduler->addRecvFrom(controller);
    case SENDTO:
        return io_scheduler->addSendTo(controller);
    case SEQUENCE:
        return io_scheduler->addSequence(controller);
    default:
        return -EINVAL;
    }
}

int registerIOSchedulerClose(Scheduler* scheduler,
                             IOController* controller) noexcept
{
    return static_cast<IOScheduler*>(scheduler)->addClose(controller);
}

} // namespace detail

std::expected<GHandle, IOError> AcceptAwaitable::await_resume() {
#ifdef USE_IOURING
    m_controller->m_accept_result_assigned = false;
#endif
    return detail::resumeIOAwaitable<ACCEPT>(*this);
}

std::expected<size_t, IOError> RecvAwaitable::await_resume() {
#ifdef USE_IOURING
    m_controller->m_recv_result_assigned = false;
#endif
    return detail::resumeIOAwaitable<RECV>(*this);
}

std::expected<size_t, IOError> SendAwaitable::await_resume() {
    return detail::resumeIOAwaitable<SEND>(*this);
}

std::expected<size_t, IOError> ReadvAwaitable::await_resume() {
    return detail::resumeIOAwaitable<READV>(*this);
}

std::expected<size_t, IOError> WritevAwaitable::await_resume() {
    return detail::resumeIOAwaitable<WRITEV>(*this);
}

std::expected<void, IOError> ConnectAwaitable::await_resume() {
    return detail::resumeIOAwaitable<CONNECT>(*this);
}

std::expected<void, IOError> CloseAwaitable::await_resume() {
    return std::move(m_result);
}

std::expected<size_t, IOError> FileReadAwaitable::await_resume() {
    return detail::resumeIOAwaitable<FILEREAD>(*this);
}

std::expected<size_t, IOError> FileWriteAwaitable::await_resume() {
    return detail::resumeIOAwaitable<FILEWRITE>(*this);
}

std::expected<size_t, IOError> RecvFromAwaitable::await_resume() {
    return detail::resumeIOAwaitable<RECVFROM>(*this);
}

std::expected<size_t, IOError> SendToAwaitable::await_resume() {
    return detail::resumeIOAwaitable<SENDTO>(*this);
}

std::expected<FileWatchResult, IOError> FileWatchAwaitable::await_resume() {
    return detail::resumeIOAwaitable<FILEWATCH>(*this);
}

std::expected<size_t, IOError> SendFileAwaitable::await_resume() {
    return detail::resumeIOAwaitable<SENDFILE>(*this);
}

}
