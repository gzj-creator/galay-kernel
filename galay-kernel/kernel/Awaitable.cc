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

bool CloseAwaitable::CloseActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<void, IOError>& result) {
#ifdef USE_IOURING
    awaitable->m_sqe_type = CLOSE;
#endif
    auto scheduler = waker.getScheduler();
    if(scheduler->type() != kIOScheduler) {
        result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<IOScheduler*>(scheduler);
    int res = io_scheduler->addClose(controller);
    if(res == 0) {
        result = {};  // Success
        return false;
    }
    result = std::unexpected(IOError(kDisconnectError, errno));
    return false;
}

}