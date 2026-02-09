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
#ifdef USE_IOURING
    m_sqe_type = ACCEPT;
#endif
    m_waker = Waker(handle);
    m_controller->fillAwaitable(ACCEPT, this);
    auto scheduler = m_waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        m_result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addAccept(m_controller) == OK) {
        return false;
    }
    return true;
}

std::expected<GHandle, IOError> AcceptAwaitable::await_resume() {
    m_controller->removeAwaitable(ACCEPT);
    return std::move(m_result);
}

bool RecvAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = RECV;
#endif
    m_waker = Waker(handle);
    m_controller->fillAwaitable(RECV, this);
    auto scheduler = m_waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        m_result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addRecv(m_controller) == OK) {
        return false;
    }
    return true;
}

std::expected<Bytes, IOError> RecvAwaitable::await_resume() {
    m_controller->removeAwaitable(RECV);
    return std::move(m_result);
}

bool SendAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = SEND;
#endif
    m_waker = Waker(handle);
    m_controller->fillAwaitable(SEND, this);
    auto scheduler = m_waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        m_result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addSend(m_controller) == OK) {
        return false;
    }
    return true;
}

std::expected<size_t, IOError> SendAwaitable::await_resume() {
    m_controller->removeAwaitable(SEND);
    return std::move(m_result);
}

bool ReadvAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = READV;
#endif
    m_waker = Waker(handle);
    m_controller->fillAwaitable(READV, this);
    auto scheduler = m_waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        m_result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addReadv(m_controller) == OK) {
        return false;
    }
    return true;
}

std::expected<size_t, IOError> ReadvAwaitable::await_resume() {
    m_controller->removeAwaitable(READV);
    return std::move(m_result);
}

bool WritevAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = WRITEV;
#endif
    m_waker = Waker(handle);
    m_controller->fillAwaitable(WRITEV, this);
    auto scheduler = m_waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        m_result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addWritev(m_controller) == OK) {
        return false;
    }
    return true;
}

std::expected<size_t, IOError> WritevAwaitable::await_resume() {
    m_controller->removeAwaitable(WRITEV);
    return std::move(m_result);
}

bool ConnectAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = CONNECT;
#endif
    m_waker = Waker(handle);
    m_controller->fillAwaitable(CONNECT, this);
    auto scheduler = m_waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        m_result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addConnect(m_controller) == OK) {
        return false;
    }
    return true;
}

std::expected<void, IOError> ConnectAwaitable::await_resume() {
    m_controller->removeAwaitable(CONNECT);
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
    m_controller->fillAwaitable(FILEREAD, this);
    auto scheduler = m_waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        m_result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addFileRead(m_controller) == OK) {
        return false;
    }
    return true;
}

std::expected<Bytes, IOError> FileReadAwaitable::await_resume() {
    m_controller->removeAwaitable(FILEREAD);
    return std::move(m_result);
}

bool FileWriteAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = FILEWRITE;
#endif
    m_waker = Waker(handle);
    m_controller->fillAwaitable(FILEWRITE, this);
    auto scheduler = m_waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        m_result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addFileWrite(m_controller) == OK) {
        return false;
    }
    return true;
}

std::expected<size_t, IOError> FileWriteAwaitable::await_resume() {
    m_controller->removeAwaitable(FILEWRITE);
    return std::move(m_result);
}

bool RecvFromAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = RECVFROM;
#endif
    m_waker = Waker(handle);
    m_controller->fillAwaitable(RECVFROM, this);
    auto scheduler = m_waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        m_result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addRecvFrom(m_controller) == OK) {
        return false;
    }
    return true;
}

std::expected<Bytes, IOError> RecvFromAwaitable::await_resume() {
    m_controller->removeAwaitable(RECVFROM);
    return std::move(m_result);
}

bool SendToAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = SENDTO;
#endif
    m_waker = Waker(handle);
    m_controller->fillAwaitable(SENDTO, this);
    auto scheduler = m_waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        m_result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addSendTo(m_controller) == OK) {
        return false;
    }
    return true;
}

std::expected<size_t, IOError> SendToAwaitable::await_resume() {
    m_controller->removeAwaitable(SENDTO);
    return std::move(m_result);
}

bool FileWatchAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = FILEWATCH;
#endif
    m_waker = Waker(handle);
    m_controller->fillAwaitable(FILEWATCH, this);
    auto scheduler = m_waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        m_result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addFileWatch(m_controller) == OK) {
        return false;
    }
    return true;
}

std::expected<FileWatchResult, IOError> FileWatchAwaitable::await_resume() {
    m_controller->removeAwaitable(FILEWATCH);
    return std::move(m_result);
}

bool RecvNotifyAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = RECV_NOTIFY;
#endif
    m_waker = Waker(handle);
    m_controller->fillAwaitable(RECV_NOTIFY, this);
    auto scheduler = m_waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addRecvNotify(m_controller) < 0) {
        return false;
    }
    return true;
}

void RecvNotifyAwaitable::await_resume() {
    m_controller->removeAwaitable(RECV_NOTIFY);
}

bool SendNotifyAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = SEND_NOTIFY;
#endif
    m_waker = Waker(handle);
    m_controller->fillAwaitable(SEND_NOTIFY, this);
    auto scheduler = m_waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addSendNotify(m_controller) < 0) {
        return false;
    }
    return true;
}

void SendNotifyAwaitable::await_resume() {
    m_controller->removeAwaitable(SEND_NOTIFY);
}

bool SendFileAwaitable::await_suspend(std::coroutine_handle<> handle) {
#ifdef USE_IOURING
    m_sqe_type = SENDFILE;
#endif
    m_waker = Waker(handle);
    m_controller->fillAwaitable(SENDFILE, this);
    auto scheduler = m_waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        m_result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    if(io_scheduler->addSendFile(m_controller) == OK) {
        return false;
    }
    return true;
}

std::expected<size_t, IOError> SendFileAwaitable::await_resume() {
    m_controller->removeAwaitable(SENDFILE);
    return std::move(m_result);
}

}