#include "IOUringScheduler.h"
#include "Scheduler.h"
#include "Timeout.h"
#include "common/Defn.hpp"
#include "common/Error.h"
#include "common/Log.h"

#ifdef USE_IOURING

#include "Awaitable.h"
#include <sys/eventfd.h>
#include <sys/inotify.h>
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
    // 注意：不能使用 IORING_SETUP_SINGLE_ISSUER，因为协程的 await_suspend
    // 可能在用户线程中调用 io_uring_get_sqe()，而 io_uring_submit() 在事件循环线程中调用
    // SINGLE_ISSUER 要求所有操作必须在同一个线程，否则会返回 -EEXIST
    params.flags = IORING_SETUP_SQPOLL | IORING_SETUP_COOP_TASKRUN;
    params.sq_thread_idle = 1000;  // 内核线程空闲 1 秒后休眠

    if (io_uring_queue_init_params(m_queue_depth, &m_ring, &params) < 0) {
        // 降级：不使用 SQPOLL
        std::memset(&params, 0, sizeof(params));
        params.flags = IORING_SETUP_COOP_TASKRUN;
        if (io_uring_queue_init_params(m_queue_depth, &m_ring, &params) < 0) {
            // 最后降级：使用默认配置
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
    // 使用 eventfd 唤醒事件循环，避免多线程竞争 io_uring
    uint64_t val = 1;
    write(m_event_fd, &val, sizeof(val));
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
    // SQE 已准备，由事件循环统一调用 io_uring_submit() 批量提交
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
    // 延迟提交：由事件循环批量提交
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
    // 延迟提交：由事件循环批量提交
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
    // 延迟提交：由事件循环批量提交
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
    // 延迟提交：由事件循环批量提交
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
    // 延迟提交：由事件循环批量提交
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
    // 延迟提交：由事件循环批量提交
    return 0;
}

int IOUringScheduler::addRecvFrom(IOController* controller)
{
    RecvFromAwaitable* awaitable = static_cast<RecvFromAwaitable*>(controller->m_awaitable);

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    // 使用 awaitable 中的持久化结构体
    std::memset(&awaitable->m_msg, 0, sizeof(awaitable->m_msg));
    std::memset(&awaitable->m_addr, 0, sizeof(awaitable->m_addr));

    awaitable->m_iov.iov_base = awaitable->m_buffer;
    awaitable->m_iov.iov_len = awaitable->m_length;

    awaitable->m_msg.msg_iov = &awaitable->m_iov;
    awaitable->m_msg.msg_iovlen = 1;
    awaitable->m_msg.msg_name = &awaitable->m_addr;
    awaitable->m_msg.msg_namelen = sizeof(awaitable->m_addr);

    io_uring_prep_recvmsg(sqe, awaitable->m_handle.fd, &awaitable->m_msg, 0);
    io_uring_sqe_set_data(sqe, controller);
    // 延迟提交：由事件循环批量提交
    return 0;
}

int IOUringScheduler::addSendTo(IOController* controller)
{
    SendToAwaitable* awaitable = static_cast<SendToAwaitable*>(controller->m_awaitable);

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    // 使用 awaitable 中的持久化结构体
    std::memset(&awaitable->m_msg, 0, sizeof(awaitable->m_msg));

    awaitable->m_iov.iov_base = const_cast<char*>(awaitable->m_buffer);
    awaitable->m_iov.iov_len = awaitable->m_length;

    awaitable->m_msg.msg_iov = &awaitable->m_iov;
    awaitable->m_msg.msg_iovlen = 1;
    awaitable->m_msg.msg_name = const_cast<sockaddr*>(awaitable->m_to.sockAddr());
    awaitable->m_msg.msg_namelen = *awaitable->m_to.addrLen();

    io_uring_prep_sendmsg(sqe, awaitable->m_handle.fd, &awaitable->m_msg, 0);
    io_uring_sqe_set_data(sqe, controller);
    // 延迟提交：由事件循环批量提交
    return 0;
}

int IOUringScheduler::addFileWatch(IOController* controller)
{
    FileWatchAwaitable* awaitable = static_cast<FileWatchAwaitable*>(controller->m_awaitable);

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    // inotify fd 是可读的，当有事件时读取
    io_uring_prep_read(sqe, awaitable->m_inotify_fd,
                       awaitable->m_buffer, awaitable->m_buffer_size, 0);
    io_uring_sqe_set_data(sqe, controller);
    // 延迟提交：由事件循环批量提交
    return 0;
}

int IOUringScheduler::addTimer(int timer_fd, TimerController* timer_ctrl)
{
    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    if (timer_fd == -1) {
        // io_uring 原生超时：timer_ctrl 实际上是 IOController*
        IOController* controller = reinterpret_cast<IOController*>(timer_ctrl);
        // 使用独立的 timeout 操作
        io_uring_prep_timeout(sqe, &controller->m_timeout_ts, 0, 0);
        // 使用特殊标记：最低两位为 11 (3) 表示 IO 超时
        io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(controller) | 3));
    } else {
        // 传统 timerfd 方式（epoll 兼容）
        io_uring_prep_read(sqe, timer_fd, &timer_ctrl->m_generation, sizeof(uint64_t), 0);
        // 使用特殊标记：最低位为 1 表示 timerfd 事件
        io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(timer_ctrl) | 1));
    }
    // 延迟提交：由事件循环批量提交
    return 0;
}

int IOUringScheduler::addSleep(IOController* controller)
{
    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return -EAGAIN;
    }

    // 使用 io_uring 原生超时，无需 timerfd
    // controller->m_timeout_ts 需要在调用前设置好
    io_uring_prep_timeout(sqe, &controller->m_timeout_ts, 0, 0);
    io_uring_sqe_set_data(sqe, controller);
    // 延迟提交：由事件循环批量提交
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
    // 延迟提交：由事件循环批量提交
    return 0;
}

void IOUringScheduler::spawn(Coroutine coro)
{
    m_coro_queue.enqueue(std::move(coro));
    notify();
}

void IOUringScheduler::processPendingCoroutines()
{
    // 循环处理直到队列为空，因为resume可能会spawn新协程
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

    // 添加 eventfd 到 io_uring 监听（SQPOLL 会自动提交）
    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (sqe) {
        io_uring_prep_read(sqe, m_event_fd, &m_eventfd_buf, sizeof(m_eventfd_buf), 0);
        io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(-1)); // 特殊标记
    }

    while (m_running.load(std::memory_order_acquire)) {
        // 1. 处理待执行的协程
        processPendingCoroutines();

        // 2. 提交所有待处理的 SQE
        // 即使在 SQPOLL 模式下，也需要调用 io_uring_submit() 来更新 SQ tail 指针
        // 让内核线程能看到新的 SQE。io_uring_submit() 会自动检查 NEED_WAKEUP 标志
        // 并在需要时唤醒内核线程，不会产生不必要的系统调用
        int submitted = io_uring_submit(&m_ring);
        if (submitted < 0 && submitted != -EBUSY) {
            LogError("io_uring_submit failed: {}", submitted);
        }

        // 3. 等待完成事件（带超时）
        int ret = io_uring_wait_cqe_timeout(&m_ring, &cqe, &timeout);
        if (ret < 0) {
            if (ret == -EINTR || ret == -ETIME) {
                // 超时或被中断，继续循环检查协程队列
                continue;
            }
            LogError("io_uring_wait_cqe_timeout failed: {}", ret);
            break;
        }

        // 4. 批量收割所有完成事件
        unsigned head;
        unsigned count = 0;
        bool eventfd_triggered = false;

        io_uring_for_each_cqe(&m_ring, head, cqe) {
            void* user_data = io_uring_cqe_get_data(cqe);

            // 检查是否是 eventfd 事件
            if (user_data == reinterpret_cast<void*>(-1)) {
                // eventfd 被触发，数据已经被 io_uring 读取到 m_eventfd_buf
                // 标记需要重新提交监听
                eventfd_triggered = true;
            } else if (user_data != nullptr) {
                // 检查是否是定时器事件（最低位为1）
                uintptr_t ptr_val = reinterpret_cast<uintptr_t>(user_data);
                if (ptr_val & 1) [[unlikely]] {
                    if ((ptr_val & 3) == 3) {
                        // IO 超时事件（最低两位都是1）
                        IOController* controller = reinterpret_cast<IOController*>(ptr_val & ~3UL);
                        if (controller && !controller->m_timer_cancelled) {
                            // 检查 CQE 结果：-ETIME 表示超时触发
                            if (cqe->res == -ETIME && controller->tryTimeout()) {
                                // 超时触发，需要唤醒协程
                                // 从 awaitable 中获取 waker
                                if (controller->m_awaitable) {
                                    // 通用方式：所有 Awaitable 都有 m_waker 成员
                                    // 使用 IOEventType 来确定具体类型
                                    switch (controller->m_type) {
                                    case ACCEPT: {
                                        auto* aw = static_cast<AcceptAwaitable*>(controller->m_awaitable);
                                        aw->m_waker.wakeUp();
                                        break;
                                    }
                                    case CONNECT: {
                                        auto* aw = static_cast<ConnectAwaitable*>(controller->m_awaitable);
                                        aw->m_waker.wakeUp();
                                        break;
                                    }
                                    case RECV: {
                                        auto* aw = static_cast<RecvAwaitable*>(controller->m_awaitable);
                                        aw->m_waker.wakeUp();
                                        break;
                                    }
                                    case SEND: {
                                        auto* aw = static_cast<SendAwaitable*>(controller->m_awaitable);
                                        aw->m_waker.wakeUp();
                                        break;
                                    }
                                    case RECVFROM: {
                                        auto* aw = static_cast<RecvFromAwaitable*>(controller->m_awaitable);
                                        aw->m_waker.wakeUp();
                                        break;
                                    }
                                    case SENDTO: {
                                        auto* aw = static_cast<SendToAwaitable*>(controller->m_awaitable);
                                        aw->m_waker.wakeUp();
                                        break;
                                    }
                                    default:
                                        break;
                                    }
                                }
                            }
                            // 其他结果（如 -ECANCELED）表示超时被取消，不需要处理
                        }
                    } else {
                        // 传统 timerfd 事件（只有最低位是1）
                        TimerController* timer_ctrl = reinterpret_cast<TimerController*>(ptr_val & ~1UL);
                        if (timer_ctrl && !timer_ctrl->m_cancelled) {
                            // 尝试标记为超时（需检查 generation 防止过期定时器）
                            if (timer_ctrl->m_io_controller &&
                                timer_ctrl->m_generation == timer_ctrl->m_io_controller->m_generation &&
                                timer_ctrl->m_io_controller->tryTimeout()) {
                                // 成功标记超时，唤醒协程
                                if (timer_ctrl->m_waker) {
                                    timer_ctrl->m_waker->wakeUp();
                                }
                            }
                        }
                    }
                } else {
                    processCompletion(cqe);
                }
            }
            ++count;
        }

        if (count > 0) {
            io_uring_cq_advance(&m_ring, count);
        }

        // 5. 如果 eventfd 被触发，重新提交监听（SQPOLL 会自动提交）
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

    // 关键：尝试标记为完成状态，如果已被超时处理则跳过
    if (!controller->tryComplete()) {
        // 已被超时处理，忽略此 IO 事件
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
            // 从 msghdr 中提取源地址信息
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
        SendToAwaitable* awaitable = static_cast<SendToAwaitable*>(controller->m_awaitable);
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
        FileWatchAwaitable* awaitable = static_cast<FileWatchAwaitable*>(controller->m_awaitable);
        if (res > 0) {
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
        } else if (res == 0) {
            awaitable->m_result = std::unexpected(IOError(kReadFailed, 0));
        } else {
            awaitable->m_result = std::unexpected(IOError(kReadFailed, static_cast<uint32_t>(-res)));
        }
        awaitable->m_waker.wakeUp();
        break;
    }
    case SLEEP:
    {
        // io_uring timeout 完成，res == -ETIME 表示正常超时
        // 直接唤醒协程，SleepAwaitable::await_resume 无需返回值
        SleepAwaitable* awaitable = static_cast<SleepAwaitable*>(controller->m_awaitable);
        awaitable->m_waker.wakeUp();
        break;
    }
    default:
        break;
    }
}

}

#endif // USE_IOURING
