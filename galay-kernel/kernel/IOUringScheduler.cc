#include "IOUringScheduler.h"

#ifdef USE_IOURING
#include "galay-kernel/common/Error.h"
#include "galay-kernel/common/Log.h"
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
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
    io_uring_sqe_set_data(sqe, &controller->m_sqe_tag[IOController::READ]);
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
    io_uring_sqe_set_data(sqe, &controller->m_sqe_tag[IOController::WRITE]);
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
    io_uring_sqe_set_data(sqe, &controller->m_sqe_tag[IOController::READ]);
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
    io_uring_sqe_set_data(sqe, &controller->m_sqe_tag[IOController::WRITE]);
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
    io_uring_sqe_set_data(sqe, &controller->m_sqe_tag[IOController::READ]);
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
    io_uring_sqe_set_data(sqe, &controller->m_sqe_tag[IOController::WRITE]);
    return 0;
}

int IOUringScheduler::addSendFile(IOController* controller)
{
    auto awaitable = controller->getAwaitable<SendFileAwaitable>();
    if (awaitable == nullptr) return -1;

    // io_uring 的 splice 不能直接从普通文件到 socket
    // 使用 poll 监听 socket 可写，然后在 processCompletion 中调用 sendfile
    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    // 使用 poll 等待 socket 可写
    io_uring_prep_poll_add(sqe, controller->m_handle.fd, POLLOUT);
    io_uring_sqe_set_data(sqe, &controller->m_sqe_tag[IOController::WRITE]);
    return 0;
}

int IOUringScheduler::addClose(IOController* controller)
{
    if (controller == nullptr || controller->m_handle == GHandle::invalid()) {
        return 0;
    }

    const int fd = controller->m_handle.fd;

    // Best-effort cancel outstanding requests for this fd before close.
    struct io_uring_sqe* cancel_sqe = io_uring_get_sqe(&m_ring);
    if (cancel_sqe) {
        io_uring_prep_cancel_fd(cancel_sqe, fd, 0);
        io_uring_sqe_set_data(cancel_sqe, nullptr);
    }

    struct io_uring_sqe* close_sqe = io_uring_get_sqe(&m_ring);
    if (!close_sqe) {
        close(fd);
    } else {
        io_uring_prep_close(close_sqe, fd);
        io_uring_sqe_set_data(close_sqe, nullptr);
    }

    controller->m_type = IOEventType::INVALID;
    controller->m_awaitable[IOController::READ] = nullptr;
    controller->m_awaitable[IOController::WRITE] = nullptr;
    controller->m_handle = GHandle::invalid();
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
    io_uring_sqe_set_data(sqe, &controller->m_sqe_tag[IOController::READ]);
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
    io_uring_sqe_set_data(sqe, &controller->m_sqe_tag[IOController::WRITE]);
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
    io_uring_sqe_set_data(sqe, &controller->m_sqe_tag[IOController::READ]);
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
    io_uring_sqe_set_data(sqe, &controller->m_sqe_tag[IOController::WRITE]);
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
    io_uring_sqe_set_data(sqe, &controller->m_sqe_tag[IOController::READ]);
    return 0;
}

int IOUringScheduler::addCustom(IOController* controller)
{
    auto* custom = controller->getAwaitable<CustomAwaitable>();
    if (custom == nullptr) return -1;
    while (auto* task = custom->front()) {
        bool done = task->context->handleComplete(nullptr, controller->m_handle);
        if (done) { custom->popFront(); continue; }
        return submitCustomSqe(custom->resolveTaskEventType(*task), task->context, controller);
    }
    return OK;  // 队列空，由调用方决定是否唤醒
}

int IOUringScheduler::submitCustomSqe(IOEventType type, IOContextBase* ctx, IOController* controller)
{
    auto* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) return -EAGAIN;

    switch (type) {
    case RECV: {
        auto* c = static_cast<RecvIOContext*>(ctx);
        io_uring_prep_recv(sqe, controller->m_handle.fd, c->m_buffer, c->m_length, 0);
        break;
    }
    case SEND: {
        auto* c = static_cast<SendIOContext*>(ctx);
        io_uring_prep_send(sqe, controller->m_handle.fd, c->m_buffer, c->m_length, 0);
        break;
    }
    case ACCEPT: {
        auto* c = static_cast<AcceptIOContext*>(ctx);
        io_uring_prep_accept(sqe, controller->m_handle.fd,
                             c->m_host->sockAddr(), c->m_host->addrLen(), 0);
        break;
    }
    case CONNECT: {
        auto* c = static_cast<ConnectIOContext*>(ctx);
        io_uring_prep_connect(sqe, controller->m_handle.fd,
                              c->m_host.sockAddr(), *c->m_host.addrLen());
        break;
    }
    case READV: {
        auto* c = static_cast<ReadvIOContext*>(ctx);
        io_uring_prep_readv(sqe, controller->m_handle.fd,
                            c->m_iovecs.data(), static_cast<unsigned>(c->m_iovecs.size()), 0);
        break;
    }
    case WRITEV: {
        auto* c = static_cast<WritevIOContext*>(ctx);
        io_uring_prep_writev(sqe, controller->m_handle.fd,
                             c->m_iovecs.data(), static_cast<unsigned>(c->m_iovecs.size()), 0);
        break;
    }
    case FILEREAD: {
        auto* c = static_cast<FileReadIOContext*>(ctx);
        io_uring_prep_read(sqe, controller->m_handle.fd, c->m_buffer, c->m_length, c->m_offset);
        break;
    }
    case FILEWRITE: {
        auto* c = static_cast<FileWriteIOContext*>(ctx);
        io_uring_prep_write(sqe, controller->m_handle.fd, c->m_buffer, c->m_length, c->m_offset);
        break;
    }
    case RECVFROM: {
        auto* c = static_cast<RecvFromIOContext*>(ctx);
        std::memset(&c->m_msg, 0, sizeof(c->m_msg));
        std::memset(&c->m_addr, 0, sizeof(c->m_addr));
        c->m_iov.iov_base = c->m_buffer;
        c->m_iov.iov_len = c->m_length;
        c->m_msg.msg_iov = &c->m_iov;
        c->m_msg.msg_iovlen = 1;
        c->m_msg.msg_name = &c->m_addr;
        c->m_msg.msg_namelen = sizeof(c->m_addr);
        io_uring_prep_recvmsg(sqe, controller->m_handle.fd, &c->m_msg, 0);
        break;
    }
    case SENDTO: {
        auto* c = static_cast<SendToIOContext*>(ctx);
        std::memset(&c->m_msg, 0, sizeof(c->m_msg));
        c->m_iov.iov_base = const_cast<char*>(c->m_buffer);
        c->m_iov.iov_len = c->m_length;
        c->m_msg.msg_iov = &c->m_iov;
        c->m_msg.msg_iovlen = 1;
        c->m_msg.msg_name = const_cast<sockaddr*>(c->m_to.sockAddr());
        c->m_msg.msg_namelen = *c->m_to.addrLen();
        io_uring_prep_sendmsg(sqe, controller->m_handle.fd, &c->m_msg, 0);
        break;
    }
    case SENDFILE: {
        io_uring_prep_poll_add(sqe, controller->m_handle.fd, POLLOUT);
        break;
    }
    case FILEWATCH: {
        auto* c = static_cast<FileWatchIOContext*>(ctx);
        io_uring_prep_read(sqe, controller->m_handle.fd, c->m_buffer, c->m_buffer_size, 0);
        break;
    }
    default:
        return -1;
    }

    // Set sqe_type on the CustomAwaitable so processCompletion routes correctly
    auto* custom = controller->getAwaitable<CustomAwaitable>();
    custom->m_sqe_type = CUSTOM;
    io_uring_sqe_set_data(sqe, &controller->m_sqe_tag[IOController::READ]);
    return 0;
}

int IOUringScheduler::remove(IOController* controller)
{
    if (controller == nullptr || controller->m_handle == GHandle::invalid()) {
        return 0;
    }

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
    void* data = io_uring_cqe_get_data(cqe);
    if (!data) return;

    auto* tag = static_cast<SqeTag*>(data);
    auto* controller = tag->owner;
    auto* base = static_cast<AwaitableBase*>(controller->m_awaitable[tag->slot]);
    if (!base) return;  // awaitable 已被超时清理，安全忽略

    switch (base->m_sqe_type)
    {
    case ACCEPT:
    {
        auto* awaitable = static_cast<AcceptAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            addAccept(controller);
        }
        break;
    }
    case CONNECT:
    {
        auto* awaitable = static_cast<ConnectAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            addConnect(controller);
        }
        break;
    }
    case RECV:
    {
        auto* awaitable = static_cast<RecvAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            addRecv(controller);
        }
        break;
    }
    case SEND:
    {
        auto* awaitable = static_cast<SendAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            addSend(controller);
        }
        break;
    }
    case READV:
    {
        auto* awaitable = static_cast<ReadvAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            addReadv(controller);
        }
        break;
    }
    case WRITEV:
    {
        auto* awaitable = static_cast<WritevAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            addWritev(controller);
        }
        break;
    }
    case FILEREAD:
    {
        auto* awaitable = static_cast<FileReadAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            addFileRead(controller);
        }
        break;
    }
    case FILEWRITE:
    {
        auto* awaitable = static_cast<FileWriteAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            addFileWrite(controller);
        }
        break;
    }
    case RECVFROM:
    {
        auto* awaitable = static_cast<RecvFromAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            addRecvFrom(controller);
        }
        break;
    }
    case SENDTO:
    {
        auto* awaitable = static_cast<SendToAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            addSendTo(controller);
        }
        break;
    }
    case FILEWATCH:
    {
        auto* awaitable = static_cast<FileWatchAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            addFileWatch(controller);
        }
        break;
    }
    case SENDFILE:
    {
        auto* awaitable = static_cast<SendFileAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            addSendFile(controller);
        }
        break;
    }
    case CUSTOM:
    {
        auto* custom = static_cast<CustomAwaitable*>(base);
        auto* task = custom->front();
        if (task) {
            bool done = task->context->handleComplete(cqe, controller->m_handle);
            if (done) {
                custom->popFront();
                if (custom->empty()) {
                    custom->m_waker.wakeUp();
                } else {
                    if (addCustom(controller) == OK) {
                        custom->m_waker.wakeUp();
                    }
                }
            } else {
                addCustom(controller);
            }
        }
        break;
    }
    default:
        break;
    }
}

}

#endif // USE_IOURING
