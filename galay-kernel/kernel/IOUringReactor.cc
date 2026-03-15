#include "IOUringReactor.h"

#ifdef USE_IOURING

#include "kernel/Awaitable.h"

#include <sys/eventfd.h>
#include <sys/poll.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <expected>
#include <memory>
#include <stdexcept>

namespace galay::kernel {

namespace {

constexpr int kImmediateReady = 1;

inline auto wakeToken() -> void* {
    return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
}

inline int waitForIoUringCompletion(struct io_uring* ring,
                                    struct io_uring_cqe** cqe,
                                    struct __kernel_timespec* timeout) {
    if (io_uring_sq_ready(ring) == 0) {
        return io_uring_wait_cqe_timeout(ring, cqe, timeout);
    }

    const int ret = io_uring_submit_and_wait_timeout(ring, cqe, 1, timeout, nullptr);
    if (ret == -EBUSY) {
        return io_uring_wait_cqe_timeout(ring, cqe, timeout);
    }
    return ret;
}

inline auto negativeRetOrErrno(int ret) -> uint32_t {
    return (ret < 0 && ret != -1)
        ? static_cast<uint32_t>(-ret)
        : static_cast<uint32_t>(errno);
}

}  // namespace

IOUringReactor::IOUringReactor(int queue_depth, std::atomic<uint64_t>& last_error_code)
    : m_queue_depth(queue_depth)
    , m_event_fd(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC))
    , m_last_error_code(last_error_code) {
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
}

IOUringReactor::~IOUringReactor() {
    io_uring_queue_exit(&m_ring);
    if (m_event_fd != -1) {
        close(m_event_fd);
    }
}

void IOUringReactor::notify() {
    uint64_t val = 1;
    if (write(m_event_fd, &val, sizeof(val)) < 0) {
        detail::storeBackendError(
            m_last_error_code, kNotReady, static_cast<uint32_t>(errno));
    }
}

int IOUringReactor::wakeReadFdForTest() const {
    return m_event_fd;
}

int IOUringReactor::addAccept(IOController* controller) {
    auto* awaitable = controller->getAwaitable<AcceptAwaitable>();
    if (awaitable == nullptr) return -1;
    auto* token = controller->makeSqeRequest(IOController::READ);
    if (token == nullptr) {
        return -ENOMEM;
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        delete token;
        return -EAGAIN;
    }

    io_uring_prep_accept(sqe,
                         controller->m_handle.fd,
                         awaitable->m_host->sockAddr(),
                         awaitable->m_host->addrLen(),
                         0);
    io_uring_sqe_set_data(sqe, token);
    return 0;
}

int IOUringReactor::addConnect(IOController* controller) {
    auto* awaitable = controller->getAwaitable<ConnectAwaitable>();
    if (awaitable == nullptr) return -1;
    auto* token = controller->makeSqeRequest(IOController::WRITE);
    if (token == nullptr) {
        return -ENOMEM;
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        delete token;
        return -EAGAIN;
    }

    io_uring_prep_connect(sqe,
                          controller->m_handle.fd,
                          awaitable->m_host.sockAddr(),
                          *awaitable->m_host.addrLen());
    io_uring_sqe_set_data(sqe, token);
    return 0;
}

int IOUringReactor::addRecv(IOController* controller) {
    auto* awaitable = controller->getAwaitable<RecvAwaitable>();
    if (awaitable == nullptr) return -1;
    auto* token = controller->makeSqeRequest(IOController::READ);
    if (token == nullptr) {
        return -ENOMEM;
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        delete token;
        return -EAGAIN;
    }

    io_uring_prep_recv(sqe,
                       controller->m_handle.fd,
                       awaitable->m_buffer,
                       awaitable->m_length,
                       0);
    io_uring_sqe_set_data(sqe, token);
    return 0;
}

int IOUringReactor::addSend(IOController* controller) {
    auto* awaitable = controller->getAwaitable<SendAwaitable>();
    if (awaitable == nullptr) return -1;
    auto* token = controller->makeSqeRequest(IOController::WRITE);
    if (token == nullptr) {
        return -ENOMEM;
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        delete token;
        return -EAGAIN;
    }

    io_uring_prep_send(sqe,
                       controller->m_handle.fd,
                       awaitable->m_buffer,
                       awaitable->m_length,
                       0);
    io_uring_sqe_set_data(sqe, token);
    return 0;
}

int IOUringReactor::addReadv(IOController* controller) {
    auto* awaitable = controller->getAwaitable<ReadvAwaitable>();
    if (awaitable == nullptr) return -1;
    auto* token = controller->makeSqeRequest(IOController::READ);
    if (token == nullptr) {
        return -ENOMEM;
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        delete token;
        return -EAGAIN;
    }

    if (awaitable->m_iovecs.size() == 1) {
        const auto& iov = awaitable->m_iovecs[0];
        io_uring_prep_recv(sqe,
                           controller->m_handle.fd,
                           iov.iov_base,
                           static_cast<unsigned>(iov.iov_len),
                           0);
    } else {
        io_uring_prep_recvmsg(sqe, controller->m_handle.fd, &awaitable->m_msg, 0);
    }
    io_uring_sqe_set_data(sqe, token);
    return 0;
}

int IOUringReactor::addWritev(IOController* controller) {
    auto* awaitable = controller->getAwaitable<WritevAwaitable>();
    if (awaitable == nullptr) return -1;
    auto* token = controller->makeSqeRequest(IOController::WRITE);
    if (token == nullptr) {
        return -ENOMEM;
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        delete token;
        return -EAGAIN;
    }

    if (awaitable->m_iovecs.size() == 1) {
        const auto& iov = awaitable->m_iovecs[0];
        io_uring_prep_send(sqe,
                           controller->m_handle.fd,
                           iov.iov_base,
                           static_cast<unsigned>(iov.iov_len),
                           0);
    } else {
        io_uring_prep_sendmsg(sqe, controller->m_handle.fd, &awaitable->m_msg, 0);
    }
    io_uring_sqe_set_data(sqe, token);
    return 0;
}

int IOUringReactor::addSendFile(IOController* controller) {
    auto* awaitable = controller->getAwaitable<SendFileAwaitable>();
    if (awaitable == nullptr) return -1;
    auto* token = controller->makeSqeRequest(IOController::WRITE);
    if (token == nullptr) {
        return -ENOMEM;
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        delete token;
        return -EAGAIN;
    }

    io_uring_prep_poll_add(sqe, controller->m_handle.fd, POLLOUT);
    io_uring_sqe_set_data(sqe, token);
    return 0;
}

int IOUringReactor::addClose(IOController* controller) {
    if (controller == nullptr || controller->m_handle == GHandle::invalid()) {
        return 0;
    }

    const int fd = controller->m_handle.fd;

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
    controller->invalidateSqeRequests();
    controller->m_handle = GHandle::invalid();
    return 0;
}

int IOUringReactor::addFileRead(IOController* controller) {
    auto* awaitable = controller->getAwaitable<FileReadAwaitable>();
    if (awaitable == nullptr) return -1;
    auto* token = controller->makeSqeRequest(IOController::READ);
    if (token == nullptr) {
        return -ENOMEM;
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        delete token;
        return -EAGAIN;
    }

    io_uring_prep_read(sqe,
                       controller->m_handle.fd,
                       awaitable->m_buffer,
                       awaitable->m_length,
                       awaitable->m_offset);
    io_uring_sqe_set_data(sqe, token);
    return 0;
}

int IOUringReactor::addFileWrite(IOController* controller) {
    auto* awaitable = controller->getAwaitable<FileWriteAwaitable>();
    if (awaitable == nullptr) return -1;
    auto* token = controller->makeSqeRequest(IOController::WRITE);
    if (token == nullptr) {
        return -ENOMEM;
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        delete token;
        return -EAGAIN;
    }

    io_uring_prep_write(sqe,
                        controller->m_handle.fd,
                        awaitable->m_buffer,
                        awaitable->m_length,
                        awaitable->m_offset);
    io_uring_sqe_set_data(sqe, token);
    return 0;
}

int IOUringReactor::addRecvFrom(IOController* controller) {
    auto* awaitable = controller->getAwaitable<RecvFromAwaitable>();
    if (awaitable == nullptr) return -1;
    auto* token = controller->makeSqeRequest(IOController::READ);
    if (token == nullptr) {
        return -ENOMEM;
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        delete token;
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
    io_uring_sqe_set_data(sqe, token);
    return 0;
}

int IOUringReactor::addSendTo(IOController* controller) {
    auto* awaitable = controller->getAwaitable<SendToAwaitable>();
    if (awaitable == nullptr) return -1;
    auto* token = controller->makeSqeRequest(IOController::WRITE);
    if (token == nullptr) {
        return -ENOMEM;
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        delete token;
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
    io_uring_sqe_set_data(sqe, token);
    return 0;
}

int IOUringReactor::addFileWatch(IOController* controller) {
    auto* awaitable = controller->getAwaitable<FileWatchAwaitable>();
    if (awaitable == nullptr) return -1;
    auto* token = controller->makeSqeRequest(IOController::READ);
    if (token == nullptr) {
        return -ENOMEM;
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        delete token;
        return -EAGAIN;
    }

    io_uring_prep_read(sqe,
                       controller->m_handle.fd,
                       awaitable->m_buffer,
                       awaitable->m_buffer_size,
                       0);
    io_uring_sqe_set_data(sqe, token);
    return 0;
}

int IOUringReactor::addSequence(IOController* controller) {
    auto* sequence = controller->getAwaitable<SequenceAwaitableBase>();
    if (sequence == nullptr) return -1;
    if (sequence->prepareForSubmit() == SequenceProgress::kCompleted) {
        return kImmediateReady;
    }
    auto* task = sequence->front();
    if (task == nullptr) {
        return kImmediateReady;
    }
    return submitSequenceSqe(sequence->resolveTaskEventType(*task), task->context, controller);
}

int IOUringReactor::submitSequenceSqe(IOEventType type,
                                      IOContextBase* ctx,
                                      IOController* controller) {
    auto* token = controller->makeSqeRequest(IOController::READ);
    if (token == nullptr) {
        return -ENOMEM;
    }

    auto* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        delete token;
        return -EAGAIN;
    }

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
        io_uring_prep_accept(sqe,
                             controller->m_handle.fd,
                             c->m_host->sockAddr(),
                             c->m_host->addrLen(),
                             0);
        break;
    }
    case CONNECT: {
        auto* c = static_cast<ConnectIOContext*>(ctx);
        io_uring_prep_connect(sqe,
                              controller->m_handle.fd,
                              c->m_host.sockAddr(),
                              *c->m_host.addrLen());
        break;
    }
    case READV: {
        auto* c = static_cast<ReadvIOContext*>(ctx);
        io_uring_prep_readv(sqe,
                            controller->m_handle.fd,
                            c->m_iovecs.data(),
                            static_cast<unsigned>(c->m_iovecs.size()),
                            0);
        break;
    }
    case WRITEV: {
        auto* c = static_cast<WritevIOContext*>(ctx);
        io_uring_prep_writev(sqe,
                             controller->m_handle.fd,
                             c->m_iovecs.data(),
                             static_cast<unsigned>(c->m_iovecs.size()),
                             0);
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
    case SENDFILE:
        io_uring_prep_poll_add(sqe, controller->m_handle.fd, POLLOUT);
        break;
    case FILEWATCH: {
        auto* c = static_cast<FileWatchIOContext*>(ctx);
        io_uring_prep_read(sqe, controller->m_handle.fd, c->m_buffer, c->m_buffer_size, 0);
        break;
    }
    default:
        delete token;
        return -1;
    }

    auto* sequence = controller->getAwaitable<SequenceAwaitableBase>();
    sequence->m_sqe_type = SEQUENCE;
    io_uring_sqe_set_data(sqe, token);
    return 0;
}

int IOUringReactor::remove(IOController* controller) {
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

void IOUringReactor::ensureWakeReadArmed() {
    if (m_wake_read_armed) {
        return;
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return;
    }

    io_uring_prep_read(sqe, m_event_fd, &m_eventfd_buf, sizeof(m_eventfd_buf), 0);
    io_uring_sqe_set_data(sqe, wakeToken());
    m_wake_read_armed = true;
}

void IOUringReactor::poll(uint64_t timeout_ns, WakeCoordinator& wake_coordinator) {
    ensureWakeReadArmed();

    struct io_uring_cqe* cqe = nullptr;
    struct __kernel_timespec timeout;
    timeout.tv_sec = static_cast<__kernel_time64_t>(timeout_ns / 1000000000ULL);
    timeout.tv_nsec = timeout_ns % 1000000000ULL;

    const int ret = waitForIoUringCompletion(&m_ring, &cqe, &timeout);
    if (ret < 0) {
        if (ret == -EINTR || ret == -ETIME) {
            return;
        }
        detail::storeBackendError(
            m_last_error_code, kNotReady, static_cast<uint32_t>(-ret));
        return;
    }

    unsigned head = 0;
    unsigned count = 0;
    bool wake_triggered = false;

    io_uring_for_each_cqe(&m_ring, head, cqe) {
        void* user_data = io_uring_cqe_get_data(cqe);
        if (user_data == wakeToken()) {
            wake_triggered = true;
            m_wake_read_armed = false;
        } else if (user_data != nullptr) {
            processCompletion(cqe);
        }
        ++count;
    }

    if (count > 0) {
        io_uring_cq_advance(&m_ring, count);
    }

    if (wake_triggered) {
        wake_coordinator.cancelPendingWake();
        ensureWakeReadArmed();
    }
}

void IOUringReactor::processCompletion(struct io_uring_cqe* cqe) {
    void* data = io_uring_cqe_get_data(cqe);
    if (!data) {
        return;
    }

    std::unique_ptr<SqeRequestToken> token(static_cast<SqeRequestToken*>(data));
    if (!token->state) {
        return;
    }
    if (token->state->generation.load(std::memory_order_acquire) != token->generation) {
        return;
    }

    auto* controller = token->state->owner.load(std::memory_order_acquire);
    if (!controller) {
        return;
    }

    const auto slot = static_cast<IOController::Index>(token->state->slot);
    auto* base = static_cast<AwaitableBase*>(controller->m_awaitable[slot]);
    if (!base) {
        return;
    }

    switch (base->m_sqe_type) {
    case ACCEPT: {
        auto* awaitable = static_cast<AcceptAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            const int ret = addAccept(controller);
            if (ret < 0) {
                awaitable->m_result =
                    std::unexpected(IOError(kAcceptFailed, negativeRetOrErrno(ret)));
                awaitable->m_waker.wakeUp();
            }
        }
        break;
    }
    case CONNECT: {
        auto* awaitable = static_cast<ConnectAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            const int ret = addConnect(controller);
            if (ret < 0) {
                awaitable->m_result =
                    std::unexpected(IOError(kConnectFailed, negativeRetOrErrno(ret)));
                awaitable->m_waker.wakeUp();
            }
        }
        break;
    }
    case RECV: {
        auto* awaitable = static_cast<RecvAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            const int ret = addRecv(controller);
            if (ret < 0) {
                awaitable->m_result =
                    std::unexpected(IOError(kRecvFailed, negativeRetOrErrno(ret)));
                awaitable->m_waker.wakeUp();
            }
        }
        break;
    }
    case SEND: {
        auto* awaitable = static_cast<SendAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            const int ret = addSend(controller);
            if (ret < 0) {
                awaitable->m_result =
                    std::unexpected(IOError(kSendFailed, negativeRetOrErrno(ret)));
                awaitable->m_waker.wakeUp();
            }
        }
        break;
    }
    case READV: {
        auto* awaitable = static_cast<ReadvAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            const int ret = addReadv(controller);
            if (ret < 0) {
                awaitable->m_result =
                    std::unexpected(IOError(kRecvFailed, negativeRetOrErrno(ret)));
                awaitable->m_waker.wakeUp();
            }
        }
        break;
    }
    case WRITEV: {
        auto* awaitable = static_cast<WritevAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            const int ret = addWritev(controller);
            if (ret < 0) {
                awaitable->m_result =
                    std::unexpected(IOError(kSendFailed, negativeRetOrErrno(ret)));
                awaitable->m_waker.wakeUp();
            }
        }
        break;
    }
    case FILEREAD: {
        auto* awaitable = static_cast<FileReadAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            const int ret = addFileRead(controller);
            if (ret < 0) {
                awaitable->m_result =
                    std::unexpected(IOError(kReadFailed, negativeRetOrErrno(ret)));
                awaitable->m_waker.wakeUp();
            }
        }
        break;
    }
    case FILEWRITE: {
        auto* awaitable = static_cast<FileWriteAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            const int ret = addFileWrite(controller);
            if (ret < 0) {
                awaitable->m_result =
                    std::unexpected(IOError(kWriteFailed, negativeRetOrErrno(ret)));
                awaitable->m_waker.wakeUp();
            }
        }
        break;
    }
    case RECVFROM: {
        auto* awaitable = static_cast<RecvFromAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            const int ret = addRecvFrom(controller);
            if (ret < 0) {
                awaitable->m_result =
                    std::unexpected(IOError(kRecvFailed, negativeRetOrErrno(ret)));
                awaitable->m_waker.wakeUp();
            }
        }
        break;
    }
    case SENDTO: {
        auto* awaitable = static_cast<SendToAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            const int ret = addSendTo(controller);
            if (ret < 0) {
                awaitable->m_result =
                    std::unexpected(IOError(kSendFailed, negativeRetOrErrno(ret)));
                awaitable->m_waker.wakeUp();
            }
        }
        break;
    }
    case FILEWATCH: {
        auto* awaitable = static_cast<FileWatchAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            const int ret = addFileWatch(controller);
            if (ret < 0) {
                awaitable->m_result =
                    std::unexpected(IOError(kReadFailed, negativeRetOrErrno(ret)));
                awaitable->m_waker.wakeUp();
            }
        }
        break;
    }
    case SENDFILE: {
        auto* awaitable = static_cast<SendFileAwaitable*>(base);
        if (awaitable->handleComplete(cqe, controller->m_handle)) {
            awaitable->m_waker.wakeUp();
        } else {
            const int ret = addSendFile(controller);
            if (ret < 0) {
                awaitable->m_result =
                    std::unexpected(IOError(kSendFailed, negativeRetOrErrno(ret)));
                awaitable->m_waker.wakeUp();
            }
        }
        break;
    }
    case SEQUENCE: {
        auto* sequence = static_cast<SequenceAwaitableBase*>(base);
        auto* task = sequence->front();
        if (task) {
            const auto progress = sequence->onActiveEvent(cqe, controller->m_handle);
            if (progress == SequenceProgress::kCompleted) {
                sequence->m_waker.wakeUp();
            } else {
                const int ret = addSequence(controller);
                if (ret == kImmediateReady) {
                    sequence->m_waker.wakeUp();
                } else if (ret < 0) {
                    detail::storeBackendError(
                        m_last_error_code, kNotReady, negativeRetOrErrno(ret));
                    sequence->m_waker.wakeUp();
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

}  // namespace galay::kernel

#endif  // USE_IOURING
