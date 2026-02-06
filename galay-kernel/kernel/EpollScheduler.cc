#include "EpollScheduler.h"

#ifdef USE_EPOLL

#include "galay-kernel/common/Error.h"
#include "galay-kernel/async/AioFile.h"
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
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
        m_threadId = std::this_thread::get_id();
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
    if (write(m_event_fd, &val, sizeof(val)) < 0) {}
}

uint32_t EpollScheduler::buildEpollEvents(IOController* controller)
{
    uint32_t events = EPOLLET;
    uint32_t t = static_cast<uint32_t>(controller->m_type);
    if (t & (ACCEPT | RECV | READV | RECVFROM | FILEREAD | FILEWATCH | RECV_NOTIFY))
        events |= EPOLLIN;
    if (t & (CONNECT | SEND | WRITEV | SENDTO | SENDFILE | FILEWRITE | SEND_NOTIFY))
        events |= EPOLLOUT;
    return events;
}

int EpollScheduler::addAccept(IOController* controller)
{
    if (handleAccept(controller)) {
        return OK;
    }
    auto awaitable = controller->getAwaitable<AcceptAwaitable>();
    if (awaitable == nullptr) return -1;
    struct epoll_event ev;
    ev.events = buildEpollEvents(controller);
    ev.data.ptr = controller;
    return epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, controller->m_handle.fd, &ev);
}

int EpollScheduler::addConnect(IOController* controller)
{
    if (handleConnect(controller)) {
        return OK;
    }

    auto awaitable = controller->getAwaitable<ConnectAwaitable>();
    if (awaitable == nullptr) return -1;
    struct epoll_event ev;
    ev.events = buildEpollEvents(controller);
    ev.data.ptr = controller;
    return epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, controller->m_handle.fd, &ev);
}

int EpollScheduler::addRecv(IOController* controller)
{
    if (handleRecv(controller)) {
        return OK;
    }

    auto awaitable = controller->getAwaitable<RecvAwaitable>();
    if (awaitable == nullptr) return -1;
    struct epoll_event ev;
    ev.events = buildEpollEvents(controller);
    ev.data.ptr = controller;

    int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, controller->m_handle.fd, &ev);
    if (ret == -1 && errno == ENOENT) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, controller->m_handle.fd, &ev);
    }
    return ret;
}

int EpollScheduler::addSend(IOController* controller)
{
    if (handleSend(controller)) {
        return OK;
    }

    auto awaitable = controller->getAwaitable<SendAwaitable>();
    if (awaitable == nullptr) return -1;
    struct epoll_event ev;
    ev.events = buildEpollEvents(controller);
    ev.data.ptr = controller;

    int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, controller->m_handle.fd, &ev);
    if (ret == -1 && errno == ENOENT) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, controller->m_handle.fd, &ev);
    }
    return ret;
}

int EpollScheduler::addReadv(IOController* controller)
{
    if (handleReadv(controller)) {
        return OK;
    }

    auto awaitable = controller->getAwaitable<ReadvAwaitable>();
    if (awaitable == nullptr) return -1;
    struct epoll_event ev;
    ev.events = buildEpollEvents(controller);
    ev.data.ptr = controller;

    int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, controller->m_handle.fd, &ev);
    if (ret == -1 && errno == ENOENT) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, controller->m_handle.fd, &ev);
    }
    return ret;
}

int EpollScheduler::addWritev(IOController* controller)
{
    if (handleWritev(controller)) {
        return OK;
    }

    auto awaitable = controller->getAwaitable<WritevAwaitable>();
    if (awaitable == nullptr) return -1;
    struct epoll_event ev;
    ev.events = buildEpollEvents(controller);
    ev.data.ptr = controller;

    int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, controller->m_handle.fd, &ev);
    if (ret == -1 && errno == ENOENT) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, controller->m_handle.fd, &ev);
    }
    return ret;
}

int EpollScheduler::addSendFile(IOController* controller)
{
    auto awaitable = controller->getAwaitable<SendFileAwaitable>();
    if (awaitable == nullptr) return -1;

    // 先尝试直接发送
    if (handleSendFile(controller)) {
        return OK;
    }

    // 如果 EAGAIN，注册 EPOLLOUT 等待 socket 可写
    struct epoll_event ev;
    ev.events = buildEpollEvents(controller);
    ev.data.ptr = controller;

    int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, controller->m_handle.fd, &ev);
    if (ret == -1 && errno == ENOENT) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, controller->m_handle.fd, &ev);
    }
    return ret;
}

int EpollScheduler::addClose(IOController* controller)
{
    if (controller->m_handle == GHandle::invalid()) {
        return 0;
    }
    close(controller->m_handle.fd);
    controller->m_handle = GHandle::invalid();
    remove(controller);
    return 0;
}

int EpollScheduler::addFileRead(IOController* controller)
{
    // epoll 下 FILEREAD 仅用于 AIO eventfd 监听，不调用 handleFileRead
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = controller;
    return epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, controller->m_handle.fd, &ev);
}

int EpollScheduler::addFileWrite(IOController* controller)
{
    // epoll 下 FILEWRITE 仅用于 AIO eventfd 监听，不调用 handleFileWrite
    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLET;
    ev.data.ptr = controller;
    return epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, controller->m_handle.fd, &ev);
}

int EpollScheduler::remove(IOController* controller)
{
    return epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, controller->m_handle.fd, nullptr);
}

bool EpollScheduler::spawn(Coroutine co)
{
    auto* scheduler = co.belongScheduler();
    // 如果协程未绑定 scheduler，绑定到当前 scheduler
    if (!scheduler) {
        co.belongScheduler(this);
        co.threadId(m_threadId);
    } else {
        if(scheduler != this) return false;
    }
    m_coro_queue.enqueue(std::move(co));
    // 非事件循环线程入队时唤醒 epoll_wait，避免等待超时
    if (std::this_thread::get_id() != m_threadId) {
        notify();
    }
    return true;
}

bool EpollScheduler::spawnImmidiately(Coroutine co)
{
    auto* scheduler = co.belongScheduler();
    if (scheduler) {
        return false;
    }
    co.belongScheduler(this);
    co.threadId(m_threadId);
    resume(co);
    return true;
}

void EpollScheduler::processPendingCoroutines()
{
    while (true) {
        size_t count = m_coro_queue.try_dequeue_bulk(m_coro_buffer.data(), m_batch_size);
        if (count == 0) {
            break;
        }
        for (size_t i = 0; i < count; ++i) {
            Scheduler::resume(m_coro_buffer[i]);
        }
    }
}

void EpollScheduler::eventLoop()
{
    // epoll_wait 超时时间公式：timeout = tickDuration / 2
    uint64_t tick_duration_ns = m_timer_manager.during();
    int timeout_ms = static_cast<int>(tick_duration_ns / 2000000ULL);
    if (timeout_ms < 1) timeout_ms = 1;

    while (m_running.load(std::memory_order_acquire)) {
        processPendingCoroutines();
        m_timer_manager.tick();

        int nev = epoll_wait(m_epoll_fd, m_events.data(), m_max_events, timeout_ms);

        if (nev < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        for (int i = 0; i < nev; ++i) {
            struct epoll_event& ev = m_events[i];

            // 检查是否是 eventfd（用于唤醒）
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
    if (!controller || controller->m_type == IOEventType::INVALID) {
        return;
    }

    uint32_t t = static_cast<uint32_t>(controller->m_type);

    // ===== 读方向事件 =====
    if (ev.events & EPOLLIN) {
        if (t & ACCEPT) {
            handleAccept(controller);
            AcceptAwaitable* awaitable = controller->getAwaitable<AcceptAwaitable>();
            if (awaitable) awaitable->m_waker.wakeUp();
        }
        else if (t & RECV) {
            handleRecv(controller);
            RecvAwaitable* awaitable = controller->getAwaitable<RecvAwaitable>();
            if (awaitable) awaitable->m_waker.wakeUp();
        }
        else if (t & READV) {
            handleReadv(controller);
            ReadvAwaitable* awaitable = controller->getAwaitable<ReadvAwaitable>();
            if (awaitable) awaitable->m_waker.wakeUp();
        }
        else if (t & RECVFROM) {
            handleRecvFrom(controller);
            RecvFromAwaitable* awaitable = controller->getAwaitable<RecvFromAwaitable>();
            if (awaitable) awaitable->m_waker.wakeUp();
        }
        else if (t & FILEREAD) {
            // AIO eventfd 处理：读取 eventfd 获取完成的事件数
            auto* aioAwaitable = static_cast<galay::async::AioCommitAwaitable*>(controller->m_awaitable[IOController::READ]);
            if (aioAwaitable) {
                uint64_t completed = 0;
                ssize_t n = read(controller->m_handle.fd, &completed, sizeof(completed));
                if (n == sizeof(completed) && completed > 0) {
                    // 获取 AIO 结果
                    std::vector<struct io_event> events(aioAwaitable->m_pending_count);
                    int num_events = io_getevents(aioAwaitable->m_aio_ctx, 1, aioAwaitable->m_pending_count,
                                                   events.data(), nullptr);
                    if (num_events > 0) {
                        std::vector<ssize_t> results;
                        results.reserve(num_events);
                        for (int i = 0; i < num_events; ++i) {
                            results.push_back(events[i].res);
                        }
                        aioAwaitable->m_result = std::move(results);
                    } else {
                        aioAwaitable->m_result = std::unexpected(IOError(kReadFailed, errno));
                    }
                } else {
                    aioAwaitable->m_result = std::unexpected(IOError(kReadFailed, errno));
                }
                // 从 epoll 移除 eventfd
                epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, controller->m_handle.fd, nullptr);
                aioAwaitable->m_waker.wakeUp();
            }
        }
        else if (t & FILEWATCH) {
            FileWatchAwaitable* awaitable = controller->getAwaitable<FileWatchAwaitable>();
            if (awaitable) {
                // 从 inotify fd 读取事件
                ssize_t len = read(controller->m_handle.fd, awaitable->m_buffer, awaitable->m_buffer_size);
                if (len > 0) {
                    // 解析 inotify 事件
                    struct inotify_event* event = reinterpret_cast<struct inotify_event*>(awaitable->m_buffer);
                    FileWatchResult result;
                    result.isDir = (event->mask & IN_ISDIR) != 0;
                    if (event->len > 0) {
                        result.name = std::string(event->name);
                    }
                    // 转换 inotify 事件掩码到 FileWatchEvent
                    uint32_t mask = 0;
                    if (event->mask & IN_ACCESS)      mask |= static_cast<uint32_t>(FileWatchEvent::Access);
                    if (event->mask & IN_MODIFY)      mask |= static_cast<uint32_t>(FileWatchEvent::Modify);
                    if (event->mask & IN_ATTRIB)      mask |= static_cast<uint32_t>(FileWatchEvent::Attrib);
                    if (event->mask & IN_CLOSE_WRITE) mask |= static_cast<uint32_t>(FileWatchEvent::CloseWrite);
                    if (event->mask & IN_CLOSE_NOWRITE) mask |= static_cast<uint32_t>(FileWatchEvent::CloseNoWrite);
                    if (event->mask & IN_OPEN)        mask |= static_cast<uint32_t>(FileWatchEvent::Open);
                    if (event->mask & IN_MOVED_FROM)  mask |= static_cast<uint32_t>(FileWatchEvent::MovedFrom);
                    if (event->mask & IN_MOVED_TO)    mask |= static_cast<uint32_t>(FileWatchEvent::MovedTo);
                    if (event->mask & IN_CREATE)      mask |= static_cast<uint32_t>(FileWatchEvent::Create);
                    if (event->mask & IN_DELETE)      mask |= static_cast<uint32_t>(FileWatchEvent::Delete);
                    if (event->mask & IN_DELETE_SELF) mask |= static_cast<uint32_t>(FileWatchEvent::DeleteSelf);
                    if (event->mask & IN_MOVE_SELF)   mask |= static_cast<uint32_t>(FileWatchEvent::MoveSelf);
                    result.event = static_cast<FileWatchEvent>(mask);
                    awaitable->m_result = std::move(result);
                } else if (len == 0) {
                    awaitable->m_result = std::unexpected(IOError(kReadFailed, 0));
                } else {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        awaitable->m_result = std::unexpected(IOError(kReadFailed, static_cast<uint32_t>(errno)));
                    }
                }
                awaitable->m_waker.wakeUp();
            }
        }
        else if (t & RECV_NOTIFY) {
            RecvNotifyAwaitable* awaitable = controller->getAwaitable<RecvNotifyAwaitable>();
            if (awaitable) awaitable->m_waker.wakeUp();
        }
        else {
            // 残留的 EPOLLIN 事件，m_type 中已无读类型
            // syncEpollEvents 会在末尾更新 epoll 注册
        }
    }

    // ===== 写方向事件 =====
    if (ev.events & EPOLLOUT) {
        if (t & CONNECT) {
            handleConnect(controller);
            ConnectAwaitable* awaitable = controller->getAwaitable<ConnectAwaitable>();
            if (awaitable) awaitable->m_waker.wakeUp();
        }
        else if (t & SEND) {
            handleSend(controller);
            SendAwaitable* awaitable = controller->getAwaitable<SendAwaitable>();
            if (awaitable) awaitable->m_waker.wakeUp();
        }
        else if (t & WRITEV) {
            handleWritev(controller);
            WritevAwaitable* awaitable = controller->getAwaitable<WritevAwaitable>();
            if (awaitable) awaitable->m_waker.wakeUp();
        }
        else if (t & SENDTO) {
            handleSendTo(controller);
            SendToAwaitable* awaitable = controller->getAwaitable<SendToAwaitable>();
            if (awaitable) awaitable->m_waker.wakeUp();
        }
        else if (t & FILEWRITE) {
            handleFileWrite(controller);
            FileWriteAwaitable* awaitable = controller->getAwaitable<FileWriteAwaitable>();
            if (awaitable) awaitable->m_waker.wakeUp();
        }
        else if (t & SENDFILE) {
            handleSendFile(controller);
            SendFileAwaitable* awaitable = controller->getAwaitable<SendFileAwaitable>();
            if (awaitable) awaitable->m_waker.wakeUp();
        }
        else if (t & SEND_NOTIFY) {
            SendNotifyAwaitable* awaitable = controller->getAwaitable<SendNotifyAwaitable>();
            if (awaitable) awaitable->m_waker.wakeUp();
        }
        else {
            // 残留的 EPOLLOUT 事件，m_type 中已无写类型（对端 awaitable 已完成）
            // ET 模式下必须更新 epoll 注册，否则此事件不会再触发且掩码过时
        }
    }

    // 事件处理完毕后，同步 epoll 注册与当前 m_type
    // 防止 ET 模式下残留事件掩码导致后续事件丢失
    syncEpollEvents(controller);
}

void EpollScheduler::syncEpollEvents(IOController* controller)
{
    uint32_t newEvents = buildEpollEvents(controller);
    if (newEvents == EPOLLET) {
        epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, controller->m_handle.fd, nullptr);
        return;
    }
    struct epoll_event ev;
    ev.events = newEvents;
    ev.data.ptr = controller;
    epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, controller->m_handle.fd, &ev);
}

bool EpollScheduler::handleAccept(IOController* controller)
{
    AcceptAwaitable* awaitable = controller->getAwaitable<AcceptAwaitable>();
    if (awaitable == nullptr) return false;
    sockaddr_storage addr{};
    socklen_t addr_len = sizeof(addr);
    GHandle handle {
        .fd = accept(controller->m_handle.fd, reinterpret_cast<sockaddr*>(&addr), &addr_len),
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

bool EpollScheduler::handleRecv(IOController* controller)
{
    RecvAwaitable* awaitable = controller->getAwaitable<RecvAwaitable>();
    if (awaitable == nullptr) return false;
    Bytes bytes;
    int recvBytes = recv(controller->m_handle.fd, awaitable->m_buffer, awaitable->m_length, 0);
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
    SendAwaitable* awaitable = controller->getAwaitable<SendAwaitable>();
    if (awaitable == nullptr) return false;
    ssize_t sentBytes = send(controller->m_handle.fd, awaitable->m_buffer, awaitable->m_length, 0);

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

bool EpollScheduler::handleReadv(IOController* controller)
{
    ReadvAwaitable* awaitable = controller->getAwaitable<ReadvAwaitable>();
    if (awaitable == nullptr) return false;

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

bool EpollScheduler::handleWritev(IOController* controller)
{
    WritevAwaitable* awaitable = controller->getAwaitable<WritevAwaitable>();
    if (awaitable == nullptr) return false;

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

bool EpollScheduler::handleSendFile(IOController* controller)
{
    SendFileAwaitable* awaitable = controller->getAwaitable<SendFileAwaitable>();
    if (awaitable == nullptr) return false;

    // Linux sendfile 参数: sendfile(out_fd, in_fd, offset, count)
    off_t offset = awaitable->m_offset;
    ssize_t sentBytes = ::sendfile(controller->m_handle.fd, awaitable->m_file_fd,
                                   &offset, awaitable->m_count);

    if (sentBytes > 0) {
        // 成功发送
        awaitable->m_result = static_cast<size_t>(sentBytes);
        return true;
    } else if (sentBytes == 0) {
        // 没有数据发送（可能文件已到末尾）
        awaitable->m_result = 0;
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
    ConnectAwaitable* awaitable = controller->getAwaitable<ConnectAwaitable>();
    if (awaitable == nullptr) return false;

    const Host& host = awaitable->m_host;
    int result = connect(controller->m_handle.fd, host.sockAddr(), host.addrLen());

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

bool EpollScheduler::handleFileRead(IOController* controller)
{
    FileReadAwaitable* awaitable = controller->getAwaitable<FileReadAwaitable>();
    if (awaitable == nullptr) return false;
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

bool EpollScheduler::handleFileWrite(IOController* controller)
{
    FileWriteAwaitable* awaitable = controller->getAwaitable<FileWriteAwaitable>();
    if (awaitable == nullptr) return false;
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

int EpollScheduler::addRecvFrom(IOController* controller)
{
    if (handleRecvFrom(controller)) {
        return OK;
    }

    auto awaitable = controller->getAwaitable<RecvFromAwaitable>();
    if (awaitable == nullptr) return -1;
    struct epoll_event ev;
    ev.events = buildEpollEvents(controller);
    ev.data.ptr = controller;

    int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, controller->m_handle.fd, &ev);
    if (ret == -1 && errno == ENOENT) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, controller->m_handle.fd, &ev);
    }
    return ret;
}

int EpollScheduler::addSendTo(IOController* controller)
{
    if (handleSendTo(controller)) {
        return OK;
    }

    auto awaitable = controller->getAwaitable<SendToAwaitable>();
    if (awaitable == nullptr) return -1;
    struct epoll_event ev;
    ev.events = buildEpollEvents(controller);
    ev.data.ptr = controller;

    int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, controller->m_handle.fd, &ev);
    if (ret == -1 && errno == ENOENT) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, controller->m_handle.fd, &ev);
    }
    return ret;
}

bool EpollScheduler::handleRecvFrom(IOController* controller)
{
    RecvFromAwaitable* awaitable = controller->getAwaitable<RecvFromAwaitable>();
    if (awaitable == nullptr) return false;
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
    SendToAwaitable* awaitable = controller->getAwaitable<SendToAwaitable>();
    if (awaitable == nullptr) return false;
    const Host& to = awaitable->m_to;

    ssize_t sentBytes = sendto(controller->m_handle.fd, awaitable->m_buffer, awaitable->m_length,
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

int EpollScheduler::addFileWatch(IOController* controller)
{
    auto awaitable = controller->getAwaitable<FileWatchAwaitable>();
    if (awaitable == nullptr) return -1;

    struct epoll_event ev;
    ev.events = buildEpollEvents(controller);
    ev.data.ptr = controller;

    int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, controller->m_handle.fd, &ev);
    if (ret == -1 && errno == EEXIST) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, controller->m_handle.fd, &ev);
    }
    return ret;
}

int EpollScheduler::addRecvNotify(IOController* controller)
{
    // 仅注册读事件，不执行IO操作
    // 用于SSL等需要自定义IO处理的场景
    struct epoll_event ev;
    ev.events = buildEpollEvents(controller);
    ev.data.ptr = controller;

    int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, controller->m_handle.fd, &ev);
    if (ret == -1 && errno == ENOENT) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, controller->m_handle.fd, &ev);
    }
    return ret;
}

int EpollScheduler::addSendNotify(IOController* controller)
{
    // 仅注册写事件，不执行IO操作
    // 用于SSL等需要自定义IO处理的场景
    struct epoll_event ev;
    ev.events = buildEpollEvents(controller);
    ev.data.ptr = controller;

    int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, controller->m_handle.fd, &ev);
    if (ret == -1 && errno == ENOENT) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, controller->m_handle.fd, &ev);
    }
    return ret;
}

}

#endif // USE_EPOLL
