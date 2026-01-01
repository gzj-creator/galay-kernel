#include "KqueueScheduler.h"
#include "Scheduler.h"
#include "common/Defn.hpp"
#include "common/Error.h"

#ifdef USE_KQUEUE

#include "Awaitable.h"
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <stdexcept>

namespace galay::kernel
{

KqueueScheduler::KqueueScheduler(int max_events, int batch_size, int check_interval_ms)
    : m_kqueue_fd(kqueue())
    , m_running(false)
    , m_max_events(max_events)
    , m_batch_size(batch_size)
    , m_check_interval_ms(check_interval_ms)
{
    if (m_kqueue_fd == -1) {
        throw std::runtime_error("Failed to create kqueue");
    }

    // Create notification pipe
    if (pipe(m_notify_pipe) == -1) {
        close(m_kqueue_fd);
        throw std::runtime_error("Failed to create notification pipe");
    }

    // Set pipe non-blocking
    int flags = fcntl(m_notify_pipe[0], F_GETFL, 0);
    fcntl(m_notify_pipe[0], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(m_notify_pipe[1], F_GETFL, 0);
    fcntl(m_notify_pipe[1], F_SETFL, flags | O_NONBLOCK);

    // Add pipe read end to kqueue for notification
    struct kevent ev;
    EV_SET(&ev, m_notify_pipe[0], EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);

    m_events.resize(m_max_events);
    m_coro_buffer.resize(m_batch_size);
}

KqueueScheduler::~KqueueScheduler()
{
    stop();
    if (m_kqueue_fd != -1) {
        close(m_kqueue_fd);
    }
    close(m_notify_pipe[0]);
    close(m_notify_pipe[1]);
}

void KqueueScheduler::start()
{
    if (m_running.exchange(true, std::memory_order_acq_rel)) {
        return; // Already running
    }

    m_thread = std::thread([this]() {
        eventLoop();
    });
}

void KqueueScheduler::stop()
{
    if (!m_running.exchange(false, std::memory_order_acq_rel)) {
        return; // Already stopped
    }

    notify();

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void KqueueScheduler::notify()
{
    char buf = 1;
    write(m_notify_pipe[1], &buf, 1);
}

int KqueueScheduler::addAccept(IOController* event)
{
    if(handleAccept(event)) {
        return OK;
    }
    AcceptAwaitable* awaitable = static_cast<AcceptAwaitable*>(event->m_awaitable);
    struct kevent ev;
    EV_SET(&ev, awaitable->m_listen_handle.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, event);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addConnect(IOController* controller)
{
    // 先尝试立即连接
    if (handleConnect(controller)) {
        return OK;
    }

    // 连接正在进行中，注册到 kqueue
    ConnectAwaitable* awaitable = static_cast<ConnectAwaitable*>(controller->m_awaitable);
    struct kevent ev;
    EV_SET(&ev, awaitable->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addRecv(IOController* controller)
{
    // 先尝试立即接收
    if (handleRecv(controller)) {
        return OK;
    }

    // 需要等待数据，注册到 kqueue
    RecvAwaitable* awaitable = static_cast<RecvAwaitable*>(controller->m_awaitable);
    struct kevent ev;
    EV_SET(&ev, awaitable->m_handle.fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addSend(IOController* controller)
{
    // 先尝试立即发送
    if (handleSend(controller)) {
        return OK;
    }

    // 需要等待可写，注册到 kqueue
    SendAwaitable* awaitable = static_cast<SendAwaitable*>(controller->m_awaitable);
    struct kevent ev;
    EV_SET(&ev, awaitable->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addClose(int fd)
{
    close(fd);
    remove(fd);
    return 0;
}

int KqueueScheduler::addFileRead(IOController* controller)
{
    if (handleFileRead(controller)) {
        return OK;
    }
    FileReadAwaitable* awaitable = static_cast<FileReadAwaitable*>(controller->m_awaitable);
    struct kevent ev;
    EV_SET(&ev, awaitable->m_handle.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addFileWrite(IOController* controller)
{
    if (handleFileWrite(controller)) {
        return OK;
    }
    FileWriteAwaitable* awaitable = static_cast<FileWriteAwaitable*>(controller->m_awaitable);
    struct kevent ev;
    EV_SET(&ev, awaitable->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::remove(int fd)
{
    struct kevent evs[2];
    EV_SET(&evs[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&evs[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    return kevent(m_kqueue_fd, evs, 2, nullptr, 0, nullptr);
}

void KqueueScheduler::spawn(Coroutine coro)
{
    m_coro_queue.enqueue(std::move(coro));
}

void KqueueScheduler::processPendingCoroutines()
{
    size_t count = m_coro_queue.try_dequeue_bulk(m_coro_buffer.data(), m_batch_size);
    for (size_t i = 0; i < count; ++i) {
       Scheduler::resume( m_coro_buffer[i]);
    }
}



void KqueueScheduler::eventLoop()
{
    while (m_running.load(std::memory_order_acquire)) {
        // Process pending coroutines
        processPendingCoroutines();

        // Wait for events with timeout
        struct timespec timeout;
        timeout.tv_sec = 0;
        timeout.tv_nsec = m_check_interval_ms * 1000000; // Convert ms to ns

        int nev = kevent(m_kqueue_fd, nullptr, 0, m_events.data(), m_max_events, &timeout);

        if (nev < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        for (int i = 0; i < nev; ++i) {
            struct kevent& ev = m_events[i];
            if (ev.ident == static_cast<uintptr_t>(m_notify_pipe[0])) {
                char buf[256];
                while (read(m_notify_pipe[0], buf, sizeof(buf)) > 0);
                continue;
            }
            if (ev.flags & EV_ERROR) {
                continue;
            }
            if (!ev.udata) {
                continue;
            }
            processEvent(ev);
        }
    }
}

void KqueueScheduler::processEvent(struct kevent& ev)
{
    IOController* controller = static_cast<IOController*>(ev.udata);
    if (!controller || controller->m_type == IOEventType::INVALID || controller->m_awaitable == nullptr) {
        return;
    }

    // 检查错误标志
    if (ev.flags & EV_ERROR) {
        // 处理错误
        return;
    }

    switch (controller->m_type)
    {
    case ACCEPT:
    {
        // Accept 需要 EVFILT_READ 事件
        if (ev.filter == EVFILT_READ) {
            handleAccept(controller);
            AcceptAwaitable* awaitable = static_cast<AcceptAwaitable*>(controller->m_awaitable);
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case CONNECT:
    {
        // Connect 需要 EVFILT_WRITE 事件
        if (ev.filter == EVFILT_WRITE) {
            handleConnect(controller);
            ConnectAwaitable* awaitable = static_cast<ConnectAwaitable*>(controller->m_awaitable);
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case RECV:
    {
        // Recv 需要 EVFILT_READ 事件
        if (ev.filter == EVFILT_READ) {
            handleRecv(controller);
            RecvAwaitable* awaitable = static_cast<RecvAwaitable*>(controller->m_awaitable);
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case SEND:
    {
        // Send 需要 EVFILT_WRITE 事件
        if (ev.filter == EVFILT_WRITE) {
            handleSend(controller);
            SendAwaitable* awaitable = static_cast<SendAwaitable*>(controller->m_awaitable);
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case FILEREAD:
    {
        if (ev.filter == EVFILT_READ) {
            handleFileRead(controller);
            FileReadAwaitable* awaitable = static_cast<FileReadAwaitable*>(controller->m_awaitable);
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case FILEWRITE:
    {
        if (ev.filter == EVFILT_WRITE) {
            handleFileWrite(controller);
            FileWriteAwaitable* awaitable = static_cast<FileWriteAwaitable*>(controller->m_awaitable);
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    default:
        break;
    }
}

bool KqueueScheduler::handleAccept(IOController* event)
{
    AcceptAwaitable* awaitable = static_cast<AcceptAwaitable*>(event->m_awaitable);
    sockaddr_storage addr{};
    socklen_t addr_len = sizeof(addr);
    GHandle handle {
        .fd = accept(awaitable->m_listen_handle.fd, reinterpret_cast<sockaddr*>(&addr), &addr_len),
    };
    if( handle.fd < 0 ) {
        if( static_cast<uint32_t>(errno) == EAGAIN || static_cast<uint32_t>(errno) == EWOULDBLOCK || static_cast<uint32_t>(errno) == EINTR ) {
            return false;
        }
        awaitable->m_result = std::unexpected(IOError(kAcceptFailed, static_cast<uint32_t>(errno)));
        return true;
    }
    // 使用 Host::fromSockAddr 自动识别 IPv4 或 IPv6
    Host host = Host::fromSockAddr(addr);
    awaitable->m_result = handle;
    *(awaitable->m_host) = host;
    return true;
}

bool KqueueScheduler::handleRecv(IOController* event)
{
    RecvAwaitable* awaitable = static_cast<RecvAwaitable*>(event->m_awaitable);
    Bytes bytes;
    int recvBytes = recv(awaitable->m_handle.fd, awaitable->m_buffer, awaitable->m_length, 0);
    if (recvBytes > 0) {
        bytes = Bytes::fromCString(awaitable->m_buffer, recvBytes, recvBytes);
        awaitable->m_result = std::move(bytes);
    } else if (recvBytes == 0) {
        awaitable->m_result = std::unexpected(IOError(kDisconnectError, static_cast<uint32_t>(errno)));
    } else {
        if(static_cast<uint32_t>(errno) == EAGAIN || static_cast<uint32_t>(errno) == EWOULDBLOCK || static_cast<uint32_t>(errno) == EINTR )
        {
            return false;
        }
        awaitable->m_result = std::unexpected(IOError(kRecvFailed, static_cast<uint32_t>(errno)));
    }
    return true;
}

bool KqueueScheduler::handleSend(IOController* controller)
{
    SendAwaitable* awaitable = static_cast<SendAwaitable*>(controller->m_awaitable);
    ssize_t sentBytes = send(awaitable->m_handle.fd, awaitable->m_buffer, awaitable->m_length, 0);

    if (sentBytes >= 0) {
        awaitable->m_result = static_cast<size_t>(sentBytes);
        return true;
    } else {
        if (static_cast<uint32_t>(errno) == EAGAIN || static_cast<uint32_t>(errno) == EWOULDBLOCK || static_cast<uint32_t>(errno) == EINTR) {
            return false;  // 需要重试
        }
        awaitable->m_result = std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(errno)));
        return true;
    }
}

bool KqueueScheduler::handleConnect(IOController* controller)
{
    ConnectAwaitable* awaitable = static_cast<ConnectAwaitable*>(controller->m_awaitable);

    const Host& host = awaitable->m_host;
    int result = ::connect(awaitable->m_handle.fd, host.sockAddr(), host.addrLen());

    if (result == 0) {
        awaitable->m_result = {};
        return true;
    } else if (errno == EINPROGRESS) {
        return false;
    } else if (errno == EISCONN) {
        awaitable->m_result = {};
        return true;
    } else {
        // 连接失败
        awaitable->m_result = std::unexpected(IOError(kConnectFailed, static_cast<uint32_t>(errno)));
        return true;
    }
}

bool KqueueScheduler::handleFileRead(IOController* controller)
{
    FileReadAwaitable* awaitable = static_cast<FileReadAwaitable*>(controller->m_awaitable);
    ssize_t readBytes = pread(awaitable->m_handle.fd, awaitable->m_buffer, awaitable->m_length, awaitable->m_offset);

    if (readBytes > 0) {
        Bytes bytes = Bytes::fromCString(awaitable->m_buffer, readBytes, readBytes);
        awaitable->m_result = std::move(bytes);
        return true;
    } else if (readBytes == 0) {
        // EOF
        awaitable->m_result = Bytes();
        return true;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return false;
        }
        awaitable->m_result = std::unexpected(IOError(kReadFailed, static_cast<uint32_t>(errno)));
        return true;
    }
}

bool KqueueScheduler::handleFileWrite(IOController* controller)
{
    FileWriteAwaitable* awaitable = static_cast<FileWriteAwaitable*>(controller->m_awaitable);
    ssize_t writtenBytes = pwrite(awaitable->m_handle.fd, awaitable->m_buffer, awaitable->m_length, awaitable->m_offset);

    if (writtenBytes >= 0) {
        awaitable->m_result = static_cast<size_t>(writtenBytes);
        return true;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return false;
        }
        awaitable->m_result = std::unexpected(IOError(kWriteFailed, static_cast<uint32_t>(errno)));
        return true;
    }
}

}

#endif // USE_KQUEUE
