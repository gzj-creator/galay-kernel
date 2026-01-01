#include "EpollScheduler.h"

#ifdef USE_EPOLL

#include "Awaitable.h"
#include "galay-kernel/async/AioFile.h"
#include "galay-kernel/common/Log.h"
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <stdexcept>

namespace galay::kernel
{

EpollScheduler::EpollScheduler(int max_events, int batch_size, int check_interval_ms)
    : m_epoll_fd(epoll_create1(EPOLL_CLOEXEC))
    , m_running(false)
    , m_max_events(max_events)
    , m_batch_size(batch_size)
    , m_check_interval_ms(check_interval_ms)
    , m_event_fd(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC))
{
    if (m_epoll_fd == -1) {
        throw std::runtime_error("Failed to create epoll");
    }

    if (m_event_fd == -1) {
        close(m_epoll_fd);
        throw std::runtime_error("Failed to create eventfd");
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = nullptr;
    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_event_fd, &ev) == -1) {
        close(m_epoll_fd);
        close(m_event_fd);
        throw std::runtime_error("Failed to add eventfd to epoll");
    }

    m_events.resize(m_max_events);
    m_coro_buffer.resize(m_batch_size);
}

EpollScheduler::~EpollScheduler()
{
    stop();
    if (m_epoll_fd != -1) {
        close(m_epoll_fd);
    }
    if (m_event_fd != -1) {
        close(m_event_fd);
    }
}

void EpollScheduler::start()
{
    if (m_running.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    m_thread = std::thread([this]() {
        eventLoop();
    });
}

void EpollScheduler::stop()
{
    if (!m_running.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    notify();

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void EpollScheduler::notify()
{
    uint64_t val = 1;
    write(m_event_fd, &val, sizeof(val));
}

int EpollScheduler::addAccept(IOController* event)
{
    if (handleAccept(event)) {
        return OK;
    }
    AcceptAwaitable* awaitable = static_cast<AcceptAwaitable*>(event->m_awaitable);
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = event;
    return epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, awaitable->m_listen_handle.fd, &ev);
}

int EpollScheduler::addConnect(IOController* controller)
{
    if (handleConnect(controller)) {
        return OK;
    }

    ConnectAwaitable* awaitable = static_cast<ConnectAwaitable*>(controller->m_awaitable);
    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLET;
    ev.data.ptr = controller;
    return epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, awaitable->m_handle.fd, &ev);
}

int EpollScheduler::addRecv(IOController* controller)
{
    if (handleRecv(controller)) {
        return OK;
    }

    RecvAwaitable* awaitable = static_cast<RecvAwaitable*>(controller->m_awaitable);
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = controller;

    int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, awaitable->m_handle.fd, &ev);
    if (ret == -1 && errno == ENOENT) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, awaitable->m_handle.fd, &ev);
    }
    return ret;
}

int EpollScheduler::addSend(IOController* controller)
{
    if (handleSend(controller)) {
        return OK;
    }

    SendAwaitable* awaitable = static_cast<SendAwaitable*>(controller->m_awaitable);
    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLET;
    ev.data.ptr = controller;

    int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, awaitable->m_handle.fd, &ev);
    if (ret == -1 && errno == ENOENT) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, awaitable->m_handle.fd, &ev);
    }
    return ret;
}

int EpollScheduler::addClose(int fd)
{
    close(fd);
    remove(fd);
    return 0;
}

int EpollScheduler::addFileRead(IOController* controller)
{
    // AioCommitAwaitable 已经在 await_suspend 中提交了 AIO
    // 这里只需要注册 eventfd 到 epoll 等待完成通知
    galay::async::AioCommitAwaitable* awaitable =
        static_cast<galay::async::AioCommitAwaitable*>(controller->m_awaitable);

    LogDebug("[EpollScheduler::addFileRead] controller={}, event_fd={}", (void*)controller, awaitable->m_event_fd);

    // 将 eventfd 注册到 epoll
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = controller;

    int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, awaitable->m_event_fd, &ev);
    LogDebug("[EpollScheduler::addFileRead] epoll_ctl ADD returned: {}, errno={}", ret, errno);
    if (ret == -1 && errno == EEXIST) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, awaitable->m_event_fd, &ev);
        LogDebug("[EpollScheduler::addFileRead] epoll_ctl MOD returned: {}", ret);
    }
    return ret < 0 ? ret : OK;
}

int EpollScheduler::addFileWrite(IOController* controller)
{
    // 与 addFileRead 相同，只注册 eventfd
    galay::async::AioCommitAwaitable* awaitable =
        static_cast<galay::async::AioCommitAwaitable*>(controller->m_awaitable);

    LogDebug("[EpollScheduler::addFileWrite] controller={}, event_fd={}", (void*)controller, awaitable->m_event_fd);

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;  // eventfd 是读事件
    ev.data.ptr = controller;

    int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, awaitable->m_event_fd, &ev);
    if (ret == -1 && errno == EEXIST) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, awaitable->m_event_fd, &ev);
    }
    return ret < 0 ? ret : OK;
}

int EpollScheduler::remove(int fd)
{
    return epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
}

void EpollScheduler::spawn(Coroutine coro)
{
    m_coro_queue.enqueue(std::move(coro));
}

void EpollScheduler::processPendingCoroutines()
{
    size_t count = m_coro_queue.try_dequeue_bulk(m_coro_buffer.data(), m_batch_size);
    for (size_t i = 0; i < count; ++i) {
        Scheduler::resume(m_coro_buffer[i]);
    }
}

void EpollScheduler::eventLoop()
{
    while (m_running.load(std::memory_order_acquire)) {
        processPendingCoroutines();

        int nev = epoll_wait(m_epoll_fd, m_events.data(), m_max_events, m_check_interval_ms);

        if (nev < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        for (int i = 0; i < nev; ++i) {
            struct epoll_event& ev = m_events[i];

            if (ev.data.ptr == nullptr) {
                uint64_t val;
                while (read(m_event_fd, &val, sizeof(val)) > 0);
                continue;
            }

            if (ev.events & EPOLLERR) {
                continue;
            }

            processEvent(ev);
        }
    }
}

void EpollScheduler::processEvent(struct epoll_event& ev)
{
    IOController* controller = static_cast<IOController*>(ev.data.ptr);
    if (!controller || controller->m_type == IOEventType::INVALID || controller->m_awaitable == nullptr) {
        return;
    }

    switch (controller->m_type)
    {
    case ACCEPT:
    {
        if (ev.events & EPOLLIN) {
            handleAccept(controller);
            AcceptAwaitable* awaitable = static_cast<AcceptAwaitable*>(controller->m_awaitable);
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case CONNECT:
    {
        if (ev.events & EPOLLOUT) {
            handleConnect(controller);
            ConnectAwaitable* awaitable = static_cast<ConnectAwaitable*>(controller->m_awaitable);
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case RECV:
    {
        if (ev.events & EPOLLIN) {
            handleRecv(controller);
            RecvAwaitable* awaitable = static_cast<RecvAwaitable*>(controller->m_awaitable);
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case SEND:
    {
        if (ev.events & EPOLLOUT) {
            handleSend(controller);
            SendAwaitable* awaitable = static_cast<SendAwaitable*>(controller->m_awaitable);
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case FILEREAD:
    {
        if (ev.events & EPOLLIN) {
            handleFileRead(controller);
        }
        break;
    }
    case FILEWRITE:
    {
        if (ev.events & EPOLLIN) {
            handleFileWrite(controller);
        }
        break;
    }
    case RECVFROM:
    {
        if (ev.events & EPOLLIN) {
            handleRecvFrom(controller);
            RecvFromAwaitable* awaitable = static_cast<RecvFromAwaitable*>(controller->m_awaitable);
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    case SENDTO:
    {
        if (ev.events & EPOLLOUT) {
            handleSendTo(controller);
            SendToAwaitable* awaitable = static_cast<SendToAwaitable*>(controller->m_awaitable);
            awaitable->m_waker.wakeUp();
        }
        break;
    }
    default:
        break;
    }
}

bool EpollScheduler::handleAccept(IOController* event)
{
    AcceptAwaitable* awaitable = static_cast<AcceptAwaitable*>(event->m_awaitable);
    sockaddr_storage addr{};
    socklen_t addr_len = sizeof(addr);
    GHandle handle {
        .fd = accept(awaitable->m_listen_handle.fd, reinterpret_cast<sockaddr*>(&addr), &addr_len),
    };
    if (handle.fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return false;
        }
        awaitable->m_result = std::unexpected(IOError(kAcceptFailed, static_cast<uint32_t>(errno)));
        return true;
    }
    Host host = Host::fromSockAddr(addr);
    awaitable->m_result = handle;
    *(awaitable->m_host) = host;
    return true;
}

bool EpollScheduler::handleRecv(IOController* event)
{
    RecvAwaitable* awaitable = static_cast<RecvAwaitable*>(event->m_awaitable);
    Bytes bytes;
    int recvBytes = recv(awaitable->m_handle.fd, awaitable->m_buffer, awaitable->m_length, 0);
    if (recvBytes > 0) {
        bytes = Bytes::fromCString(awaitable->m_buffer, recvBytes, recvBytes);
        awaitable->m_result = std::move(bytes);
    } else if (recvBytes == 0) {
        awaitable->m_result = std::unexpected(IOError(kDisconnectError, 0));
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return false;
        }
        awaitable->m_result = std::unexpected(IOError(kRecvFailed, static_cast<uint32_t>(errno)));
    }
    return true;
}

bool EpollScheduler::handleSend(IOController* controller)
{
    SendAwaitable* awaitable = static_cast<SendAwaitable*>(controller->m_awaitable);
    ssize_t sentBytes = send(awaitable->m_handle.fd, awaitable->m_buffer, awaitable->m_length, 0);

    if (sentBytes >= 0) {
        awaitable->m_result = static_cast<size_t>(sentBytes);
        return true;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return false;
        }
        awaitable->m_result = std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(errno)));
        return true;
    }
}

bool EpollScheduler::handleConnect(IOController* controller)
{
    ConnectAwaitable* awaitable = static_cast<ConnectAwaitable*>(controller->m_awaitable);

    const Host& host = awaitable->m_host;
    int result = connect(awaitable->m_handle.fd, host.sockAddr(), host.addrLen());

    if (result == 0) {
        awaitable->m_result = {};
        return true;
    } else if (errno == EINPROGRESS) {
        return false;
    } else if (errno == EISCONN) {
        awaitable->m_result = {};
        return true;
    } else {
        awaitable->m_result = std::unexpected(IOError(kConnectFailed, static_cast<uint32_t>(errno)));
        return true;
    }
}

void EpollScheduler::handleFileRead(IOController* controller)
{
    LogDebug("[EpollScheduler::handleFileRead] controller={}, type={}", (void*)controller, (int)controller->m_type);

    // 直接转换为 AioCommitAwaitable 指针
    galay::async::AioCommitAwaitable* awaitable =
        static_cast<galay::async::AioCommitAwaitable*>(controller->m_awaitable);

    // 读取 eventfd 获取完成的事件数量
    uint64_t finish_count = 0;
    int ret = read(awaitable->m_event_fd, &finish_count, sizeof(finish_count));
    LogDebug("[EpollScheduler::handleFileRead] read eventfd returned: {}, finish_count={}", ret, finish_count);
    if (ret < 0) {
        LogError("[EpollScheduler::handleFileRead] read eventfd failed: {}", strerror(errno));
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return;  // 尚未就绪，等待下次通知
        }
        awaitable->m_result = std::unexpected(IOError(kReadFailed, static_cast<uint32_t>(errno)));
        epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, awaitable->m_event_fd, nullptr);
        awaitable->m_waker.wakeUp();
        return;
    }

    // 批量收割 AIO 事件
    std::vector<struct io_event> aio_events(finish_count);
    int num = io_getevents(awaitable->m_aio_ctx, finish_count, finish_count, aio_events.data(), nullptr);
    LogDebug("[EpollScheduler::handleFileRead] io_getevents returned: {}", num);

    if (num > 0) {
        // 收集所有结果
        for (int i = 0; i < num; ++i) {
            awaitable->m_results.push_back(aio_events[i].res);
            LogDebug("[EpollScheduler::handleFileRead] aio result[{}]: {}", i, aio_events[i].res);
        }
    }

    // 检查是否所有事件都完成了
    if (awaitable->m_results.size() >= awaitable->m_pending_count) {
        epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, awaitable->m_event_fd, nullptr);
        awaitable->m_result = std::move(awaitable->m_results);
        LogDebug("[EpollScheduler::handleFileRead] all {} events completed, waking up coroutine", awaitable->m_pending_count);
        awaitable->m_waker.wakeUp();
    }
}

void EpollScheduler::handleFileWrite(IOController* controller)
{
    // 与 handleFileRead 相同的处理逻辑
    handleFileRead(controller);
}

int EpollScheduler::addRecvFrom(IOController* controller)
{
    if (handleRecvFrom(controller)) {
        return OK;
    }

    RecvFromAwaitable* awaitable = static_cast<RecvFromAwaitable*>(controller->m_awaitable);
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = controller;

    int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, awaitable->m_handle.fd, &ev);
    if (ret == -1 && errno == ENOENT) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, awaitable->m_handle.fd, &ev);
    }
    return ret;
}

int EpollScheduler::addSendTo(IOController* controller)
{
    if (handleSendTo(controller)) {
        return OK;
    }

    SendToAwaitable* awaitable = static_cast<SendToAwaitable*>(controller->m_awaitable);
    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLET;
    ev.data.ptr = controller;

    int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, awaitable->m_handle.fd, &ev);
    if (ret == -1 && errno == ENOENT) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, awaitable->m_handle.fd, &ev);
    }
    return ret;
}

bool EpollScheduler::handleRecvFrom(IOController* controller)
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
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return false;
        }
        awaitable->m_result = std::unexpected(IOError(kRecvFailed, static_cast<uint32_t>(errno)));
        return true;
    }
}

bool EpollScheduler::handleSendTo(IOController* controller)
{
    SendToAwaitable* awaitable = static_cast<SendToAwaitable*>(controller->m_awaitable);
    const Host& to = awaitable->m_to;

    ssize_t sentBytes = sendto(awaitable->m_handle.fd, awaitable->m_buffer, awaitable->m_length,
                                0, to.sockAddr(), to.addrLen());

    if (sentBytes >= 0) {
        awaitable->m_result = static_cast<size_t>(sentBytes);
        return true;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return false;
        }
        awaitable->m_result = std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(errno)));
        return true;
    }
}

}

#endif // USE_EPOLL
