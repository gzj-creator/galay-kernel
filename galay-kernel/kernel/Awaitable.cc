#include "Awaitable.h"
#include "galay-kernel/common/Error.h"
#include <cerrno>

#ifdef USE_EPOLL
#include "EpollScheduler.h"
#elif defined(USE_KQUEUE)
#include "KqueueScheduler.h"
#elif defined(USE_IOURING)
#include "IOUringScheduler.h"
#endif

namespace galay::kernel
{

namespace {

template <typename AwaitableT, IOEventType Event, IOErrorCode ErrorCode, auto RegisterFn>
inline bool suspendRegisteredAwaitable(AwaitableT& awaitable, std::coroutine_handle<> handle) {
    awaitable.m_waker = Waker(handle);
#ifdef USE_IOURING
    awaitable.m_sqe_type = Event;
#endif
    awaitable.m_controller->fillAwaitable(Event, &awaitable);
    auto* scheduler = awaitable.m_waker.getScheduler();
    if (scheduler == nullptr || scheduler->type() != kIOScheduler) {
        awaitable.m_result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    const int ret = (static_cast<IOScheduler*>(scheduler)->*RegisterFn)(awaitable.m_controller);
    return detail::finalizeAwaitableAddResult(ret, ErrorCode, awaitable.m_result);
}

template <auto RegisterFn>
inline bool suspendCustomAwaitable(CustomAwaitable& awaitable, std::coroutine_handle<> handle) {
    awaitable.m_waker = Waker(handle);
#ifdef USE_IOURING
    awaitable.m_sqe_type = CUSTOM;
#endif
    awaitable.m_controller->fillAwaitable(CUSTOM, &awaitable);
    auto* scheduler = awaitable.m_waker.getScheduler();
    if (scheduler == nullptr || scheduler->type() != kIOScheduler) {
        return false;
    }
    const int ret = (static_cast<IOScheduler*>(scheduler)->*RegisterFn)(awaitable.m_controller);
    if (ret == 1) {
        return false;
    }
    if (ret < 0) {
        awaitable.m_error = IOError(kNotReady, detail::normalizeAwaitableErrno(ret));
        return false;
    }
    return true;
}

}  // namespace

// ============ await_suspend / await_resume implementations ============

bool AcceptAwaitable::await_suspend(std::coroutine_handle<> handle) {
    return suspendRegisteredAwaitable<AcceptAwaitable, ACCEPT, kAcceptFailed, &IOScheduler::addAccept>(*this, handle);
}

std::expected<GHandle, IOError> AcceptAwaitable::await_resume() {
    return detail::resumeIOAwaitable<ACCEPT>(*this);
}

bool RecvAwaitable::await_suspend(std::coroutine_handle<> handle) {
    return suspendRegisteredAwaitable<RecvAwaitable, RECV, kRecvFailed, &IOScheduler::addRecv>(*this, handle);
}

std::expected<size_t, IOError> RecvAwaitable::await_resume() {
    return detail::resumeIOAwaitable<RECV>(*this);
}

bool SendAwaitable::await_suspend(std::coroutine_handle<> handle) {
    return suspendRegisteredAwaitable<SendAwaitable, SEND, kSendFailed, &IOScheduler::addSend>(*this, handle);
}

std::expected<size_t, IOError> SendAwaitable::await_resume() {
    return detail::resumeIOAwaitable<SEND>(*this);
}

bool ReadvAwaitable::await_suspend(std::coroutine_handle<> handle) {
    return suspendRegisteredAwaitable<ReadvAwaitable, READV, kRecvFailed, &IOScheduler::addReadv>(*this, handle);
}

std::expected<size_t, IOError> ReadvAwaitable::await_resume() {
    return detail::resumeIOAwaitable<READV>(*this);
}

bool WritevAwaitable::await_suspend(std::coroutine_handle<> handle) {
    return suspendRegisteredAwaitable<WritevAwaitable, WRITEV, kSendFailed, &IOScheduler::addWritev>(*this, handle);
}

std::expected<size_t, IOError> WritevAwaitable::await_resume() {
    return detail::resumeIOAwaitable<WRITEV>(*this);
}

bool ConnectAwaitable::await_suspend(std::coroutine_handle<> handle) {
    return suspendRegisteredAwaitable<ConnectAwaitable, CONNECT, kConnectFailed, &IOScheduler::addConnect>(*this, handle);
}

std::expected<void, IOError> ConnectAwaitable::await_resume() {
    return detail::resumeIOAwaitable<CONNECT>(*this);
}

bool CloseAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    auto scheduler = m_waker.getScheduler();
    if (scheduler->type() != kIOScheduler) {
        m_result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    int res = io_scheduler->addClose(m_controller);
    if (res == 0) {
        m_result = {};
        return false;
    }
    m_result = std::unexpected(IOError(kDisconnectError, errno));
    return false;
}

std::expected<void, IOError> CloseAwaitable::await_resume() {
    return std::move(m_result);
}

bool FileReadAwaitable::await_suspend(std::coroutine_handle<> handle) {
    return suspendRegisteredAwaitable<FileReadAwaitable, FILEREAD, kReadFailed, &IOScheduler::addFileRead>(*this, handle);
}

std::expected<size_t, IOError> FileReadAwaitable::await_resume() {
    return detail::resumeIOAwaitable<FILEREAD>(*this);
}

bool FileWriteAwaitable::await_suspend(std::coroutine_handle<> handle) {
    return suspendRegisteredAwaitable<FileWriteAwaitable, FILEWRITE, kWriteFailed, &IOScheduler::addFileWrite>(*this, handle);
}

std::expected<size_t, IOError> FileWriteAwaitable::await_resume() {
    return detail::resumeIOAwaitable<FILEWRITE>(*this);
}

bool RecvFromAwaitable::await_suspend(std::coroutine_handle<> handle) {
    return suspendRegisteredAwaitable<RecvFromAwaitable, RECVFROM, kRecvFailed, &IOScheduler::addRecvFrom>(*this, handle);
}

std::expected<size_t, IOError> RecvFromAwaitable::await_resume() {
    return detail::resumeIOAwaitable<RECVFROM>(*this);
}

bool SendToAwaitable::await_suspend(std::coroutine_handle<> handle) {
    return suspendRegisteredAwaitable<SendToAwaitable, SENDTO, kSendFailed, &IOScheduler::addSendTo>(*this, handle);
}

std::expected<size_t, IOError> SendToAwaitable::await_resume() {
    return detail::resumeIOAwaitable<SENDTO>(*this);
}

bool FileWatchAwaitable::await_suspend(std::coroutine_handle<> handle) {
    return suspendRegisteredAwaitable<FileWatchAwaitable, FILEWATCH, kReadFailed, &IOScheduler::addFileWatch>(*this, handle);
}

std::expected<FileWatchResult, IOError> FileWatchAwaitable::await_resume() {
    return detail::resumeIOAwaitable<FILEWATCH>(*this);
}

bool SendFileAwaitable::await_suspend(std::coroutine_handle<> handle) {
    return suspendRegisteredAwaitable<SendFileAwaitable, SENDFILE, kSendFailed, &IOScheduler::addSendFile>(*this, handle);
}

std::expected<size_t, IOError> SendFileAwaitable::await_resume() {
    return detail::resumeIOAwaitable<SENDFILE>(*this);
}

bool CustomAwaitable::await_suspend(std::coroutine_handle<> handle) {
    return suspendCustomAwaitable<&IOScheduler::addCustom>(*this, handle);
}

}
