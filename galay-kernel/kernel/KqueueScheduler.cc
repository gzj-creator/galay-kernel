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
    close(contoller->m_handle.fd);
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

int KqueueScheduler::remove(IOController* controller)
{
    struct kevent evs[2];
    EV_SET(&evs[0], controller->m_handle.fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&evs[1], controller->m_handle.fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    return kevent(m_kqueue_fd, evs, 2, nullptr, 0, nullptr);
}

void KqueueScheduler::spawn(Coroutine coro)
{
    // 如果协程未绑定 scheduler，绑定到当前 scheduler
    if (!coro.belongScheduler()) {
        coro.belongScheduler(this);
        coro.threadId(m_threadId);
    }
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

    switch (controller->m_type)
    {
    case ACCEPT:
    {
        // Accept 需要 EVFILT_READ 事件
        if (ev.filter == EVFILT_READ) {
            handleAccept(controller);
            AcceptAwaitable* awaitable = controller->getAwaitable<AcceptAwaitable>();
            if(awaitable == nullptr) return;
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case CONNECT:
    {
        // Connect 需要 EVFILT_WRITE 事件
        if (ev.filter == EVFILT_WRITE) {
            handleConnect(controller);
            ConnectAwaitable* awaitable = controller->getAwaitable<ConnectAwaitable>();
            if(awaitable == nullptr) return;
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case RECV:
    {
        // Recv 需要 EVFILT_READ 事件
        if (ev.filter == EVFILT_READ) {
            handleRecv(controller);
            RecvAwaitable* awaitable = controller->getAwaitable<RecvAwaitable>();
            if(awaitable == nullptr) return;
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case SEND:
    {
        // Send 需要 EVFILT_WRITE 事件
        if (ev.filter == EVFILT_WRITE) {
            handleSend(controller);
            SendAwaitable* awaitable = controller->getAwaitable<SendAwaitable>();
            if(awaitable == nullptr) return;
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case READV:
    {
        // Readv 需要 EVFILT_READ 事件
        if (ev.filter == EVFILT_READ) {
            handleReadv(controller);
            ReadvAwaitable* awaitable = controller->getAwaitable<ReadvAwaitable>();
            if(awaitable == nullptr) return;
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case WRITEV:
    {
        // Writev 需要 EVFILT_WRITE 事件
        if (ev.filter == EVFILT_WRITE) {
            handleWritev(controller);
            WritevAwaitable* awaitable = controller->getAwaitable<WritevAwaitable>();
            if(awaitable == nullptr) return;
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case FILEREAD:
    {
        if (ev.filter == EVFILT_READ) {
            handleFileRead(controller);
            FileReadAwaitable* awaitable = controller->getAwaitable<FileReadAwaitable>();
            if(awaitable == nullptr) return;
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case FILEWRITE:
    {
        if (ev.filter == EVFILT_WRITE) {
            handleFileWrite(controller);
            FileWriteAwaitable* awaitable = controller->getAwaitable<FileWriteAwaitable>();
            if(awaitable == nullptr) return;
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case RECVFROM:
    {
        if (ev.filter == EVFILT_READ) {
            handleRecvFrom(controller);
            RecvFromAwaitable* awaitable = controller->getAwaitable<RecvFromAwaitable>();
            if(awaitable == nullptr) return;
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case SENDTO:
    {
        if (ev.filter == EVFILT_WRITE) {
            handleSendTo(controller);
            SendToAwaitable* awaitable = controller->getAwaitable<SendToAwaitable>();
            if(awaitable == nullptr) return;
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case FILEWATCH:
    {
        if (ev.filter == EVFILT_VNODE) {
            FileWatchAwaitable* awaitable = controller->getAwaitable<FileWatchAwaitable>();
            if(awaitable == nullptr) return;
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
    case RECV_NOTIFY:
    {
        // 仅通知可读，不执行IO操作
        if (ev.filter == EVFILT_READ) {
            RecvNotifyAwaitable* awaitable = controller->getAwaitable<RecvNotifyAwaitable>();
            if(awaitable == nullptr) return;
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case SEND_NOTIFY:
    {
        // 仅通知可写，不执行IO操作
        if (ev.filter == EVFILT_WRITE) {
            SendNotifyAwaitable* awaitable = controller->getAwaitable<SendNotifyAwaitable>();
            if(awaitable == nullptr) return;
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    default:
        break;
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
        awaitable->m_result = std::unexpected(IOError(kAcceptFailed, static_cast<uint32_t>(errno)));
        return true;
    }
    // 使用 Host::fromSockAddr 自动识别 IPv4 或 IPv6
    Host host = Host::fromSockAddr(addr);
    awaitable->m_result = handle;
    *(awaitable->m_host) = host;
    return true;
}

bool KqueueScheduler::handleRecv(IOController* controller)
{
    RecvAwaitable* awaitable = controller->getAwaitable<RecvAwaitable>();
    if(awaitable == nullptr) return false;
    Bytes bytes;
    int recvBytes = recv(controller->m_handle.fd, awaitable->m_buffer, awaitable->m_length, 0);
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
    SendAwaitable* awaitable = controller->getAwaitable<SendAwaitable>();
    if(awaitable == nullptr) return false;
    ssize_t sentBytes = send(controller->m_handle.fd, awaitable->m_buffer, awaitable->m_length, 0);

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

bool KqueueScheduler::handleReadv(IOController* controller)
{
    ReadvAwaitable* awaitable = controller->getAwaitable<ReadvAwaitable>();
    if(awaitable == nullptr) return false;

    ssize_t readBytes = readv(controller->m_handle.fd, awaitable->m_iovecs.data(),
                              static_cast<int>(awaitable->m_iovecs.size()));

    if (readBytes > 0) {
        awaitable->m_result = static_cast<size_t>(readBytes);
        return true;
    } else if (readBytes == 0) {
        awaitable->m_result = std::unexpected(IOError(kDisconnectError, 0));
        return true;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return false;
        }
        awaitable->m_result = std::unexpected(IOError(kRecvFailed, static_cast<uint32_t>(errno)));
        return true;
    }
}

bool KqueueScheduler::handleWritev(IOController* controller)
{
    WritevAwaitable* awaitable = controller->getAwaitable<WritevAwaitable>();
    if(awaitable == nullptr) return false;

    ssize_t writtenBytes = writev(controller->m_handle.fd, awaitable->m_iovecs.data(),
                                  static_cast<int>(awaitable->m_iovecs.size()));

    if (writtenBytes >= 0) {
        awaitable->m_result = static_cast<size_t>(writtenBytes);
        return true;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return false;
        }
        awaitable->m_result = std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(errno)));
        return true;
    }
}

bool KqueueScheduler::handleConnect(IOController* controller)
{
    ConnectAwaitable* awaitable = controller->getAwaitable<ConnectAwaitable>();
    if(awaitable == nullptr) return false;

    const Host& host = awaitable->m_host;
    int result = ::connect(controller->m_handle.fd, host.sockAddr(), host.addrLen());

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
    FileReadAwaitable* awaitable = controller->getAwaitable<FileReadAwaitable>();
    if(awaitable == nullptr) return false;
    ssize_t readBytes = pread(controller->m_handle.fd, awaitable->m_buffer, awaitable->m_length, awaitable->m_offset);

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
    FileWriteAwaitable* awaitable = controller->getAwaitable<FileWriteAwaitable>();
    if(awaitable == nullptr) return false;
    ssize_t writtenBytes = pwrite(controller->m_handle.fd, awaitable->m_buffer, awaitable->m_length, awaitable->m_offset);

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
    SendToAwaitable* awaitable = controller->getAwaitable<SendToAwaitable>();
    if(awaitable == nullptr) return false;
    const Host& to = awaitable->m_to;

    ssize_t sentBytes = sendto(controller->m_handle.fd, awaitable->m_buffer, awaitable->m_length,
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
