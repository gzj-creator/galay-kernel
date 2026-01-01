#include "Awaitable.h"
#include "common/Defn.hpp"
#include "kernel/Waker.h"
#include "KqueueScheduler.h"

namespace galay::kernel
{

bool AcceptAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    m_controller->fillAwaitable(ACCEPT, this);
    if(m_scheduler->addAccept(m_controller) == OK) {
        return false;
    }
    return true;
}

bool RecvAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    m_controller->fillAwaitable(RECV, this);
    if(m_scheduler->addRecv(m_controller) == OK) {
        return false;
    }
    return true;
}

bool SendAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    m_controller->fillAwaitable(SEND, this);
    if(m_scheduler->addSend(m_controller) == OK) {
        return false;
    }
    return true;
}

bool ConnectAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    m_controller->fillAwaitable(CONNECT, this);
    if(m_scheduler->addConnect(m_controller) == OK) {
        return false;
    }
    return true;
}

bool CloseAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    int result = m_scheduler->addClose(m_handle.fd);
    if(result == 0) {
        m_result = {};  // Success
        return false;
    }
    m_result = std::unexpected(IOError(kDisconnectError, errno));
    return false;
}

bool FileReadAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    m_controller->fillAwaitable(FILEREAD, this);
    if(m_scheduler->addFileRead(m_controller) == OK) {
        return false;
    }
    return true;
}

bool FileWriteAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_waker = Waker(handle);
    m_controller->fillAwaitable(FILEWRITE, this);
    if(m_scheduler->addFileWrite(m_controller) == OK) {
        return false;
    }
    return true;
}

}