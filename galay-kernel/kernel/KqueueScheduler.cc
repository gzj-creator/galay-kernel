#include "KqueueScheduler.h"
#include "Scheduler.h"
#include "Timeout.h"
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
    // 循环处理直到队列为空，因为resume可能会spawn新协程
    while (true) {
        size_t count = m_coro_queue.try_dequeue_bulk(m_coro_buffer.data(), m_batch_size);
        if (count == 0) {
            break;
        }
        for (size_t i = 0; i < count; ++i) {
           Scheduler::resume( m_coro_buffer[i]);
        }
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

            // 检查是否是定时器事件（最低位为1）
            uintptr_t ptr_val = reinterpret_cast<uintptr_t>(ev.udata);
            if (ptr_val & 1) {
                // 定时器事件
                TimerController* timer_ctrl = reinterpret_cast<TimerController*>(ptr_val & ~1UL);
                if (timer_ctrl && !timer_ctrl->m_cancelled) {
                    // 尝试标记为超时
                    if (timer_ctrl->m_io_controller &&
                        timer_ctrl->m_generation == timer_ctrl->m_io_controller->m_generation &&
                        timer_ctrl->m_io_controller->tryTimeout()) {
                        // 成功标记超时，唤醒协程
                        if (timer_ctrl->m_waker) {
                            timer_ctrl->m_waker->wakeUp();
                        }
                    }
                }
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

    // 关键：尝试标记为完成状态，如果已被超时处理则跳过
    if (!controller->tryComplete()) {
        // 已被超时处理，忽略此 IO 事件
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
    case RECVFROM:
    {
        if (ev.filter == EVFILT_READ) {
            handleRecvFrom(controller);
            RecvFromAwaitable* awaitable = static_cast<RecvFromAwaitable*>(controller->m_awaitable);
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case SENDTO:
    {
        if (ev.filter == EVFILT_WRITE) {
            handleSendTo(controller);
            SendToAwaitable* awaitable = static_cast<SendToAwaitable*>(controller->m_awaitable);
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case FILEWATCH:
    {
        if (ev.filter == EVFILT_VNODE) {
            FileWatchAwaitable* awaitable = static_cast<FileWatchAwaitable*>(controller->m_awaitable);
            FileWatchResult result;
            result.isDir = false;  // kqueue 不直接提供此信息

            // 转换 kqueue fflags 到 FileWatchEvent
            uint32_t mask = 0;
            if (ev.fflags & NOTE_WRITE)   mask |= static_cast<uint32_t>(FileWatchEvent::Modify);
            if (ev.fflags & NOTE_DELETE)  mask |= static_cast<uint32_t>(FileWatchEvent::DeleteSelf);
            if (ev.fflags & NOTE_RENAME)  mask |= static_cast<uint32_t>(FileWatchEvent::MoveSelf);
            if (ev.fflags & NOTE_ATTRIB)  mask |= static_cast<uint32_t>(FileWatchEvent::Attrib);
            if (ev.fflags & NOTE_EXTEND)  mask |= static_cast<uint32_t>(FileWatchEvent::Modify);
            result.event = static_cast<FileWatchEvent>(mask);

            awaitable->m_result = std::move(result);
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

int KqueueScheduler::addRecvFrom(IOController* controller)
{
    if (handleRecvFrom(controller)) {
        return OK;
    }

    RecvFromAwaitable* awaitable = static_cast<RecvFromAwaitable*>(controller->m_awaitable);
    struct kevent ev;
    EV_SET(&ev, awaitable->m_handle.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addSendTo(IOController* controller)
{
    if (handleSendTo(controller)) {
        return OK;
    }

    SendToAwaitable* awaitable = static_cast<SendToAwaitable*>(controller->m_awaitable);
    struct kevent ev;
    EV_SET(&ev, awaitable->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

bool KqueueScheduler::handleRecvFrom(IOController* controller)
{
    RecvFromAwaitable* awaitable = static_cast<RecvFromAwaitable*>(controller->m_awaitable);
    sockaddr_storage addr{};
    socklen_t addr_len = sizeof(addr);

    ssize_t recvBytes = recvfrom(awaitable->m_handle.fd, awaitable->m_buffer, awaitable->m_length,
                                  0, reinterpret_cast<sockaddr*>(&addr), &addr_len);

    if (recvBytes > 0) {
        Bytes bytes = Bytes::fromCString(awaitable->m_buffer, recvBytes, recvBytes);
        awaitable->m_result = std::move(bytes);
        if (awaitable->m_from) {
            *(awaitable->m_from) = Host::fromSockAddr(addr);
        }
        return true;
    } else if (recvBytes == 0) {
        // UDP socket不会返回0，这里保持一致性
        awaitable->m_result = std::unexpected(IOError(kRecvFailed, 0));
        return true;
    } else {
        if (static_cast<uint32_t>(errno) == EAGAIN || static_cast<uint32_t>(errno) == EWOULDBLOCK || static_cast<uint32_t>(errno) == EINTR) {
            return false;
        }
        awaitable->m_result = std::unexpected(IOError(kRecvFailed, static_cast<uint32_t>(errno)));
        return true;
    }
}

bool KqueueScheduler::handleSendTo(IOController* controller)
{
    SendToAwaitable* awaitable = static_cast<SendToAwaitable*>(controller->m_awaitable);
    const Host& to = awaitable->m_to;

    ssize_t sentBytes = sendto(awaitable->m_handle.fd, awaitable->m_buffer, awaitable->m_length,
                                0, to.sockAddr(), to.addrLen());

    if (sentBytes >= 0) {
        awaitable->m_result = static_cast<size_t>(sentBytes);
        return true;
    } else {
        if (static_cast<uint32_t>(errno) == EAGAIN || static_cast<uint32_t>(errno) == EWOULDBLOCK || static_cast<uint32_t>(errno) == EINTR) {
            return false;
        }
        awaitable->m_result = std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(errno)));
        return true;
    }
}

int KqueueScheduler::addFileWatch(IOController* controller)
{
    FileWatchAwaitable* awaitable = static_cast<FileWatchAwaitable*>(controller->m_awaitable);

    // kqueue 使用 EVFILT_VNODE 监控文件变化
    // 需要打开的文件描述符，而不是 inotify fd
    struct kevent ev;
    // NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB | NOTE_EXTEND
    unsigned int fflags = NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB | NOTE_EXTEND;
    EV_SET(&ev, awaitable->m_inotify_fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, fflags, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addTimer(int timeout_ms, TimerController* timer_ctrl)
{
    // macOS/BSD 使用 EVFILT_TIMER，不需要 timerfd
    // timeout_ms 参数在 kqueue 中表示超时时间（毫秒）
    struct kevent ev;
    // 使用 timer_ctrl 指针的最低位设为1来标记这是定时器事件
    void* udata = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(timer_ctrl) | 1);
    // 使用 timer_ctrl 的地址作为唯一标识符
    uintptr_t ident = reinterpret_cast<uintptr_t>(timer_ctrl);
    // 存储 ident 到 IOController 以便后续取消
    if (timer_ctrl->m_io_controller) {
        timer_ctrl->m_io_controller->m_timer_ident = ident;
    }
    EV_SET(&ev, ident, EVFILT_TIMER, EV_ADD | EV_ONESHOT, NOTE_USECONDS, timeout_ms * 1000, udata);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

}

#endif // USE_KQUEUE
