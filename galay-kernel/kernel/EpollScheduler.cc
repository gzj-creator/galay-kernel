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
    auto awaitable = controller->getAwaitable<AcceptAwaitable>();
    if(awaitable == nullptr) return -1;
    auto result = handleAccept(controller->m_handle);
    if(awaitable->handleComplete(std::move(result.first), std::move(result.second))) {
        return OK;
    }
    struct epoll_event ev;
    ev.events = buildEpollEvents(controller);
    ev.data.ptr = controller;
    return epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, controller->m_handle.fd, &ev);
}

int EpollScheduler::addConnect(IOController* controller)
{
    auto awaitable = controller->getAwaitable<ConnectAwaitable>();
    if(awaitable == nullptr) return -1;
    auto result = handleConnect(controller->m_handle, awaitable->m_host);
    if(awaitable->handleComplete(std::move(result))) {
        return OK;
    }
    struct epoll_event ev;
    ev.events = buildEpollEvents(controller);
    ev.data.ptr = controller;
    return epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, controller->m_handle.fd, &ev);
}

int EpollScheduler::addRecv(IOController* controller)
{
    auto awaitable = controller->getAwaitable<RecvAwaitable>();
    if(awaitable == nullptr) return -1;
    auto result = handleRecv(controller->m_handle, awaitable->m_buffer, awaitable->m_length);
    if(awaitable->handleComplete(std::move(result))) {
        return OK;
    }
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
    auto awaitable = controller->getAwaitable<SendAwaitable>();
    if(awaitable == nullptr) return -1;
    auto result = handleSend(controller->m_handle, awaitable->m_buffer, awaitable->m_length);
    if(awaitable->handleComplete(std::move(result))) {
        return OK;
    }
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
    auto awaitable = controller->getAwaitable<ReadvAwaitable>();
    if(awaitable == nullptr) return -1;
    auto result = handleReadv(controller->m_handle, awaitable->m_iovecs.data(), static_cast<int>(awaitable->m_iovecs.size()));
    if(awaitable->handleComplete(std::move(result))) {
        return OK;
    }
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
    auto awaitable = controller->getAwaitable<WritevAwaitable>();
    if(awaitable == nullptr) return -1;
    auto result = handleWritev(controller->m_handle, awaitable->m_iovecs.data(), static_cast<int>(awaitable->m_iovecs.size()));
    if(awaitable->handleComplete(std::move(result))) {
        return OK;
    }
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
    if(awaitable == nullptr) return -1;
    auto result = handleSendFile(controller->m_handle, awaitable->m_file_fd, awaitable->m_offset, awaitable->m_count);
    if(awaitable->handleComplete(std::move(result))) {
        return OK;
    }
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
            auto result = handleAccept(controller->m_handle);
            AcceptAwaitable* awaitable = controller->getAwaitable<AcceptAwaitable>();
            if (awaitable) {
                if(awaitable->handleComplete(std::move(result.first), std::move(result.second))) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & RECV) {
            RecvAwaitable* awaitable = controller->getAwaitable<RecvAwaitable>();
            if (awaitable) {
                auto result = handleRecv(controller->m_handle, awaitable->m_buffer, awaitable->m_length);
                if(awaitable->handleComplete(std::move(result))) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & READV) {
            ReadvAwaitable* awaitable = controller->getAwaitable<ReadvAwaitable>();
            if (awaitable) {
                auto result = handleReadv(controller->m_handle, awaitable->m_iovecs.data(), static_cast<int>(awaitable->m_iovecs.size()));
                if(awaitable->handleComplete(std::move(result))) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & RECVFROM) {
            RecvFromAwaitable* awaitable = controller->getAwaitable<RecvFromAwaitable>();
            if (awaitable) {
                auto result = handleRecvFrom(controller->m_handle, awaitable->m_buffer, awaitable->m_length);
                if(awaitable->handleComplete(std::move(result.first), std::move(result.second))) {
                    awaitable->m_waker.wakeUp();
                }
            }
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
    }

    // ===== 写方向事件 =====
    if (ev.events & EPOLLOUT) {
        if (t & CONNECT) {
            ConnectAwaitable* awaitable = controller->getAwaitable<ConnectAwaitable>();
            if (awaitable) {
                auto result = handleConnect(controller->m_handle, awaitable->m_host);
                if(awaitable->handleComplete(std::move(result))) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & SEND) {
            SendAwaitable* awaitable = controller->getAwaitable<SendAwaitable>();
            if (awaitable) {
                auto result = handleSend(controller->m_handle, awaitable->m_buffer, awaitable->m_length);
                if(awaitable->handleComplete(std::move(result))) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & WRITEV) {
            WritevAwaitable* awaitable = controller->getAwaitable<WritevAwaitable>();
            if (awaitable) {
                auto result = handleWritev(controller->m_handle, awaitable->m_iovecs.data(), static_cast<int>(awaitable->m_iovecs.size()));
                if(awaitable->handleComplete(std::move(result))) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & SENDTO) {
            SendToAwaitable* awaitable = controller->getAwaitable<SendToAwaitable>();
            if (awaitable) {
                auto result = handleSendTo(controller->m_handle, awaitable->m_buffer, awaitable->m_length, awaitable->m_to);
                if(awaitable->handleComplete(std::move(result))) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & FILEWRITE) {
            FileWriteAwaitable* awaitable = controller->getAwaitable<FileWriteAwaitable>();
            if (awaitable) {
                auto result = handleFileWrite(controller->m_handle, awaitable->m_buffer, awaitable->m_length, awaitable->m_offset);
                if(awaitable->handleComplete(std::move(result))) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & SENDFILE) {
            SendFileAwaitable* awaitable = controller->getAwaitable<SendFileAwaitable>();
            if (awaitable) {
                auto result = handleSendFile(controller->m_handle, awaitable->m_file_fd, awaitable->m_offset, awaitable->m_count);
                if(awaitable->handleComplete(std::move(result))) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & SEND_NOTIFY) {
            SendNotifyAwaitable* awaitable = controller->getAwaitable<SendNotifyAwaitable>();
            if (awaitable) awaitable->m_waker.wakeUp();
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

int EpollScheduler::addRecvFrom(IOController* controller)
{
    auto awaitable = controller->getAwaitable<RecvFromAwaitable>();
    if(awaitable == nullptr) return -1;
    auto result = handleRecvFrom(controller->m_handle, awaitable->m_buffer, awaitable->m_length);
    if(awaitable->handleComplete(std::move(result.first), std::move(result.second))) {
        return OK;
    }
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
    auto awaitable = controller->getAwaitable<SendToAwaitable>();
    if(awaitable == nullptr) return -1;
    auto result = handleSendTo(controller->m_handle, awaitable->m_buffer, awaitable->m_length, awaitable->m_to);
    if(awaitable->handleComplete(std::move(result))) {
        return OK;
    }
    struct epoll_event ev;
    ev.events = buildEpollEvents(controller);
    ev.data.ptr = controller;

    int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, controller->m_handle.fd, &ev);
    if (ret == -1 && errno == ENOENT) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, controller->m_handle.fd, &ev);
    }
    return ret;
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
