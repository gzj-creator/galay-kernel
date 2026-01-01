#include "IOUringScheduler.h"
#include "Scheduler.h"
#include "common/Defn.hpp"
#include "common/Error.h"

#ifdef USE_IOURING

#include "Awaitable.h"
#include <sys/eventfd.h>
#include <sys/socket.h>
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
        if (io_uring_queue_init_params(m_queue_depth, &m_ring, &params) < 0) {
            close(m_event_fd);
            throw std::runtime_error("Failed to initialize io_uring");
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

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (sqe) {
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data(sqe, nullptr);
        io_uring_submit(&m_ring);
    }
}

int IOUringScheduler::addAccept(IOController* controller)
{
    AcceptAwaitable* awaitable = static_cast<AcceptAwaitable*>(controller->m_awaitable);

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    io_uring_prep_accept(sqe, awaitable->m_listen_handle.fd,
                         awaitable->m_host->sockAddr(),
                         awaitable->m_host->addrLen(), 0);
    io_uring_sqe_set_data(sqe, controller);
    io_uring_submit(&m_ring);
    return 0;
}

int IOUringScheduler::addConnect(IOController* controller)
{
    ConnectAwaitable* awaitable = static_cast<ConnectAwaitable*>(controller->m_awaitable);

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    io_uring_prep_connect(sqe, awaitable->m_handle.fd,
                          awaitable->m_host.sockAddr(),
                          *awaitable->m_host.addrLen());
    io_uring_sqe_set_data(sqe, controller);
    io_uring_submit(&m_ring);
    return 0;
}

int IOUringScheduler::addRecv(IOController* controller)
{
    RecvAwaitable* awaitable = static_cast<RecvAwaitable*>(controller->m_awaitable);

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    io_uring_prep_recv(sqe, awaitable->m_handle.fd,
                       awaitable->m_buffer, awaitable->m_length, 0);
    io_uring_sqe_set_data(sqe, controller);
    io_uring_submit(&m_ring);
    return 0;
}

int IOUringScheduler::addSend(IOController* controller)
{
    SendAwaitable* awaitable = static_cast<SendAwaitable*>(controller->m_awaitable);

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    io_uring_prep_send(sqe, awaitable->m_handle.fd,
                       awaitable->m_buffer, awaitable->m_length, 0);
    io_uring_sqe_set_data(sqe, controller);
    io_uring_submit(&m_ring);
    return 0;
}

int IOUringScheduler::addClose(int fd)
{
    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        close(fd);
        return 0;
    }

    io_uring_prep_close(sqe, fd);
    io_uring_sqe_set_data(sqe, nullptr);
    io_uring_submit(&m_ring);
    return 0;
}

int IOUringScheduler::addFileRead(IOController* controller)
{
    FileReadAwaitable* awaitable = static_cast<FileReadAwaitable*>(controller->m_awaitable);

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    // io_uring 原生支持文件读取
    io_uring_prep_read(sqe, awaitable->m_handle.fd,
                       awaitable->m_buffer, awaitable->m_length, awaitable->m_offset);
    io_uring_sqe_set_data(sqe, controller);
    io_uring_submit(&m_ring);
    return 0;
}

int IOUringScheduler::addFileWrite(IOController* controller)
{
    FileWriteAwaitable* awaitable = static_cast<FileWriteAwaitable*>(controller->m_awaitable);

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    // io_uri
    io_uring_prep_write(sqe, awaitable->m_handle.fd,
                        awaitable->m_buffer, awaitable->m_length, awaitable->m_offset);
    io_uring_sqe_set_data(sqe, controller);
    io_uring_submit(&m_ring);
    return 0;
}

int IOUringScheduler::addRecvFrom(IOController* controller)
{
    RecvFromAwaitable* awaitable = static_cast<RecvFromAwaitable*>(controller->m_awaitable);

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    // 使用 recvmsg 来接收 UDP 数据报和源地址
    struct msghdr msg;
    struct iovec iov;
    sockaddr_storage addr;

    std::memset(&msg, 0, sizeof(msg));
    std::memset(&addr, 0, sizeof(addr));

    iov.iov_base = awaitable->m_buffer;
    iov.iov_len = awaitable->m_length;

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_name = &addr;
    msg.msg_namelen = sizeof(addr);

    io_uring_prep_recvmsg(sqe, awaitable->m_handle.fd, &msg, 0);
    io_uring_sqe_set_data(sqe, controller);
    io_uring_submit(&m_ring);
    return 0;
}

int IOUringScheduler::addSendTo(IOController* controller)
{
    SendToAwaitable* awaitable = static_cast<SendToAwaitable*>(controller->m_awaitable);

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    // 使用 sendmsg 来发送 UDP 数据报到指定地址
    struct msghdr msg;
    struct iovec iov;

    std::memset(&msg, 0, sizeof(msg));

    iov.iov_base = const_cast<char*>(awaitable->m_buffer);
    iov.iov_len = awaitable->m_length;

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_name = const_cast<sockaddr*>(awaitable->m_to.sockAddr());
    msg.msg_namelen = awaitable->m_to.addrLen();

    io_uring_prep_sendmsg(sqe, awaitable->m_handle.fd, &msg, 0);
    io_uring_sqe_set_data(sqe, controller);
    io_uring_submit(&m_ring);
    return 0;
}

int IOUringScheduler::remove(int fd)
{
    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    io_uring_prep_cancel_fd(sqe, fd, 0);
    io_uring_sqe_set_data(sqe, nullptr);
    io_uring_submit(&m_ring);
    return 0;
}

void IOUringScheduler::spawn(Coroutine coro)
{
    m_coro_queue.enqueue(std::move(coro));
    notify();
}

void IOUringScheduler::processPendingCoroutines()
{
    size_t count = m_coro_queue.try_dequeue_bulk(m_coro_buffer.data(), m_batch_size);
    for (size_t i = 0; i < count; ++i) {
        Scheduler::resume(m_coro_buffer[i]);
    }
}

void IOUringScheduler::eventLoop()
{
    struct io_uring_cqe* cqe;

    while (m_running.load(std::memory_order_acquire)) {
        processPendingCoroutines();

        int ret = io_uring_wait_cqe_timeout(&m_ring, &cqe, nullptr);
        if (ret < 0) {
            if (ret == -EINTR || ret == -ETIME) {
                continue;
            }
            break;
        }

        unsigned head;
        unsigned count = 0;
        io_uring_for_each_cqe(&m_ring, head, cqe) {
            processCompletion(cqe);
            ++count;
        }

        if (count > 0) {
            io_uring_cq_advance(&m_ring, count);
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
        AcceptAwaitable* awaitable = static_cast<AcceptAwaitable*>(controller->m_awaitable);
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
        ConnectAwaitable* awaitable = static_cast<ConnectAwaitable*>(controller->m_awaitable);
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
        RecvAwaitable* awaitable = static_cast<RecvAwaitable*>(controller->m_awaitable);
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
        SendAwaitable* awaitable = static_cast<SendAwaitable*>(controller->m_awaitable);
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
        FileReadAwaitable* awaitable = static_cast<FileReadAwaitable*>(controller->m_awaitable);
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
        FileWriteAwaitable* awaitable = static_cast<FileWriteAwaitable*>(controller->m_awaitable);
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
        RecvFromAwaitable* awaitable = static_cast<RecvFromAwaitable*>(controller->m_awaitable);
        if (res > 0) {
            Bytes bytes = Bytes::fromCString(awaitable->m_buffer, res, res);
            awaitable->m_result = std::move(bytes);
            // 注意：io_uring的recvmsg需要在awaitable中保存msghdr结构来获取源地址
            // 这里需要从msghdr中提取地址信息
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
        SendToAwaitable* awaitable = static_cast<SendToAwaitable*>(controller->m_awaitable);
        if (res >= 0) {
            awaitable->m_result = static_cast<size_t>(res);
        } else {
            awaitable->m_result = std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(-res)));
        }
        awaitable->m_waker.wakeUp();
        break;
    }
    default:
        break;
    }
}

}

#endif // USE_IOURING
