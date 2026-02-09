#include "Awaitable.h"
#include "common/Defn.hpp"
#include "galay-kernel/common/Error.h"
#include "galay-kernel/kernel/Waker.h"

#ifdef USE_EPOLL
#include "EpollScheduler.h"
#elif defined(USE_KQUEUE)
#include "KqueueScheduler.h"
#elif defined(USE_IOURING)
#include "IOUringScheduler.h"
#endif

namespace galay::kernel
{

bool AcceptAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return AcceptActionSuspend(this, m_controller, m_waker, m_result);
}

std::expected<GHandle, IOError> AcceptAwaitable::await_resume() {
    AcceptActionResume(m_controller);
    return std::move(m_result);
}

bool RecvAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return RecvActionSuspend(this, m_controller, m_waker, m_result);
}

std::expected<Bytes, IOError> RecvAwaitable::await_resume() {
    RecvActionResume(m_controller);
    return std::move(m_result);
}

bool SendAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return SendActionSuspend(this, m_controller, m_waker, m_result);
}

std::expected<size_t, IOError> SendAwaitable::await_resume() {
    SendActionResume(m_controller);
    return std::move(m_result);
}

bool ReadvAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return ReadvActionSuspend(this, m_controller, m_waker, m_result);
}

std::expected<size_t, IOError> ReadvAwaitable::await_resume() {
    ReadvActionResume(m_controller);
    return std::move(m_result);
}

bool WritevAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return WritevActionSuspend(this, m_controller, m_waker, m_result);
}

std::expected<size_t, IOError> WritevAwaitable::await_resume() {
    WritevActionResume(m_controller);
    return std::move(m_result);
}

bool ConnectAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    return ConnectActionSuspend(this, m_controller, m_waker, m_result);
}

std::expected<void, IOError> ConnectAwaitable::await_resume() {
    ConnectActionResume(m_controller);
    return std::move(m_result);
}

bool CloseAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    auto scheduler = m_waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        m_result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    int result = io_scheduler->addClose(m_controller);
    if(result == 0) {
        m_result = {};  // Success
        return false;
    }
    m_result = std::unexpected(IOError(kDisconnectError, errno));
    return false;
}

std::expected<void, IOError> CloseAwaitable::await_resume() {
    return std::move(m_result);
}

bool FileReadAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = FILEREAD;
#endif
    m_waker = Waker(handle);
    return FileReadActionSuspend(this, m_controller, m_waker, m_result);
}

std::expected<Bytes, IOError> FileReadAwaitable::await_resume() {
    FileReadActionResume(m_controller);
    return std::move(m_result);
}

bool FileWriteAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = FILEWRITE;
#endif
    m_waker = Waker(handle);
    return FileWriteActionSuspend(this, m_controller, m_waker, m_result);
}

std::expected<size_t, IOError> FileWriteAwaitable::await_resume() {
    FileWriteActionResume(m_controller);
    return std::move(m_result);
}

bool RecvFromAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = RECVFROM;
#endif
    m_waker = Waker(handle);
    return RecvFromActionSuspend(this, m_controller, m_waker, m_result);
}

std::expected<Bytes, IOError> RecvFromAwaitable::await_resume() {
    RecvFromActionResume(m_controller);
    return std::move(m_result);
}

bool SendToAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = SENDTO;
#endif
    m_waker = Waker(handle);
    return SendToActionSuspend(this, m_controller, m_waker, m_result);
}

std::expected<size_t, IOError> SendToAwaitable::await_resume() {
    SendToActionResume(m_controller);
    return std::move(m_result);
}

bool FileWatchAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = FILEWATCH;
#endif
    m_waker = Waker(handle);
    return FileWatchActionSuspend(this, m_controller, m_waker, m_result);
}

std::expected<FileWatchResult, IOError> FileWatchAwaitable::await_resume() {
    FileWatchActionResume(m_controller);
    return std::move(m_result);
}

bool RecvNotifyAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = RECV_NOTIFY;
#endif
    m_waker = Waker(handle);
    return RecvNotifyActionSuspend(this, m_controller, m_waker);
}

void RecvNotifyAwaitable::await_resume() {
    RecvNotifyActionResume(m_controller);
}

bool SendNotifyAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = SEND_NOTIFY;
#endif
    m_waker = Waker(handle);
    return SendNotifyActionSuspend(this, m_controller, m_waker);
}

void SendNotifyAwaitable::await_resume() {
    SendNotifyActionResume(m_controller);
}

bool SendFileAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = SENDFILE;
#endif
    m_waker = Waker(handle);
    return SendFileActionSuspend(this, m_controller, m_waker, m_result);
}

std::expected<size_t, IOError> SendFileAwaitable::await_resume() {
    SendFileActionResume(m_controller);
    return std::move(m_result);
}

// ============ Action Function Implementations ============

bool AcceptAwaitable::AcceptActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<GHandle, IOError>& result) {
#ifdef USE_IOURING
    awaitable->m_sqe_type = ACCEPT;
#endif
    controller->fillAwaitable(ACCEPT, awaitable);
    auto scheduler = waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addAccept(controller) == OK) {
        return false;
    }
    return true;
}

void AcceptAwaitable::AcceptActionResume(IOController* controller) {
    controller->removeAwaitable(ACCEPT);
}

bool RecvAwaitable::RecvActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<Bytes, IOError>& result) {
#ifdef USE_IOURING
    awaitable->m_sqe_type = RECV;
#endif
    controller->fillAwaitable(RECV, awaitable);
    auto scheduler = waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addRecv(controller) == OK) {
        return false;
    }
    return true;
}

void RecvAwaitable::RecvActionResume(IOController* controller) {
    controller->removeAwaitable(RECV);
}

bool SendAwaitable::SendActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<size_t, IOError>& result) {
#ifdef USE_IOURING
    awaitable->m_sqe_type = SEND;
#endif
    controller->fillAwaitable(SEND, awaitable);
    auto scheduler = waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addSend(controller) == OK) {
        return false;
    }
    return true;
}

void SendAwaitable::SendActionResume(IOController* controller) {
    controller->removeAwaitable(SEND);
}

bool ReadvAwaitable::ReadvActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<size_t, IOError>& result) {
#ifdef USE_IOURING
    awaitable->m_sqe_type = READV;
#endif
    controller->fillAwaitable(READV, awaitable);
    auto scheduler = waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addReadv(controller) == OK) {
        return false;
    }
    return true;
}

void ReadvAwaitable::ReadvActionResume(IOController* controller) {
    controller->removeAwaitable(READV);
}

bool WritevAwaitable::WritevActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<size_t, IOError>& result) {
#ifdef USE_IOURING
    awaitable->m_sqe_type = WRITEV;
#endif
    controller->fillAwaitable(WRITEV, awaitable);
    auto scheduler = waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addWritev(controller) == OK) {
        return false;
    }
    return true;
}

void WritevAwaitable::WritevActionResume(IOController* controller) {
    controller->removeAwaitable(WRITEV);
}

bool ConnectAwaitable::ConnectActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<void, IOError>& result) {
#ifdef USE_IOURING
    awaitable->m_sqe_type = CONNECT;
#endif
    controller->fillAwaitable(CONNECT, awaitable);
    auto scheduler = waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addConnect(controller) == OK) {
        return false;
    }
    return true;
}

void ConnectAwaitable::ConnectActionResume(IOController* controller) {
    controller->removeAwaitable(CONNECT);
}

bool FileReadAwaitable::FileReadActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<Bytes, IOError>& result) {
#ifdef USE_IOURING
    awaitable->m_sqe_type = FILEREAD;
#endif
    controller->fillAwaitable(FILEREAD, awaitable);
    auto scheduler = waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addFileRead(controller) == OK) {
        return false;
    }
    return true;
}

void FileReadAwaitable::FileReadActionResume(IOController* controller) {
    controller->removeAwaitable(FILEREAD);
}

bool FileWriteAwaitable::FileWriteActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<size_t, IOError>& result) {
#ifdef USE_IOURING
    awaitable->m_sqe_type = FILEWRITE;
#endif
    controller->fillAwaitable(FILEWRITE, awaitable);
    auto scheduler = waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addFileWrite(controller) == OK) {
        return false;
    }
    return true;
}

void FileWriteAwaitable::FileWriteActionResume(IOController* controller) {
    controller->removeAwaitable(FILEWRITE);
}

bool RecvFromAwaitable::RecvFromActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<Bytes, IOError>& result) {
#ifdef USE_IOURING
    awaitable->m_sqe_type = RECVFROM;
#endif
    controller->fillAwaitable(RECVFROM, awaitable);
    auto scheduler = waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addRecvFrom(controller) == OK) {
        return false;
    }
    return true;
}

void RecvFromAwaitable::RecvFromActionResume(IOController* controller) {
    controller->removeAwaitable(RECVFROM);
}

bool SendToAwaitable::SendToActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<size_t, IOError>& result) {
#ifdef USE_IOURING
    awaitable->m_sqe_type = SENDTO;
#endif
    controller->fillAwaitable(SENDTO, awaitable);
    auto scheduler = waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addSendTo(controller) == OK) {
        return false;
    }
    return true;
}

void SendToAwaitable::SendToActionResume(IOController* controller) {
    controller->removeAwaitable(SENDTO);
}

bool FileWatchAwaitable::FileWatchActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<FileWatchResult, IOError>& result) {
#ifdef USE_IOURING
    awaitable->m_sqe_type = FILEWATCH;
#endif
    controller->fillAwaitable(FILEWATCH, awaitable);
    auto scheduler = waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addFileWatch(controller) == OK) {
        return false;
    }
    return true;
}

void FileWatchAwaitable::FileWatchActionResume(IOController* controller) {
    controller->removeAwaitable(FILEWATCH);
}

bool RecvNotifyAwaitable::RecvNotifyActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker) {
#ifdef USE_IOURING
    awaitable->m_sqe_type = RECV_NOTIFY;
#endif
    controller->fillAwaitable(RECV_NOTIFY, awaitable);
    auto scheduler = waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addRecvNotify(controller) < 0) {
        return false;
    }
    return true;
}

void RecvNotifyAwaitable::RecvNotifyActionResume(IOController* controller) {
    controller->removeAwaitable(RECV_NOTIFY);
}

bool SendNotifyAwaitable::SendNotifyActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker) {
#ifdef USE_IOURING
    awaitable->m_sqe_type = SEND_NOTIFY;
#endif
    controller->fillAwaitable(SEND_NOTIFY, awaitable);
    auto scheduler = waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addSendNotify(controller) < 0) {
        return false;
    }
    return true;
}

void SendNotifyAwaitable::SendNotifyActionResume(IOController* controller) {
    controller->removeAwaitable(SEND_NOTIFY);
}

bool SendFileAwaitable::SendFileActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<size_t, IOError>& result) {
#ifdef USE_IOURING
    awaitable->m_sqe_type = SENDFILE;
#endif
    controller->fillAwaitable(SENDFILE, awaitable);
    auto scheduler = waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addSendFile(controller) == OK) {
        return false;
    }
    return true;
}

void SendFileAwaitable::SendFileActionResume(IOController* controller) {
    controller->removeAwaitable(SENDFILE);
}

}