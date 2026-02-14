/**
 * @file Awaitable.h
 * @brief 异步IO可等待对象
 * @author galay-kernel
 * @version 2.0.0
 *
 * @details 三层继承结构：
 * - AwaitableBase: 基类（m_sqe_type, virtual ~）
 * - IOContextBase: 中间层（virtual handleComplete 纯虚函数）
 *   - XxxIOContext: IO参数 + result + handleComplete 实现
 *     - XxxAwaitable: m_controller + m_waker + await_* + TimeoutSupport
 * - CloseAwaitable: 直接继承 AwaitableBase（无IO参数，无handleComplete）
 * - CustomAwaitable: 中间层基类，持有 IOTask 队列，用户继承实现自定义组合IO
 *
 * 所有 Awaitable 都支持超时：
 * @code
 * auto result = co_await socket.recv(buffer, size).timeout(5s);
 * @endcode
 *
 * @note 这些类型由TcpSocket内部创建，用户通常不需要直接使用
 */

#ifndef GALAY_KERNEL_AWAITABLE_H
#define GALAY_KERNEL_AWAITABLE_H

#include "galay-kernel/common/Defn.hpp"
#include "galay-kernel/common/Error.h"
#include "galay-kernel/common/Bytes.h"
#include "galay-kernel/common/Host.hpp"
#include "Timeout.hpp"
#include "FileWatchDefs.hpp"
#include "Waker.h"
#include <coroutine>
#include <cstddef>
#include <expected>
#include <vector>
#include <sys/uio.h>

#ifdef USE_EPOLL
#include <libaio.h>
#endif

#include "IOHandlers.hpp"

#include "IOController.hpp"

namespace galay::kernel
{

// ==================== 第一层：AwaitableBase ====================

struct AwaitableBase {
#ifdef USE_IOURING
    IOEventType m_sqe_type = IOEventType::INVALID;
#endif
    virtual ~AwaitableBase() = default;
};

// ==================== 第二层：IOContextBase ====================

struct IOContextBase: public AwaitableBase {
#ifdef USE_IOURING
    virtual bool handleComplete(struct io_uring_cqe* cqe, GHandle handle) = 0;
#else
    virtual bool handleComplete(GHandle handle) = 0;
#endif

    // CustomAwaitable 调度时可由上下文动态指定下一次等待方向；
    // 返回 INVALID 表示沿用静态 task.type。
    virtual IOEventType type() const { return IOEventType::INVALID; }
};

// ==================== 第三层：IOContext + Awaitable ====================

// ---- Accept ----

struct AcceptIOContext: public IOContextBase {
    AcceptIOContext(Host* host)
        : m_host(host) {}

#ifdef USE_IOURING
    bool handleComplete(struct io_uring_cqe* cqe, GHandle handle) override;
#else
    bool handleComplete(GHandle handle) override;
#endif

    Host* m_host;
    std::expected<GHandle, IOError> m_result;
};

struct AcceptAwaitable: public AcceptIOContext, public TimeoutSupport<AcceptAwaitable> {
    AcceptAwaitable(IOController* controller, Host* host)
        : AcceptIOContext(host), m_controller(controller) {}

    bool await_ready() { return false; }
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<GHandle, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

// ---- Recv ----

struct RecvIOContext: public IOContextBase {
    RecvIOContext(char* buffer, size_t length)
        : m_buffer(buffer), m_length(length) {}

#ifdef USE_IOURING
    bool handleComplete(struct io_uring_cqe* cqe, GHandle handle) override;
#else
    bool handleComplete(GHandle handle) override;
#endif

    char* m_buffer;
    size_t m_length;
    std::expected<Bytes, IOError> m_result;
};

struct RecvAwaitable: public RecvIOContext, public TimeoutSupport<RecvAwaitable> {
    RecvAwaitable(IOController* controller, char* buffer, size_t length)
        : RecvIOContext(buffer, length), m_controller(controller) {}

    bool await_ready() { return false; }
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<Bytes, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

// ---- Send ----

struct SendIOContext: public IOContextBase {
    SendIOContext(const char* buffer, size_t length)
        : m_buffer(buffer), m_length(length) {}

#ifdef USE_IOURING
    bool handleComplete(struct io_uring_cqe* cqe, GHandle handle) override;
#else
    bool handleComplete(GHandle handle) override;
#endif

    const char* m_buffer;
    size_t m_length;
    std::expected<size_t, IOError> m_result;
};

struct SendAwaitable: public SendIOContext, public TimeoutSupport<SendAwaitable> {
    SendAwaitable(IOController* controller, const char* buffer, size_t length)
        : SendIOContext(buffer, length), m_controller(controller) {}

    bool await_ready() { return false; }
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<size_t, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

// ---- Readv ----

struct ReadvIOContext: public IOContextBase {
    ReadvIOContext(std::vector<struct iovec> iovecs)
        : m_iovecs(std::move(iovecs)) {}

#ifdef USE_IOURING
    bool handleComplete(struct io_uring_cqe* cqe, GHandle handle) override;
#else
    bool handleComplete(GHandle handle) override;
#endif

    std::vector<struct iovec> m_iovecs;
    std::expected<size_t, IOError> m_result;
};

struct ReadvAwaitable: public ReadvIOContext, public TimeoutSupport<ReadvAwaitable> {
    ReadvAwaitable(IOController* controller, std::vector<struct iovec> iovecs)
        : ReadvIOContext(std::move(iovecs)), m_controller(controller) {}

    bool await_ready() { return false; }
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<size_t, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

// ---- Writev ----

struct WritevIOContext: public IOContextBase {
    WritevIOContext(std::vector<struct iovec> iovecs)
        : m_iovecs(std::move(iovecs)) {}

#ifdef USE_IOURING
    bool handleComplete(struct io_uring_cqe* cqe, GHandle handle) override;
#else
    bool handleComplete(GHandle handle) override;
#endif

    std::vector<struct iovec> m_iovecs;
    std::expected<size_t, IOError> m_result;
};

struct WritevAwaitable: public WritevIOContext, public TimeoutSupport<WritevAwaitable> {
    WritevAwaitable(IOController* controller, std::vector<struct iovec> iovecs)
        : WritevIOContext(std::move(iovecs)), m_controller(controller) {}

    bool await_ready() { return false; }
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<size_t, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

// ---- Connect ----

struct ConnectIOContext: public IOContextBase {
    ConnectIOContext(const Host& host)
        : m_host(host) {}

#ifdef USE_IOURING
    bool handleComplete(struct io_uring_cqe* cqe, GHandle handle) override;
#else
    bool handleComplete(GHandle handle) override;
#endif

    Host m_host;
    std::expected<void, IOError> m_result;
};

struct ConnectAwaitable: public ConnectIOContext, public TimeoutSupport<ConnectAwaitable> {
    ConnectAwaitable(IOController* controller, const Host& host)
        : ConnectIOContext(host), m_controller(controller) {}

    bool await_ready() { return false; }
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<void, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

// ---- Close (直接继承 AwaitableBase，无 IOContext) ----

struct CloseAwaitable: public AwaitableBase, public TimeoutSupport<CloseAwaitable> {
    CloseAwaitable(IOController* controller)
        : m_controller(controller) {}

    bool await_ready() { return false; }
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<void, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
    std::expected<void, IOError> m_result;
};

// ---- RecvFrom ----

struct RecvFromIOContext: public IOContextBase {
    RecvFromIOContext(char* buffer, size_t length, Host* from)
        : m_buffer(buffer), m_length(length), m_from(from) {}

#ifdef USE_IOURING
    bool handleComplete(struct io_uring_cqe* cqe, GHandle handle) override;
#else
    bool handleComplete(GHandle handle) override;
#endif

    char* m_buffer;
    size_t m_length;
    Host* m_from;
    std::expected<Bytes, IOError> m_result;

#ifdef USE_IOURING
    struct msghdr m_msg;
    struct iovec m_iov;
    sockaddr_storage m_addr;
#endif
};

struct RecvFromAwaitable: public RecvFromIOContext, public TimeoutSupport<RecvFromAwaitable> {
    RecvFromAwaitable(IOController* controller, char* buffer, size_t length, Host* from)
        : RecvFromIOContext(buffer, length, from), m_controller(controller) {}

    bool await_ready() { return false; }
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<Bytes, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

// ---- SendTo ----

struct SendToIOContext: public IOContextBase {
    SendToIOContext(const char* buffer, size_t length, const Host& to)
        : m_buffer(buffer), m_length(length), m_to(to) {}

#ifdef USE_IOURING
    bool handleComplete(struct io_uring_cqe* cqe, GHandle handle) override;
#else
    bool handleComplete(GHandle handle) override;
#endif

    const char* m_buffer;
    size_t m_length;
    Host m_to;
    std::expected<size_t, IOError> m_result;

#ifdef USE_IOURING
    struct msghdr m_msg;
    struct iovec m_iov;
#endif
};

struct SendToAwaitable: public SendToIOContext, public TimeoutSupport<SendToAwaitable> {
    SendToAwaitable(IOController* controller, const char* buffer, size_t length, const Host& to)
        : SendToIOContext(buffer, length, to), m_controller(controller) {}

    bool await_ready() { return false; }
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<size_t, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

// ---- FileRead ----

struct FileReadIOContext: public IOContextBase {
#ifdef USE_EPOLL
    FileReadIOContext(char* buffer, size_t length, off_t offset,
                      int event_fd, io_context_t aio_ctx, size_t expect_count = 1)
        : m_buffer(buffer), m_length(length), m_offset(offset),
          m_event_fd(event_fd), m_aio_ctx(aio_ctx), m_expect_count(expect_count) {}
#else
    FileReadIOContext(char* buffer, size_t length, off_t offset)
        : m_buffer(buffer), m_length(length), m_offset(offset) {}
#endif

#ifdef USE_IOURING
    bool handleComplete(struct io_uring_cqe* cqe, GHandle handle) override;
#else
    bool handleComplete(GHandle handle) override;
#endif

    char* m_buffer;
    size_t m_length;
    off_t m_offset;
    std::expected<Bytes, IOError> m_result;

#ifdef USE_EPOLL
    int m_event_fd;
    io_context_t m_aio_ctx;
    size_t m_expect_count;
    size_t m_finished_count{0};
#endif
};

struct FileReadAwaitable: public FileReadIOContext, public TimeoutSupport<FileReadAwaitable> {
#ifdef USE_EPOLL
    FileReadAwaitable(IOController* controller,
                      char* buffer, size_t length, off_t offset,
                      int event_fd, io_context_t aio_ctx, size_t expect_count = 1)
        : FileReadIOContext(buffer, length, offset, event_fd, aio_ctx, expect_count),
          m_controller(controller) {}
#else
    FileReadAwaitable(IOController* controller,
                      char* buffer, size_t length, off_t offset)
        : FileReadIOContext(buffer, length, offset),
          m_controller(controller) {}
#endif

    bool await_ready() { return false; }
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<Bytes, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

// ---- FileWrite ----

struct FileWriteIOContext: public IOContextBase {
#ifdef USE_EPOLL
    FileWriteIOContext(const char* buffer, size_t length, off_t offset,
                       int event_fd, io_context_t aio_ctx, size_t expect_count = 1)
        : m_buffer(buffer), m_length(length), m_offset(offset),
          m_event_fd(event_fd), m_aio_ctx(aio_ctx), m_expect_count(expect_count) {}
#else
    FileWriteIOContext(const char* buffer, size_t length, off_t offset)
        : m_buffer(buffer), m_length(length), m_offset(offset) {}
#endif

#ifdef USE_IOURING
    bool handleComplete(struct io_uring_cqe* cqe, GHandle handle) override;
#else
    bool handleComplete(GHandle handle) override;
#endif

    const char* m_buffer;
    size_t m_length;
    off_t m_offset;
    std::expected<size_t, IOError> m_result;

#ifdef USE_EPOLL
    int m_event_fd;
    io_context_t m_aio_ctx;
    size_t m_expect_count;
    size_t m_finished_count{0};
#endif
};

struct FileWriteAwaitable: public FileWriteIOContext, public TimeoutSupport<FileWriteAwaitable> {
#ifdef USE_EPOLL
    FileWriteAwaitable(IOController* controller,
                       const char* buffer, size_t length, off_t offset,
                       int event_fd, io_context_t aio_ctx, size_t expect_count = 1)
        : FileWriteIOContext(buffer, length, offset, event_fd, aio_ctx, expect_count),
          m_controller(controller) {}
#else
    FileWriteAwaitable(IOController* controller,
                       const char* buffer, size_t length, off_t offset)
        : FileWriteIOContext(buffer, length, offset),
          m_controller(controller) {}
#endif

    bool await_ready() { return false; }
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<size_t, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

// ---- FileWatch ----

struct FileWatchIOContext: public IOContextBase {
#ifdef USE_KQUEUE
    FileWatchIOContext(char* buffer, size_t buffer_size, FileWatchEvent events)
        : m_buffer(buffer), m_buffer_size(buffer_size), m_events(events) {}
#else
    FileWatchIOContext(char* buffer, size_t buffer_size)
        : m_buffer(buffer), m_buffer_size(buffer_size) {}
#endif

#ifdef USE_IOURING
    bool handleComplete(struct io_uring_cqe* cqe, GHandle handle) override;
#else
    bool handleComplete(GHandle handle) override;
#endif

    char* m_buffer;
    size_t m_buffer_size;
#ifdef USE_KQUEUE
    FileWatchEvent m_events;
#endif
    std::expected<FileWatchResult, IOError> m_result;
};

struct FileWatchAwaitable: public FileWatchIOContext, public TimeoutSupport<FileWatchAwaitable> {
#ifdef USE_KQUEUE
    FileWatchAwaitable(IOController* controller,
                       char* buffer, size_t buffer_size,
                       FileWatchEvent events)
        : FileWatchIOContext(buffer, buffer_size, events),
          m_controller(controller) {}
#else
    FileWatchAwaitable(IOController* controller,
                       char* buffer, size_t buffer_size)
        : FileWatchIOContext(buffer, buffer_size),
          m_controller(controller) {}
#endif

    bool await_ready() { return false; }
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<FileWatchResult, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

// ---- SendFile ----

struct SendFileIOContext: public IOContextBase {
    SendFileIOContext(int file_fd, off_t offset, size_t count)
        : m_file_fd(file_fd), m_offset(offset), m_count(count) {}

#ifdef USE_IOURING
    bool handleComplete(struct io_uring_cqe* cqe, GHandle handle) override;
#else
    bool handleComplete(GHandle handle) override;
#endif

    int m_file_fd;
    off_t m_offset;
    size_t m_count;
    std::expected<size_t, IOError> m_result;
};

struct SendFileAwaitable: public SendFileIOContext, public TimeoutSupport<SendFileAwaitable> {
    SendFileAwaitable(IOController* controller, int file_fd, off_t offset, size_t count)
        : SendFileIOContext(file_fd, offset, count), m_controller(controller) {}

    bool await_ready() { return false; }
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<size_t, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

// ==================== CustomAwaitable（中间层基类） ====================

/**
 * @brief 自定义组合IO操作的基类
 *
 * @details 用户通过继承此类，在构造函数中用 addTask 填充 IO 队列，
 * 并实现 await_ready / await_resume 来定义返回值类型。
 *
 * 三层结构：
 *   AwaitableBase
 *     └── CustomAwaitable          // 中间层：IOTask队列 + await_suspend
 *           └── MyCustomAwaitable  // 用户层：填充队列 + await_resume
 *
 * @code
 * struct SendThenRecv : public CustomAwaitable {
 *     SendIOContext m_send;
 *     RecvIOContext m_recv;
 *
 *     SendThenRecv(IOController* ctrl, const char* data, size_t len, char* buf, size_t bufLen)
 *         : CustomAwaitable(ctrl), m_send(data, len), m_recv(buf, bufLen)
 *     {
 *         addTask(SEND, &m_send);
 *         addTask(RECV, &m_recv);
 *     }
 *
 *     bool await_ready() { return false; }
 *     std::expected<Bytes, IOError> await_resume() {
 *         onCompleted();
 *         return std::move(m_recv.m_result);
 *     }
 * };
 * @endcode
 */
struct CustomAwaitable: public AwaitableBase {
    struct IOTask {
        IOEventType type;
        IOContextBase* context;  // 非拥有指针，生命周期由子类管理
    };

    CustomAwaitable(IOController* controller)
        : m_controller(controller) {}

    /// 添加一个 IO 任务到队列（IOContext 生命周期由子类负责）
    void addTask(IOEventType type, IOContextBase* ctx) {
        m_tasks.push_back(IOTask{type, ctx});
    }

    IOTask* front() {
        if (m_cursor >= m_tasks.size()) return nullptr;
        return &m_tasks[m_cursor];
    }

    void popFront() {
        if (m_cursor < m_tasks.size()) ++m_cursor;
    }

    bool empty() const { return m_cursor >= m_tasks.size(); }

    IOEventType resolveTaskEventType(const IOTask& task) const {
        if (task.context == nullptr) {
            return task.type;
        }
        IOEventType desired = task.context->type();
        return desired == IOEventType::INVALID ? task.type : desired;
    }

    /// 子类在 await_resume 中调用，清理调度器注册
    void onCompleted() {
        m_controller->removeAwaitable(CUSTOM);
    }

    bool await_suspend(std::coroutine_handle<> handle);

    IOController* m_controller;
    Waker m_waker;
    std::vector<IOTask> m_tasks;
    size_t m_cursor = 0;
};


} // namespace galay::kernel

#include "Awaitable.inl"

#endif // GALAY_KERNEL_AWAITABLE_H
