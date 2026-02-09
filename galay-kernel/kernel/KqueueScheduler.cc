#include "KqueueScheduler.h"
#include "galay-kernel/common/Error.h"
#include <atomic>

#ifdef USE_KQUEUE

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
    EV_SET(&ev, m_notify_pipe[0], EVFILT_READ, EV_ADD, 0, 0, nullptr);
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
        m_threadId = std::this_thread::get_id();  // 设置调度器线程ID
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

int KqueueScheduler::addAccept(IOController* controller)
{
    if(handleAccept(controller)) {
        return OK;
    }
    auto awaitable = controller->getAwaitable<AcceptAwaitable>();
    if(awaitable == nullptr) return -1;
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addConnect(IOController* controller)
{
    // 先尝试立即连接
    if (handleConnect(controller)) {
        return OK;
    }

    // 连接正在进行中，注册到 kqueue
    auto awaitable = controller->getAwaitable<ConnectAwaitable>();
    if(awaitable == nullptr) return -1;
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addRecv(IOController* controller)
{
    // 先尝试立即接收
    if (handleRecv(controller)) {
        return OK;
    }

    // 需要等待数据，注册到 kqueue
    auto awaitable = controller->getAwaitable<RecvAwaitable>();
    if(awaitable == nullptr) return -1;
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addSend(IOController* controller)
{
    // 先尝试立即发送
    if (handleSend(controller)) {
        return OK;
    }

    // 需要等待可写，注册到 kqueue
    SendAwaitable* awaitable = controller->getAwaitable<SendAwaitable>();
    if(awaitable == nullptr) return -1;
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addReadv(IOController* controller)
{
    // 先尝试立即读取
    if (handleReadv(controller)) {
        return OK;
    }

    // 需要等待数据，注册到 kqueue
    auto awaitable = controller->getAwaitable<ReadvAwaitable>();
    if(awaitable == nullptr) return -1;
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addWritev(IOController* controller)
{
    // 先尝试立即写入
    if (handleWritev(controller)) {
        return OK;
    }

    // 需要等待可写，注册到 kqueue
    auto awaitable = controller->getAwaitable<WritevAwaitable>();
    if(awaitable == nullptr) return -1;
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addClose(IOController* contoller)
{
    if (contoller->m_handle == GHandle::invalid()) {
        return 0;
    }
    close(contoller->m_handle.fd);
    contoller->m_handle = GHandle::invalid();
    remove(contoller);
    return 0;
}

int KqueueScheduler::addFileRead(IOController* controller)
{
    if (handleFileRead(controller)) {
        return OK;
    }
    auto awaitable = controller->getAwaitable<FileReadAwaitable>();
    if(awaitable == nullptr) return -1;
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addFileWrite(IOController* controller)
{
    if (handleFileWrite(controller)) {
        return OK;
    }
    auto awaitable = controller->getAwaitable<FileWriteAwaitable>();
    if(awaitable == nullptr) return -1;
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addSendFile(IOController* controller)
{
    // 先尝试立即发送
    if (handleSendFile(controller)) {
        return OK;
    }

    // 需要等待可写，注册到 kqueue
    auto awaitable = controller->getAwaitable<SendFileAwaitable>();
    if(awaitable == nullptr) return -1;
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::remove(IOController* controller)
{
    struct kevent evs[2];
    EV_SET(&evs[0], controller->m_handle.fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&evs[1], controller->m_handle.fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    return kevent(m_kqueue_fd, evs, 2, nullptr, 0, nullptr);
}

bool KqueueScheduler::spawn(Coroutine co)
{
    auto* scheduler = co.belongScheduler();
    // 如果协程未绑定 scheduler，绑定到当前 scheduler
    if (!scheduler) {
        co.belongScheduler(this);
    } else {
        if(scheduler != this) return false;
    }
    m_coro_queue.enqueue(std::move(co));
    return true;
}

bool KqueueScheduler::spawnImmidiately(Coroutine co)
{
    auto* scheduler = co.belongScheduler();
    if (scheduler) {
        return false;
    } 
    co.belongScheduler(this);
    resume(co);
    return true;
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
    // kevent 超时时间公式：timeout = tickDuration / 2
    // 使用时间轮精度的一半作为超时，确保定时器最大误差不超过半个 tick
    // 例如：tickDuration = 50ms 时，timeout = 25ms，最大误差 ≤ 25ms
    uint64_t tick_duration_ns = m_timer_manager.during();
    uint64_t timeout_ns = tick_duration_ns / 2;
    struct timespec timeout;
    timeout.tv_sec = timeout_ns / 1000000000ULL;
    timeout.tv_nsec = timeout_ns % 1000000000ULL;

    while (m_running.load(std::memory_order_relaxed)) {
        // Process pending coroutines
        processPendingCoroutines();
        m_timer_manager.tick();
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
    if (!controller || controller->m_type == IOEventType::INVALID) {
        return;
    }

    // 检查错误标志
    if (ev.flags & EV_ERROR) {
        // 处理错误
        return;
    }

    uint32_t t = static_cast<uint32_t>(controller->m_type);

    // ===== 读方向事件 =====
    if (ev.filter == EVFILT_READ) {
        if (t & ACCEPT) {
            // ACCEPT 使用 EV_CLEAR 持久注册，EAGAIN 时无需重新注册，
            // 下次有新连接 kqueue 会再次触发。
            // 注意：epoll/io_uring 下 EAGAIN 需要重新注册事件。
            if (handleAccept(controller)) {
                AcceptAwaitable* awaitable = controller->getAwaitable<AcceptAwaitable>();
                if (awaitable) awaitable->m_waker.wakeUp();
            }
        }
        else if (t & RECV) {
            if (handleRecv(controller)) {
                RecvAwaitable* awaitable = controller->getAwaitable<RecvAwaitable>();
                if (awaitable) awaitable->m_waker.wakeUp();
            }
        }
        else if (t & READV) {
            if (handleReadv(controller)) {
                ReadvAwaitable* awaitable = controller->getAwaitable<ReadvAwaitable>();
                if (awaitable) awaitable->m_waker.wakeUp();
            }
        }
        else if (t & RECVFROM) {
            if (handleRecvFrom(controller)) {
                RecvFromAwaitable* awaitable = controller->getAwaitable<RecvFromAwaitable>();
                if (awaitable) awaitable->m_waker.wakeUp();
            }
        }
        else if (t & FILEREAD) {
            if (handleFileRead(controller)) {
                FileReadAwaitable* awaitable = controller->getAwaitable<FileReadAwaitable>();
                if (awaitable) awaitable->m_waker.wakeUp();
            }
        }
        else if (t & RECV_NOTIFY) {
            RecvNotifyAwaitable* awaitable = controller->getAwaitable<RecvNotifyAwaitable>();
            if (awaitable) awaitable->m_waker.wakeUp();
        }
    }
    // ===== 写方向事件 =====
    else if (ev.filter == EVFILT_WRITE) {
        if (t & CONNECT) {
            if (handleConnect(controller)) {
                ConnectAwaitable* awaitable = controller->getAwaitable<ConnectAwaitable>();
                if (awaitable) awaitable->m_waker.wakeUp();
            }
        }
        else if (t & SEND) {
            if (handleSend(controller)) {
                SendAwaitable* awaitable = controller->getAwaitable<SendAwaitable>();
                if (awaitable) awaitable->m_waker.wakeUp();
            }
        }
        else if (t & WRITEV) {
            if (handleWritev(controller)) {
                WritevAwaitable* awaitable = controller->getAwaitable<WritevAwaitable>();
                if (awaitable) awaitable->m_waker.wakeUp();
            }
        }
        else if (t & SENDTO) {
            if (handleSendTo(controller)) {
                SendToAwaitable* awaitable = controller->getAwaitable<SendToAwaitable>();
                if (awaitable) awaitable->m_waker.wakeUp();
            }
        }
        else if (t & FILEWRITE) {
            if (handleFileWrite(controller)) {
                FileWriteAwaitable* awaitable = controller->getAwaitable<FileWriteAwaitable>();
                if (awaitable) awaitable->m_waker.wakeUp();
            }
        }
        else if (t & SENDFILE) {
            if (handleSendFile(controller)) {
                SendFileAwaitable* awaitable = controller->getAwaitable<SendFileAwaitable>();
                if (awaitable) awaitable->m_waker.wakeUp();
            }
        }
        else if (t & SEND_NOTIFY) {
            SendNotifyAwaitable* awaitable = controller->getAwaitable<SendNotifyAwaitable>();
            if (awaitable) awaitable->m_waker.wakeUp();
        }
    }
    // ===== 文件监控事件 =====
    else if (ev.filter == EVFILT_VNODE) {
        if (t & FILEWATCH) {
            FileWatchAwaitable* awaitable = controller->getAwaitable<FileWatchAwaitable>();
            if (awaitable) {
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
        }
    }
}

bool KqueueScheduler::handleAccept(IOController* controller)
{
    AcceptAwaitable* awaitable = controller->getAwaitable<AcceptAwaitable>();
    if(awaitable == nullptr) return false;
    sockaddr_storage addr{};
    socklen_t addr_len = sizeof(addr);
    GHandle handle {
        .fd = accept(controller->m_handle.fd, reinterpret_cast<sockaddr*>(&addr), &addr_len),
    };
    if( handle.fd < 0 ) {
        if( static_cast<uint32_t>(errno) == EAGAIN || static_cast<uint32_t>(errno) == EWOULDBLOCK || static_cast<uint32_t>(errno) == EINTR ) {
            return false;
        }
        if(awaitable->handleComplete(std::unexpected(IOError(kAcceptFailed, static_cast<uint32_t>(errno))))) {
            return true;
        }
        return false;
    }
    // 使用 Host::fromSockAddr 自动识别 IPv4 或 IPv6
    Host host = Host::fromSockAddr(addr);
    awaitable->handleComplete(handle);
    *(awaitable->m_host) = host;
    return true;
}

bool KqueueScheduler::handleRecv(IOController* controller)
{
    RecvAwaitable* awaitable = controller->getAwaitable<RecvAwaitable>();
    if(awaitable == nullptr) return false;
    int recvBytes = recv(controller->m_handle.fd, awaitable->m_buffer, awaitable->m_length, 0);
    if (recvBytes > 0) {
        Bytes bytes = Bytes::fromCString(awaitable->m_buffer, recvBytes, recvBytes);
        return awaitable->handleComplete(std::move(bytes));
    } else if (recvBytes == 0) {
        return awaitable->handleComplete(std::unexpected(IOError(kDisconnectError, static_cast<uint32_t>(errno))));
    } else {
        if(static_cast<uint32_t>(errno) == EAGAIN || static_cast<uint32_t>(errno) == EWOULDBLOCK || static_cast<uint32_t>(errno) == EINTR )
        {
            return false;
        }
        return awaitable->handleComplete(std::unexpected(IOError(kRecvFailed, static_cast<uint32_t>(errno))));
    }
}

bool KqueueScheduler::handleSend(IOController* controller)
{
    SendAwaitable* awaitable = controller->getAwaitable<SendAwaitable>();
    if(awaitable == nullptr) return false;
    ssize_t sentBytes = send(controller->m_handle.fd, awaitable->m_buffer, awaitable->m_length, 0);

    if (sentBytes >= 0) {
        return awaitable->handleComplete(static_cast<size_t>(sentBytes));
    } else {
        if (static_cast<uint32_t>(errno) == EAGAIN || static_cast<uint32_t>(errno) == EWOULDBLOCK || static_cast<uint32_t>(errno) == EINTR) {
            return false;  // 需要重试
        }
        return awaitable->handleComplete(std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(errno))));
    }
}

bool KqueueScheduler::handleReadv(IOController* controller)
{
    ReadvAwaitable* awaitable = controller->getAwaitable<ReadvAwaitable>();
    if(awaitable == nullptr) return false;

    ssize_t readBytes = readv(controller->m_handle.fd, awaitable->m_iovecs.data(),
                              static_cast<int>(awaitable->m_iovecs.size()));

    if (readBytes > 0) {
        return awaitable->handleComplete(static_cast<size_t>(readBytes));
    } else if (readBytes == 0) {
        return awaitable->handleComplete(std::unexpected(IOError(kDisconnectError, 0)));
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return false;
        }
        return awaitable->handleComplete(std::unexpected(IOError(kRecvFailed, static_cast<uint32_t>(errno))));
    }
}

bool KqueueScheduler::handleWritev(IOController* controller)
{
    WritevAwaitable* awaitable = controller->getAwaitable<WritevAwaitable>();
    if(awaitable == nullptr) return false;

    ssize_t writtenBytes = writev(controller->m_handle.fd, awaitable->m_iovecs.data(),
                                  static_cast<int>(awaitable->m_iovecs.size()));

    if (writtenBytes >= 0) {
        return awaitable->handleComplete(static_cast<size_t>(writtenBytes));
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return false;
        }
        return awaitable->handleComplete(std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(errno))));
    }
}

bool KqueueScheduler::handleSendFile(IOController* controller)
{
    SendFileAwaitable* awaitable = controller->getAwaitable<SendFileAwaitable>();
    if(awaitable == nullptr) return false;

    // macOS sendfile 参数: sendfile(in_fd, out_fd, offset, len, hdtr, flags)
    off_t len = static_cast<off_t>(awaitable->m_count);
    int result = ::sendfile(awaitable->m_file_fd, controller->m_handle.fd,
                           awaitable->m_offset, &len, nullptr, 0);

    if (result == 0) {
        // 成功发送，len 包含实际发送的字节数
        return awaitable->handleComplete(static_cast<size_t>(len));
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            // 部分发送或需要重试
            if (len > 0) {
                // 部分发送成功
                return awaitable->handleComplete(static_cast<size_t>(len));
            }
            return false;
        }
        return awaitable->handleComplete(std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(errno))));
    }
}

bool KqueueScheduler::handleConnect(IOController* controller)
{
    ConnectAwaitable* awaitable = controller->getAwaitable<ConnectAwaitable>();
    if(awaitable == nullptr) return false;

    const Host& host = awaitable->m_host;
    int result = ::connect(controller->m_handle.fd, host.sockAddr(), host.addrLen());

    if (result == 0) {
        return awaitable->handleComplete({});
    } else if (errno == EINPROGRESS) {
        return false;
    } else if (errno == EISCONN) {
        return awaitable->handleComplete({});
    } else {
        // 连接失败
        return awaitable->handleComplete(std::unexpected(IOError(kConnectFailed, static_cast<uint32_t>(errno))));
    }
}

bool KqueueScheduler::handleFileRead(IOController* controller)
{
    FileReadAwaitable* awaitable = controller->getAwaitable<FileReadAwaitable>();
    if(awaitable == nullptr) return false;
    ssize_t readBytes = pread(controller->m_handle.fd, awaitable->m_buffer, awaitable->m_length, awaitable->m_offset);

    if (readBytes > 0) {
        Bytes bytes = Bytes::fromCString(awaitable->m_buffer, readBytes, readBytes);
        return awaitable->handleComplete(std::move(bytes));
    } else if (readBytes == 0) {
        // EOF
        return awaitable->handleComplete(Bytes());
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return false;
        }
        return awaitable->handleComplete(std::unexpected(IOError(kReadFailed, static_cast<uint32_t>(errno))));
    }
}

bool KqueueScheduler::handleFileWrite(IOController* controller)
{
    FileWriteAwaitable* awaitable = controller->getAwaitable<FileWriteAwaitable>();
    if(awaitable == nullptr) return false;
    ssize_t writtenBytes = pwrite(controller->m_handle.fd, awaitable->m_buffer, awaitable->m_length, awaitable->m_offset);

    if (writtenBytes >= 0) {
        return awaitable->handleComplete(static_cast<size_t>(writtenBytes));
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return false;
        }
        return awaitable->handleComplete(std::unexpected(IOError(kWriteFailed, static_cast<uint32_t>(errno))));
    }
}

int KqueueScheduler::addRecvFrom(IOController* controller)
{
    if (handleRecvFrom(controller)) {
        return OK;
    }

    RecvFromAwaitable* awaitable = controller->getAwaitable<RecvFromAwaitable>();
    if(awaitable == nullptr) return -1;
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addSendTo(IOController* controller)
{
    if (handleSendTo(controller)) {
        return OK;
    }

    SendToAwaitable* awaitable = controller->getAwaitable<SendToAwaitable>();
    if(awaitable == nullptr) return -1;
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

bool KqueueScheduler::handleRecvFrom(IOController* controller)
{
    RecvFromAwaitable* awaitable = controller->getAwaitable<RecvFromAwaitable>();
    if(awaitable == nullptr) return false;
    sockaddr_storage addr{};
    socklen_t addr_len = sizeof(addr);

    ssize_t recvBytes = recvfrom(controller->m_handle.fd, awaitable->m_buffer, awaitable->m_length,
                                  0, reinterpret_cast<sockaddr*>(&addr), &addr_len);

    if (recvBytes > 0) {
        Bytes bytes = Bytes::fromCString(awaitable->m_buffer, recvBytes, recvBytes);
        if (awaitable->m_from) {
            *(awaitable->m_from) = Host::fromSockAddr(addr);
        }
        return awaitable->handleComplete(std::move(bytes));
    } else if (recvBytes == 0) {
        // UDP socket不会返回0，这里保持一致性
        return awaitable->handleComplete(std::unexpected(IOError(kRecvFailed, 0)));
    } else {
        if (static_cast<uint32_t>(errno) == EAGAIN || static_cast<uint32_t>(errno) == EWOULDBLOCK || static_cast<uint32_t>(errno) == EINTR) {
            return false;
        }
        return awaitable->handleComplete(std::unexpected(IOError(kRecvFailed, static_cast<uint32_t>(errno))));
    }
}

bool KqueueScheduler::handleSendTo(IOController* controller)
{
    SendToAwaitable* awaitable = controller->getAwaitable<SendToAwaitable>();
    if(awaitable == nullptr) return false;
    const Host& to = awaitable->m_to;

    ssize_t sentBytes = sendto(controller->m_handle.fd, awaitable->m_buffer, awaitable->m_length,
                                0, to.sockAddr(), to.addrLen());

    if (sentBytes >= 0) {
        return awaitable->handleComplete(static_cast<size_t>(sentBytes));
    } else {
        if (static_cast<uint32_t>(errno) == EAGAIN || static_cast<uint32_t>(errno) == EWOULDBLOCK || static_cast<uint32_t>(errno) == EINTR) {
            return false;
        }
        return awaitable->handleComplete(std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(errno))));
    }
}

int KqueueScheduler::addFileWatch(IOController* controller)
{
    FileWatchAwaitable* awaitable = controller->getAwaitable<FileWatchAwaitable>();
    if(awaitable == nullptr) return -1;

    // 将 FileWatchEvent 转换为 kqueue fflags
    unsigned int fflags = 0;
    uint32_t events = static_cast<uint32_t>(awaitable->m_events);
    if (events & static_cast<uint32_t>(FileWatchEvent::Modify))     fflags |= NOTE_WRITE;
    if (events & static_cast<uint32_t>(FileWatchEvent::DeleteSelf)) fflags |= NOTE_DELETE;
    if (events & static_cast<uint32_t>(FileWatchEvent::MoveSelf))   fflags |= NOTE_RENAME;
    if (events & static_cast<uint32_t>(FileWatchEvent::Attrib))     fflags |= NOTE_ATTRIB;
    // NOTE_EXTEND 也映射到 Modify（文件扩展）
    if (events & static_cast<uint32_t>(FileWatchEvent::Modify))     fflags |= NOTE_EXTEND;

    // kqueue 使用 EVFILT_VNODE 监控文件变化
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, fflags, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addRecvNotify(IOController* controller)
{
    // 仅注册读事件，不执行IO操作
    // 用于SSL等需要自定义IO处理的场景
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addSendNotify(IOController* controller)
{
    // 仅注册写事件，不执行IO操作
    // 用于SSL等需要自定义IO处理的场景
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}


}

#endif // USE_KQUEUE
