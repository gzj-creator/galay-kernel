#include "IOUringScheduler.h"

#ifdef USE_IOURING
#include "galay-kernel/common/Error.h"
#include "galay-kernel/common/Log.h"
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <stdexcept>
#include <cstring>

namespace galay::kernel
{

IOUringScheduler::IOUringScheduler(int queue_depth, int batch_size)
    : m_running(false)
    , m_queue_depth(queue_depth)
    , m_batch_size(batch_size)
    , m_event_fd(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC))
{
    if (m_event_fd == -1) {
        throw std::runtime_error("Failed to create eventfd");
    }

    struct io_uring_params params;
    std::memset(&params, 0, sizeof(params));
    params.flags = IORING_SETUP_SQPOLL | IORING_SETUP_COOP_TASKRUN;
    params.sq_thread_idle = 1000;

    if (io_uring_queue_init_params(m_queue_depth, &m_ring, &params) < 0) {
        std::memset(&params, 0, sizeof(params));
        params.flags = IORING_SETUP_COOP_TASKRUN;
        if (io_uring_queue_init_params(m_queue_depth, &m_ring, &params) < 0) {
            std::memset(&params, 0, sizeof(params));
            if (io_uring_queue_init_params(m_queue_depth, &m_ring, &params) < 0) {
                close(m_event_fd);
                throw std::runtime_error("Failed to initialize io_uring");
            }
        }
    }

    m_coro_buffer.resize(m_batch_size);
}

IOUringScheduler::~IOUringScheduler()
{
    stop();
    io_uring_queue_exit(&m_ring);
    if (m_event_fd != -1) {
        close(m_event_fd);
    }
}

void IOUringScheduler::start()
{
    if (m_running.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    m_thread = std::thread([this]() {
        m_threadId = std::this_thread::get_id();
        eventLoop();
    });
}

void IOUringScheduler::stop()
{
    if (!m_running.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    notify();

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void IOUringScheduler::notify()
{
    uint64_t val = 1;
    write(m_event_fd, &val, sizeof(val));
}

int IOUringScheduler::addAccept(IOController* controller)
{
    auto awaitable = controller->getAwaitable<AcceptAwaitable>();
    if (awaitable == nullptr) return -1;

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    io_uring_prep_accept(sqe, controller->m_handle.fd,
                         awaitable->m_host->sockAddr(),
                         awaitable->m_host->addrLen(), 0);
    io_uring_sqe_set_data(sqe, controller);
    return 0;
}

int IOUringScheduler::addConnect(IOController* controller)
{
    auto awaitable = controller->getAwaitable<ConnectAwaitable>();
    if (awaitable == nullptr) return -1;

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    io_uring_prep_connect(sqe, controller->m_handle.fd,
                          awaitable->m_host.sockAddr(),
                          *awaitable->m_host.addrLen());
    io_uring_sqe_set_data(sqe, controller);
    return 0;
}

int IOUringScheduler::addRecv(IOController* controller)
{
    auto awaitable = controller->getAwaitable<RecvAwaitable>();
    if (awaitable == nullptr) return -1;

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    io_uring_prep_recv(sqe, controller->m_handle.fd,
                       awaitable->m_buffer, awaitable->m_length, 0);
    io_uring_sqe_set_data(sqe, controller);
    return 0;
}

int IOUringScheduler::addSend(IOController* controller)
{
    auto awaitable = controller->getAwaitable<SendAwaitable>();
    if (awaitable == nullptr) return -1;

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    io_uring_prep_send(sqe, controller->m_handle.fd,
                       awaitable->m_buffer, awaitable->m_length, 0);
    io_uring_sqe_set_data(sqe, controller);
    return 0;
}

int IOUringScheduler::addReadv(IOController* controller)
{
    auto awaitable = controller->getAwaitable<ReadvAwaitable>();
    if (awaitable == nullptr) return -1;

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    io_uring_prep_readv(sqe, controller->m_handle.fd,
                        awaitable->m_iovecs.data(),
                        static_cast<unsigned>(awaitable->m_iovecs.size()), 0);
    io_uring_sqe_set_data(sqe, controller);
    return 0;
}

int IOUringScheduler::addWritev(IOController* controller)
{
    auto awaitable = controller->getAwaitable<WritevAwaitable>();
    if (awaitable == nullptr) return -1;

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    io_uring_prep_writev(sqe, controller->m_handle.fd,
                         awaitable->m_iovecs.data(),
                         static_cast<unsigned>(awaitable->m_iovecs.size()), 0);
    io_uring_sqe_set_data(sqe, controller);
    return 0;
}

int IOUringScheduler::addClose(IOController* controller)
{
    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        close(controller->m_handle.fd);
        return 0;
    }

    io_uring_prep_close(sqe, controller->m_handle.fd);
    io_uring_sqe_set_data(sqe, nullptr);
    return 0;
}

int IOUringScheduler::addFileRead(IOController* controller)
{
    auto awaitable = controller->getAwaitable<FileReadAwaitable>();
    if (awaitable == nullptr) return -1;

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    io_uring_prep_read(sqe, controller->m_handle.fd,
                       awaitable->m_buffer, awaitable->m_length, awaitable->m_offset);
    io_uring_sqe_set_data(sqe, controller);
    return 0;
}

int IOUringScheduler::addFileWrite(IOController* controller)
{
    auto awaitable = controller->getAwaitable<FileWriteAwaitable>();
    if (awaitable == nullptr) return -1;

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    io_uring_prep_write(sqe, controller->m_handle.fd,
                        awaitable->m_buffer, awaitable->m_length, awaitable->m_offset);
    io_uring_sqe_set_data(sqe, controller);
    return 0;
}

int IOUringScheduler::addRecvFrom(IOController* controller)
{
    auto awaitable = controller->getAwaitable<RecvFromAwaitable>();
    if (awaitable == nullptr) return -1;

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    std::memset(&awaitable->m_msg, 0, sizeof(awaitable->m_msg));
    std::memset(&awaitable->m_addr, 0, sizeof(awaitable->m_addr));

    awaitable->m_iov.iov_base = awaitable->m_buffer;
    awaitable->m_iov.iov_len = awaitable->m_length;

    awaitable->m_msg.msg_iov = &awaitable->m_iov;
    awaitable->m_msg.msg_iovlen = 1;
    awaitable->m_msg.msg_name = &awaitable->m_addr;
    awaitable->m_msg.msg_namelen = sizeof(awaitable->m_addr);

    io_uring_prep_recvmsg(sqe, controller->m_handle.fd, &awaitable->m_msg, 0);
    io_uring_sqe_set_data(sqe, controller);
    return 0;
}

int IOUringScheduler::addSendTo(IOController* controller)
{
    auto awaitable = controller->getAwaitable<SendToAwaitable>();
    if (awaitable == nullptr) return -1;

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    std::memset(&awaitable->m_msg, 0, sizeof(awaitable->m_msg));

    awaitable->m_iov.iov_base = const_cast<char*>(awaitable->m_buffer);
    awaitable->m_iov.iov_len = awaitable->m_length;

    awaitable->m_msg.msg_iov = &awaitable->m_iov;
    awaitable->m_msg.msg_iovlen = 1;
    awaitable->m_msg.msg_name = const_cast<sockaddr*>(awaitable->m_to.sockAddr());
    awaitable->m_msg.msg_namelen = *awaitable->m_to.addrLen();

    io_uring_prep_sendmsg(sqe, controller->m_handle.fd, &awaitable->m_msg, 0);
    io_uring_sqe_set_data(sqe, controller);
    return 0;
}

int IOUringScheduler::addFileWatch(IOController* controller)
{
    auto awaitable = controller->getAwaitable<FileWatchAwaitable>();
    if (awaitable == nullptr) return -1;

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    // inotify fd 是可读的，当有事件时读取
    io_uring_prep_read(sqe, controller->m_handle.fd,
                       awaitable->m_buffer, awaitable->m_buffer_size, 0);
    io_uring_sqe_set_data(sqe, controller);
    return 0;
}

int IOUringScheduler::addRecvNotify(IOController* controller)
{
    // 仅注册读事件，不执行IO操作
    // 用于SSL等需要自定义IO处理的场景
    // io_uring 使用 poll 来监听可读事件
    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    io_uring_prep_poll_add(sqe, controller->m_handle.fd, POLLIN);
    io_uring_sqe_set_data(sqe, controller);
    return 0;
}

int IOUringScheduler::addSendNotify(IOController* controller)
{
    // 仅注册写事件，不执行IO操作
    // 用于SSL等需要自定义IO处理的场景
    // io_uring 使用 poll 来监听可写事件
    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    io_uring_prep_poll_add(sqe, controller->m_handle.fd, POLLOUT);
    io_uring_sqe_set_data(sqe, controller);
    return 0;
}

int IOUringScheduler::remove(IOController* controller)
{
    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    io_uring_prep_cancel_fd(sqe, controller->m_handle.fd, 0);
    io_uring_sqe_set_data(sqe, nullptr);
    return 0;
}

bool IOUringScheduler::spawn(Coroutine co)
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
    notify();
    return true;
}

bool IOUringScheduler::spawnImmidiately(Coroutine co)
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

void IOUringScheduler::processPendingCoroutines()
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

void IOUringScheduler::eventLoop()
{
    struct io_uring_cqe* cqe;
    struct __kernel_timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = GALAY_IOURING_WAIT_TIMEOUT_NS;

    // 添加 eventfd 到 io_uring 监听
    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (sqe) {
        io_uring_prep_read(sqe, m_event_fd, &m_eventfd_buf, sizeof(m_eventfd_buf), 0);
        io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(-1));
    }

    while (m_running.load(std::memory_order_acquire)) {
        processPendingCoroutines();
        m_timer_manager.tick();

        int submitted = io_uring_submit(&m_ring);
        if (submitted < 0 && submitted != -EBUSY) {
            LogError("io_uring_submit failed: {}", submitted);
        }

        int ret = io_uring_wait_cqe_timeout(&m_ring, &cqe, &timeout);
        if (ret < 0) {
            if (ret == -EINTR || ret == -ETIME) {
                continue;
            }
            LogError("io_uring_wait_cqe_timeout failed: {}", ret);
            break;
        }

        unsigned head;
        unsigned count = 0;
        bool eventfd_triggered = false;

        io_uring_for_each_cqe(&m_ring, head, cqe) {
            void* user_data = io_uring_cqe_get_data(cqe);

            if (user_data == reinterpret_cast<void*>(-1)) {
                eventfd_triggered = true;
            } else if (user_data != nullptr) {
                processCompletion(cqe);
            }
            ++count;
        }

        if (count > 0) {
            io_uring_cq_advance(&m_ring, count);
        }

        if (eventfd_triggered) {
            struct io_uring_sqe* new_sqe = io_uring_get_sqe(&m_ring);
            if (new_sqe) {
                io_uring_prep_read(new_sqe, m_event_fd, &m_eventfd_buf, sizeof(m_eventfd_buf), 0);
                io_uring_sqe_set_data(new_sqe, reinterpret_cast<void*>(-1));
            }
        }
    }
}

void IOUringScheduler::processCompletion(struct io_uring_cqe* cqe)
{
    IOController* controller = static_cast<IOController*>(io_uring_cqe_get_data(cqe));
    if (!controller) {
        return;
    }

    int res = cqe->res;

    switch (controller->m_type)
    {
    case ACCEPT:
    {
        AcceptAwaitable* awaitable = controller->getAwaitable<AcceptAwaitable>();
        if (awaitable == nullptr) return;
        if (res >= 0) {
            GHandle handle { .fd = res };
            awaitable->m_result = handle;
        } else {
            awaitable->m_result = std::unexpected(IOError(kAcceptFailed, static_cast<uint32_t>(-res)));
        }
        awaitable->m_waker.wakeUp();
        break;
    }
    case CONNECT:
    {
        ConnectAwaitable* awaitable = controller->getAwaitable<ConnectAwaitable>();
        if (awaitable == nullptr) return;
        if (res == 0) {
            awaitable->m_result = {};
        } else {
            awaitable->m_result = std::unexpected(IOError(kConnectFailed, static_cast<uint32_t>(-res)));
        }
        awaitable->m_waker.wakeUp();
        break;
    }
    case RECV:
    {
        RecvAwaitable* awaitable = controller->getAwaitable<RecvAwaitable>();
        if (awaitable == nullptr) return;
        if (res > 0) {
            Bytes bytes = Bytes::fromCString(awaitable->m_buffer, res, res);
            awaitable->m_result = std::move(bytes);
        } else if (res == 0) {
            awaitable->m_result = std::unexpected(IOError(kDisconnectError, 0));
        } else {
            awaitable->m_result = std::unexpected(IOError(kRecvFailed, static_cast<uint32_t>(-res)));
        }
        awaitable->m_waker.wakeUp();
        break;
    }
    case SEND:
    {
        SendAwaitable* awaitable = controller->getAwaitable<SendAwaitable>();
        if (awaitable == nullptr) return;
        if (res >= 0) {
            awaitable->m_result = static_cast<size_t>(res);
        } else {
            awaitable->m_result = std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(-res)));
        }
        awaitable->m_waker.wakeUp();
        break;
    }
    case READV:
    {
        ReadvAwaitable* awaitable = controller->getAwaitable<ReadvAwaitable>();
        if (awaitable == nullptr) return;
        if (res > 0) {
            awaitable->m_result = static_cast<size_t>(res);
        } else if (res == 0) {
            awaitable->m_result = std::unexpected(IOError(kDisconnectError, 0));
        } else {
            awaitable->m_result = std::unexpected(IOError(kRecvFailed, static_cast<uint32_t>(-res)));
        }
        awaitable->m_waker.wakeUp();
        break;
    }
    case WRITEV:
    {
        WritevAwaitable* awaitable = controller->getAwaitable<WritevAwaitable>();
        if (awaitable == nullptr) return;
        if (res >= 0) {
            awaitable->m_result = static_cast<size_t>(res);
        } else {
            awaitable->m_result = std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(-res)));
        }
        awaitable->m_waker.wakeUp();
        break;
    }
    case FILEREAD:
    {
        FileReadAwaitable* awaitable = controller->getAwaitable<FileReadAwaitable>();
        if (awaitable == nullptr) return;
        if (res > 0) {
            Bytes bytes = Bytes::fromCString(awaitable->m_buffer, res, res);
            awaitable->m_result = std::move(bytes);
        } else if (res == 0) {
            awaitable->m_result = Bytes();
        } else {
            awaitable->m_result = std::unexpected(IOError(kReadFailed, static_cast<uint32_t>(-res)));
        }
        awaitable->m_waker.wakeUp();
        break;
    }
    case FILEWRITE:
    {
        FileWriteAwaitable* awaitable = controller->getAwaitable<FileWriteAwaitable>();
        if (awaitable == nullptr) return;
        if (res >= 0) {
            awaitable->m_result = static_cast<size_t>(res);
        } else {
            awaitable->m_result = std::unexpected(IOError(kWriteFailed, static_cast<uint32_t>(-res)));
        }
        awaitable->m_waker.wakeUp();
        break;
    }
    case RECVFROM:
    {
        RecvFromAwaitable* awaitable = controller->getAwaitable<RecvFromAwaitable>();
        if (awaitable == nullptr) return;
        if (res > 0) {
            Bytes bytes = Bytes::fromCString(awaitable->m_buffer, res, res);
            awaitable->m_result = std::move(bytes);
            if (awaitable->m_from) {
                *awaitable->m_from = Host::fromSockAddr(awaitable->m_addr);
            }
        } else if (res == 0) {
            awaitable->m_result = std::unexpected(IOError(kRecvFailed, 0));
        } else {
            awaitable->m_result = std::unexpected(IOError(kRecvFailed, static_cast<uint32_t>(-res)));
        }
        awaitable->m_waker.wakeUp();
        break;
    }
    case SENDTO:
    {
        SendToAwaitable* awaitable = controller->getAwaitable<SendToAwaitable>();
        if (awaitable == nullptr) return;
        if (res >= 0) {
            awaitable->m_result = static_cast<size_t>(res);
        } else {
            awaitable->m_result = std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(-res)));
        }
        awaitable->m_waker.wakeUp();
        break;
    }
    case FILEWATCH:
    {
        FileWatchAwaitable* awaitable = controller->getAwaitable<FileWatchAwaitable>();
        if (awaitable == nullptr) return;
        if (res > 0) {
            struct inotify_event* event = reinterpret_cast<struct inotify_event*>(awaitable->m_buffer);
            FileWatchResult result;
            result.isDir = (event->mask & IN_ISDIR) != 0;
            if (event->len > 0) {
                result.name = std::string(event->name);
            }
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
        } else if (res == 0) {
            awaitable->m_result = std::unexpected(IOError(kReadFailed, 0));
        } else {
            awaitable->m_result = std::unexpected(IOError(kReadFailed, static_cast<uint32_t>(-res)));
        }
        awaitable->m_waker.wakeUp();
        break;
    }
    case RECV_NOTIFY:
    {
        // poll 完成，仅唤醒协程，不执行IO操作
        RecvNotifyAwaitable* awaitable = controller->getAwaitable<RecvNotifyAwaitable>();
        if (awaitable == nullptr) return;
        awaitable->m_waker.wakeUp();
        break;
    }
    case SEND_NOTIFY:
    {
        // poll 完成，仅唤醒协程，不执行IO操作
        SendNotifyAwaitable* awaitable = controller->getAwaitable<SendNotifyAwaitable>();
        if (awaitable == nullptr) return;
        awaitable->m_waker.wakeUp();
        break;
    }
    default:
        break;
    }
}

}

#endif // USE_IOURING
