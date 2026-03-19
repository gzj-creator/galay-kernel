/**
 * @file Awaitable.h
 * @brief 异步IO可等待对象
 * @author galay-kernel
 * @version 3.2.0
 *
 * @details 三层继承结构：
 * - AwaitableBase: 基类（m_sqe_type, virtual ~）
 * - IOContextBase: 中间层（virtual handleComplete 纯虚函数）
 *   - XxxIOContext: IO参数 + result + handleComplete 实现
 *     - XxxAwaitable: m_controller + m_waker + await_* + TimeoutSupport
 * - CloseAwaitable: 直接继承 AwaitableBase（无IO参数，无handleComplete）
 * - SequenceAwaitable: 组合式序列 Awaitable，支持标准 IO 与本地解析步骤
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
#include "galay-kernel/common/Host.hpp"
#include "Timeout.hpp"
#include "FileWatchDefs.hpp"
#include "Waker.h"
#include <cerrno>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <span>
#include <array>
#include <vector>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <sys/socket.h>
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

    // SequenceAwaitable 调度时可由上下文动态指定下一次等待方向；
    // 返回 INVALID 表示沿用静态 task.type。
    virtual IOEventType type() const { return IOEventType::INVALID; }
};

struct AcceptIOContext;
struct RecvIOContext;
struct SendIOContext;
struct ReadvIOContext;
struct WritevIOContext;
struct ConnectIOContext;
struct FileReadIOContext;
struct FileWriteIOContext;
struct RecvFromIOContext;
struct SendToIOContext;
struct FileWatchIOContext;
struct SendFileIOContext;
struct SequenceAwaitableBase;

namespace detail {

int registerIOSchedulerEvent(Scheduler* scheduler,
                             IOEventType event,
                             IOController* controller) noexcept;
int registerIOSchedulerClose(Scheduler* scheduler,
                             IOController* controller) noexcept;

inline uint32_t normalizeAwaitableErrno(int ret) noexcept {
    return (ret < 0 && ret != -1)
        ? static_cast<uint32_t>(-ret)
        : static_cast<uint32_t>(errno);
}

template <typename ResultT>
inline bool finalizeAwaitableAddResult(int ret,
                                       IOErrorCode io_error,
                                       std::expected<ResultT, IOError>& result) {
    if (ret == 1) {
        return false;
    }
    if (ret < 0) {
        result = std::unexpected(IOError(io_error, normalizeAwaitableErrno(ret)));
        return false;
    }
    return true;
}

template <IOEventType Event, typename AwaitableT>
inline auto resumeIOAwaitable(AwaitableT& awaitable) -> decltype(std::move(awaitable.m_result)) {
    awaitable.m_controller->removeAwaitable(Event);
    return std::move(awaitable.m_result);
}

template <typename AwaitableT, IOEventType Event, IOErrorCode ErrorCode, typename Promise>
inline bool suspendRegisteredAwaitable(AwaitableT& awaitable, std::coroutine_handle<Promise> handle) {
    awaitable.m_waker = Waker(handle);
#ifdef USE_IOURING
    awaitable.m_sqe_type = Event;
#endif
    awaitable.m_controller->fillAwaitable(Event, &awaitable);
    auto* scheduler = awaitable.m_waker.getScheduler();
    if (scheduler == nullptr || scheduler->type() != kIOScheduler) {
        awaitable.m_result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
        return false;
    }
    const int ret = registerIOSchedulerEvent(scheduler, Event, awaitable.m_controller);
    return finalizeAwaitableAddResult(ret, ErrorCode, awaitable.m_result);
}

template <typename Promise>
inline bool suspendSequenceAwaitable(SequenceAwaitableBase& awaitable,
                                     std::coroutine_handle<Promise> handle);

template <typename ContextT>
constexpr IOEventType customAwaitableDefaultEvent() {
    using T = std::remove_cvref_t<ContextT>;
    if constexpr (std::is_base_of_v<AcceptIOContext, T>) {
        return ACCEPT;
    } else if constexpr (std::is_base_of_v<RecvIOContext, T>) {
        return RECV;
    } else if constexpr (std::is_base_of_v<SendIOContext, T>) {
        return SEND;
    } else if constexpr (std::is_base_of_v<ReadvIOContext, T>) {
        return READV;
    } else if constexpr (std::is_base_of_v<WritevIOContext, T>) {
        return WRITEV;
    } else if constexpr (std::is_base_of_v<ConnectIOContext, T>) {
        return CONNECT;
    } else if constexpr (std::is_base_of_v<FileReadIOContext, T>) {
        return FILEREAD;
    } else if constexpr (std::is_base_of_v<FileWriteIOContext, T>) {
        return FILEWRITE;
    } else if constexpr (std::is_base_of_v<RecvFromIOContext, T>) {
        return RECVFROM;
    } else if constexpr (std::is_base_of_v<SendToIOContext, T>) {
        return SENDTO;
    } else if constexpr (std::is_base_of_v<FileWatchIOContext, T>) {
        return FILEWATCH;
    } else if constexpr (std::is_base_of_v<SendFileIOContext, T>) {
        return SENDFILE;
    } else {
        return IOEventType::INVALID;
    }
}

template <typename T>
struct is_expected : std::false_type {};

template <typename T, typename E>
struct is_expected<std::expected<T, E>> : std::true_type {};

template <typename ResultT>
constexpr bool is_expected_v = is_expected<std::remove_cvref_t<ResultT>>::value;

template <typename ResultT>
struct expected_traits;

template <typename T, typename E>
struct expected_traits<std::expected<T, E>> {
    using value_type = T;
    using error_type = E;
};

}  // namespace detail

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
    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) {
        return detail::suspendRegisteredAwaitable<AcceptAwaitable, ACCEPT, kAcceptFailed>(
            *this, handle);
    }
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
    std::expected<size_t, IOError> m_result;
};

struct RecvAwaitable: public RecvIOContext, public TimeoutSupport<RecvAwaitable> {
    RecvAwaitable(IOController* controller, char* buffer, size_t length)
        : RecvIOContext(buffer, length), m_controller(controller) {}

    bool await_ready() { return false; }
    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) {
        return detail::suspendRegisteredAwaitable<RecvAwaitable, RECV, kRecvFailed>(
            *this, handle);
    }
    std::expected<size_t, IOError> await_resume();

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
    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) {
        return detail::suspendRegisteredAwaitable<SendAwaitable, SEND, kSendFailed>(
            *this, handle);
    }
    std::expected<size_t, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

// ---- Readv ----

struct ReadvIOContext: public IOContextBase {
    explicit ReadvIOContext(std::span<const struct iovec> iovecs)
        : m_iovecs(iovecs) {
#ifdef USE_IOURING
        initMsghdr();
#endif
    }

    template<size_t N>
    ReadvIOContext(std::array<struct iovec, N>& iovecs, size_t count)
        : m_iovecs(iovecs.data(), validateBorrowedCountOrAbort(count, N, "readv")) {
#ifdef USE_IOURING
        initMsghdr();
#endif
    }

    template<size_t N>
    ReadvIOContext(struct iovec (&iovecs)[N], size_t count)
        : m_iovecs(iovecs, validateBorrowedCountOrAbort(count, N, "readv")) {
#ifdef USE_IOURING
        initMsghdr();
#endif
    }

#ifdef USE_IOURING
    bool handleComplete(struct io_uring_cqe* cqe, GHandle handle) override;
#else
    bool handleComplete(GHandle handle) override;
#endif

    static size_t validateBorrowedCountOrAbort(size_t count, size_t capacity, const char* op) {
        if (count <= capacity) {
            return count;
        }
        std::fprintf(stderr,
                     "invalid borrowed %s count: %zu > %zu\n",
                     op,
                     count,
                     capacity);
        std::abort();
    }

    std::span<const struct iovec> m_iovecs;
    std::expected<size_t, IOError> m_result;

#ifdef USE_IOURING
    void initMsghdr() {
        m_msg.msg_iov = const_cast<struct iovec*>(m_iovecs.data());
        m_msg.msg_iovlen = m_iovecs.size();
    }

    struct msghdr m_msg{};
#endif
};

struct ReadvAwaitable: public ReadvIOContext, public TimeoutSupport<ReadvAwaitable> {
    template<size_t N>
    ReadvAwaitable(IOController* controller, std::array<struct iovec, N>& iovecs, size_t count)
        : ReadvIOContext(iovecs, count), m_controller(controller) {}

    template<size_t N>
    ReadvAwaitable(IOController* controller, struct iovec (&iovecs)[N], size_t count)
        : ReadvIOContext(iovecs, count), m_controller(controller) {}

    bool await_ready() { return false; }
    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) {
        return detail::suspendRegisteredAwaitable<ReadvAwaitable, READV, kRecvFailed>(
            *this, handle);
    }
    std::expected<size_t, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

// ---- Writev ----

struct WritevIOContext: public IOContextBase {
    explicit WritevIOContext(std::span<const struct iovec> iovecs)
        : m_iovecs(iovecs) {
#ifdef USE_IOURING
        initMsghdr();
#endif
    }

    template<size_t N>
    WritevIOContext(std::array<struct iovec, N>& iovecs, size_t count)
        : m_iovecs(iovecs.data(), validateBorrowedCountOrAbort(count, N, "writev")) {
#ifdef USE_IOURING
        initMsghdr();
#endif
    }

    template<size_t N>
    WritevIOContext(struct iovec (&iovecs)[N], size_t count)
        : m_iovecs(iovecs, validateBorrowedCountOrAbort(count, N, "writev")) {
#ifdef USE_IOURING
        initMsghdr();
#endif
    }

#ifdef USE_IOURING
    bool handleComplete(struct io_uring_cqe* cqe, GHandle handle) override;
#else
    bool handleComplete(GHandle handle) override;
#endif

    static size_t validateBorrowedCountOrAbort(size_t count, size_t capacity, const char* op) {
        if (count <= capacity) {
            return count;
        }
        std::fprintf(stderr,
                     "invalid borrowed %s count: %zu > %zu\n",
                     op,
                     count,
                     capacity);
        std::abort();
    }

    std::span<const struct iovec> m_iovecs;
    std::expected<size_t, IOError> m_result;

#ifdef USE_IOURING
    void initMsghdr() {
        m_msg.msg_iov = const_cast<struct iovec*>(m_iovecs.data());
        m_msg.msg_iovlen = m_iovecs.size();
    }

    struct msghdr m_msg{};
#endif
};

struct WritevAwaitable: public WritevIOContext, public TimeoutSupport<WritevAwaitable> {
    template<size_t N>
    WritevAwaitable(IOController* controller, std::array<struct iovec, N>& iovecs, size_t count)
        : WritevIOContext(iovecs, count), m_controller(controller) {}

    template<size_t N>
    WritevAwaitable(IOController* controller, struct iovec (&iovecs)[N], size_t count)
        : WritevIOContext(iovecs, count), m_controller(controller) {}

    bool await_ready() { return false; }
    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) {
        return detail::suspendRegisteredAwaitable<WritevAwaitable, WRITEV, kSendFailed>(
            *this, handle);
    }
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
    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) {
        return detail::suspendRegisteredAwaitable<ConnectAwaitable, CONNECT, kConnectFailed>(
            *this, handle);
    }
    std::expected<void, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

// ---- Close (直接继承 AwaitableBase，无 IOContext) ----

struct CloseAwaitable: public AwaitableBase, public TimeoutSupport<CloseAwaitable> {
    CloseAwaitable(IOController* controller)
        : m_controller(controller) {}

    bool await_ready() { return false; }
    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) {
        m_waker = Waker(handle);
        auto scheduler = m_waker.getScheduler();
        if (scheduler == nullptr || scheduler->type() != kIOScheduler) {
            m_result = std::unexpected(IOError(kNotRunningOnIOScheduler, errno));
            return false;
        }
        int res = detail::registerIOSchedulerClose(scheduler, m_controller);
        if (res == 0) {
            m_result = {};
            return false;
        }
        m_result = std::unexpected(IOError(kDisconnectError, detail::normalizeAwaitableErrno(res)));
        return false;
    }
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
    std::expected<size_t, IOError> m_result;

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
    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) {
        return detail::suspendRegisteredAwaitable<RecvFromAwaitable, RECVFROM, kRecvFailed>(
            *this, handle);
    }
    std::expected<size_t, IOError> await_resume();

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
    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) {
        return detail::suspendRegisteredAwaitable<SendToAwaitable, SENDTO, kSendFailed>(
            *this, handle);
    }
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
    std::expected<size_t, IOError> m_result;

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
    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) {
        return detail::suspendRegisteredAwaitable<FileReadAwaitable, FILEREAD, kReadFailed>(
            *this, handle);
    }
    std::expected<size_t, IOError> await_resume();

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
    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) {
        return detail::suspendRegisteredAwaitable<FileWriteAwaitable, FILEWRITE, kWriteFailed>(
            *this, handle);
    }
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
    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) {
        return detail::suspendRegisteredAwaitable<FileWatchAwaitable, FILEWATCH, kReadFailed>(
            *this, handle);
    }
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
    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) {
        return detail::suspendRegisteredAwaitable<SendFileAwaitable, SENDFILE, kSendFailed>(
            *this, handle);
    }
    std::expected<size_t, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

enum class SequenceProgress {
    kNeedWait,
    kCompleted,
};

enum class ParseStatus {
    kNeedMore,
    kContinue,
    kCompleted,
};

enum class MachineSignal {
    kContinue,
    kWaitRead,
    kWaitReadv,
    kWaitWrite,
    kWaitWritev,
    kWaitConnect,
    kComplete,
    kFail,
};

template <typename ResultT>
struct MachineAction {
    MachineSignal signal = MachineSignal::kContinue;
    char* read_buffer = nullptr;
    size_t read_length = 0;
    const struct iovec* iovecs = nullptr;
    size_t iov_count = 0;
    const char* write_buffer = nullptr;
    size_t write_length = 0;
    Host connect_host{};
    std::optional<ResultT> result;
    std::optional<IOError> error;

    static MachineAction continue_() {
        return MachineAction{};
    }

    static MachineAction waitRead(char* buffer, size_t length) {
        MachineAction action;
        action.signal = MachineSignal::kWaitRead;
        action.read_buffer = buffer;
        action.read_length = length;
        return action;
    }

    static MachineAction waitWrite(const char* buffer, size_t length) {
        MachineAction action;
        action.signal = MachineSignal::kWaitWrite;
        action.write_buffer = buffer;
        action.write_length = length;
        return action;
    }

    static MachineAction waitReadv(const struct iovec* iovecs, size_t count) {
        MachineAction action;
        action.signal = MachineSignal::kWaitReadv;
        action.iovecs = iovecs;
        action.iov_count = count;
        return action;
    }

    static MachineAction waitWritev(const struct iovec* iovecs, size_t count) {
        MachineAction action;
        action.signal = MachineSignal::kWaitWritev;
        action.iovecs = iovecs;
        action.iov_count = count;
        return action;
    }

    static MachineAction waitConnect(const Host& host) {
        MachineAction action;
        action.signal = MachineSignal::kWaitConnect;
        action.connect_host = host;
        return action;
    }

    static MachineAction complete(ResultT value) {
        MachineAction action;
        action.signal = MachineSignal::kComplete;
        action.result = std::move(value);
        return action;
    }

    static MachineAction fail(IOError io_error) {
        MachineAction action;
        action.signal = MachineSignal::kFail;
        action.error = std::move(io_error);
        return action;
    }
};

template <typename MachineT>
concept AwaitableStateMachine =
    requires(MachineT& machine, std::expected<size_t, IOError> io_result) {
        typename MachineT::result_type;
        { machine.advance() } -> std::same_as<MachineAction<typename MachineT::result_type>>;
        { machine.onRead(std::move(io_result)) } -> std::same_as<void>;
        { machine.onWrite(std::move(io_result)) } -> std::same_as<void>;
    };

struct SequenceAwaitableBase: public AwaitableBase {
    struct IOTask {
        IOEventType type;
        void* task = nullptr;
        IOContextBase* context = nullptr;
    };

    explicit SequenceAwaitableBase(IOController* controller)
        : m_controller(controller) {}

    virtual IOTask* front() = 0;
    virtual const IOTask* front() const = 0;
    virtual void popFront() = 0;
    virtual bool empty() const = 0;

    IOEventType resolveTaskEventType(const IOTask& task) const {
        if (task.context == nullptr) {
            return task.type;
        }
        IOEventType desired = task.context->type();
        return desired == IOEventType::INVALID ? task.type : desired;
    }

    void onCompleted() {
        m_controller->removeAwaitable(SEQUENCE);
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) {
        return detail::suspendSequenceAwaitable(*this, handle);
    }

#ifdef USE_IOURING
    virtual SequenceProgress prepareForSubmit() = 0;
    virtual SequenceProgress onActiveEvent(struct io_uring_cqe* cqe, GHandle handle) = 0;
#else
    virtual SequenceProgress prepareForSubmit(GHandle handle) = 0;
    virtual SequenceProgress onActiveEvent(GHandle handle) = 0;
#endif

    std::optional<IOError> m_error;
    IOController* m_controller;
    Waker m_waker;
};

namespace detail {

template <typename Promise>
inline bool suspendSequenceAwaitable(SequenceAwaitableBase& awaitable,
                                     std::coroutine_handle<Promise> handle) {
    awaitable.m_waker = Waker(handle);
#ifdef USE_IOURING
    awaitable.m_sqe_type = SEQUENCE;
#endif
    awaitable.m_controller->fillAwaitable(SEQUENCE, &awaitable);
    auto* scheduler = awaitable.m_waker.getScheduler();
    if (scheduler == nullptr || scheduler->type() != kIOScheduler) {
        return false;
    }
    const int ret = registerIOSchedulerEvent(scheduler, SEQUENCE, awaitable.m_controller);
    if (ret == 1) {
        return false;
    }
    if (ret < 0) {
        awaitable.m_error = IOError(kNotReady, normalizeAwaitableErrno(ret));
        return false;
    }
    return true;
}

}  // namespace detail

template <typename ResultT, size_t InlineN = 4>
class SequenceAwaitable;

template <typename ResultT, size_t InlineN = 4>
class SequenceOps {
public:
    explicit SequenceOps(SequenceAwaitable<ResultT, InlineN>& owner)
        : m_owner(owner) {}

    template <typename StepT>
    StepT& queue(StepT& step) {
        m_owner.queue(step);
        return step;
    }

    template <typename... StepTs>
    void queueMany(StepTs&... steps) {
        (queue(steps), ...);
    }

    void clear() {
        m_owner.clear();
    }

    template <typename ValueT>
    void complete(ValueT&& value) {
        m_owner.complete(std::forward<ValueT>(value));
    }

private:
    SequenceAwaitable<ResultT, InlineN>& m_owner;
};

template <typename ResultT, size_t InlineN>
class SequenceAwaitable : public SequenceAwaitableBase {
public:
    struct TaskBase {
        virtual ~TaskBase() = default;
        virtual IOContextBase* contextBase() = 0;
        virtual IOEventType defaultEventType() const = 0;
        virtual void beforeSubmit() {}
        virtual bool isLocal() const = 0;
#ifdef USE_IOURING
        virtual bool onEvent(SequenceAwaitable& owner, struct io_uring_cqe* cqe, GHandle handle) = 0;
#else
        virtual bool onReady(SequenceAwaitable& owner, GHandle handle) = 0;
        virtual bool onEvent(SequenceAwaitable& owner, GHandle handle) = 0;
#endif
    };

    explicit SequenceAwaitable(IOController* controller)
        : SequenceAwaitableBase(controller) {}

    bool await_ready() {
        return m_result_set;
    }

    auto await_resume() -> ResultT {
        onCompleted();
        if (m_result_set) {
            return std::move(*m_result);
        }
        if (m_error.has_value()) {
            if constexpr (detail::is_expected_v<ResultT>) {
                using ErrorT = typename detail::expected_traits<ResultT>::error_type;
                if constexpr (std::is_constructible_v<ErrorT, IOError>) {
                    return std::unexpected(ErrorT(*m_error));
                }
            }
        }
        if constexpr (detail::is_expected_v<ResultT>) {
            using ErrorT = typename detail::expected_traits<ResultT>::error_type;
            if constexpr (std::is_constructible_v<ErrorT, IOError>) {
                return std::unexpected(ErrorT(IOError(kNotReady, errno)));
            }
        }
        std::abort();
    }

    template <typename StepT>
    StepT& queue(StepT& step) {
        static_assert(std::is_base_of_v<TaskBase, std::remove_cvref_t<StepT>>,
                      "SequenceAwaitable::queue requires a Sequence task");
        emplaceTask(step.defaultEventType(), step.contextBase(), &step);
        return step;
    }

    TaskBase& queue(TaskBase& task) {
        emplaceTask(task.defaultEventType(), task.contextBase(), &task);
        return task;
    }

    template <typename StepT>
    StepT& queue(IOEventType type, StepT& step) {
        static_assert(std::is_base_of_v<TaskBase, std::remove_cvref_t<StepT>>,
                      "SequenceAwaitable::queue requires a Sequence task");
        emplaceTask(type, step.contextBase(), &step);
        return step;
    }

    TaskBase& queue(IOEventType type, TaskBase& task) {
        emplaceTask(type, task.contextBase(), &task);
        return task;
    }

    template <typename... StepTs>
    void queueMany(StepTs&... steps) {
        (queue(steps), ...);
    }

    void clear() {
        m_head = 0;
        m_size = 0;
    }

    template <typename ValueT>
    void complete(ValueT&& value) {
        m_result = std::forward<ValueT>(value);
        m_result_set = true;
        clear();
    }

    void fail(IOError error) {
        m_error = std::move(error);
        clear();
    }

    bool hasResultValue() const {
        return m_result_set && m_result.has_value();
    }

    bool hasFailure() const {
        return m_error.has_value();
    }

    std::optional<ResultT> takeResultValue() {
        m_result_set = false;
        auto result = std::move(m_result);
        m_result.reset();
        return result;
    }

    std::optional<IOError> takeFailure() {
        auto error = std::move(m_error);
        m_error.reset();
        return error;
    }

    void resetOutcomeForReuse() {
        m_result.reset();
        m_result_set = false;
        m_error.reset();
        clear();
    }

    SequenceOps<ResultT, InlineN> ops() {
        return SequenceOps<ResultT, InlineN>(*this);
    }

    IOTask* front() override {
        if (m_size == 0) {
            return nullptr;
        }
        return &m_tasks[m_head];
    }

    const IOTask* front() const override {
        if (m_size == 0) {
            return nullptr;
        }
        return &m_tasks[m_head];
    }

    void popFront() override {
        if (m_size == 0) {
            return;
        }
        m_head = (m_head + 1) % InlineN;
        --m_size;
    }

    bool empty() const override {
        return m_size == 0;
    }

#ifdef USE_IOURING
    SequenceProgress prepareForSubmit() override {
        while (auto* entry = front()) {
            auto* task = static_cast<TaskBase*>(entry->task);
            if (!task) {
                popFront();
                continue;
            }
            if (task->isLocal()) {
                task->onEvent(*this, nullptr, m_controller->m_handle);
                consumeFrontIfSame(task);
                if (m_result_set) {
                    return SequenceProgress::kCompleted;
                }
                continue;
            }
            task->beforeSubmit();
            entry->context = task->contextBase();
            return SequenceProgress::kNeedWait;
        }
        return SequenceProgress::kCompleted;
    }

    SequenceProgress onActiveEvent(struct io_uring_cqe* cqe, GHandle handle) override {
        auto* entry = front();
        if (!entry) {
            return SequenceProgress::kCompleted;
        }
        auto* task = static_cast<TaskBase*>(entry->task);
        if (!task) {
            popFront();
            return prepareForSubmit();
        }
        if (task->onEvent(*this, cqe, handle)) {
            consumeFrontIfSame(task);
        }
        if (m_result_set) {
            return SequenceProgress::kCompleted;
        }
        return prepareForSubmit();
    }
#else
    SequenceProgress prepareForSubmit(GHandle handle) override {
        while (auto* entry = front()) {
            auto* task = static_cast<TaskBase*>(entry->task);
            if (!task) {
                popFront();
                continue;
            }
            task->beforeSubmit();
            entry->context = task->contextBase();
            if (task->onReady(*this, handle)) {
                consumeFrontIfSame(task);
                if (m_result_set) {
                    return SequenceProgress::kCompleted;
                }
                continue;
            }
            return SequenceProgress::kNeedWait;
        }
        return SequenceProgress::kCompleted;
    }

    SequenceProgress onActiveEvent(GHandle handle) override {
        auto* entry = front();
        if (!entry) {
            return SequenceProgress::kCompleted;
        }
        auto* task = static_cast<TaskBase*>(entry->task);
        if (!task) {
            popFront();
            return prepareForSubmit(handle);
        }
        if (task->onEvent(*this, handle)) {
            consumeFrontIfSame(task);
        }
        if (m_result_set) {
            return SequenceProgress::kCompleted;
        }
        return prepareForSubmit(handle);
    }
#endif

private:
    void emplaceTask(IOEventType type, IOContextBase* context, TaskBase* task) {
        if (m_size >= InlineN) {
            std::abort();
        }
        const size_t index = (m_head + m_size) % InlineN;
        m_tasks[index] = IOTask{type, task, context};
        ++m_size;
    }

    void consumeFrontIfSame(TaskBase* task) {
        auto* entry = front();
        if (entry && entry->task == task) {
            popFront();
        }
    }

    std::array<IOTask, InlineN> m_tasks{};
    size_t m_head = 0;
    size_t m_size = 0;
    std::optional<ResultT> m_result;
    bool m_result_set = false;
};

template <AwaitableStateMachine MachineT>
class StateMachineAwaitable : public SequenceAwaitableBase {
public:
    using result_type = typename MachineT::result_type;

    StateMachineAwaitable(IOController* controller, MachineT machine)
        : SequenceAwaitableBase(controller)
        , m_machine(std::move(machine))
        , m_recv_context(nullptr, 0)
        , m_readv_context(std::span<const struct iovec>{})
        , m_send_context(nullptr, 0)
        , m_writev_context(std::span<const struct iovec>{})
        , m_connect_context(Host{}) {}

    bool await_ready() {
        return m_result_set;
    }

    auto await_resume() -> result_type {
        onCompleted();
        if (m_result_set) {
            return std::move(*m_result);
        }
        if (m_error.has_value()) {
            if constexpr (detail::is_expected_v<result_type>) {
                using ErrorT = typename detail::expected_traits<result_type>::error_type;
                if constexpr (std::is_constructible_v<ErrorT, IOError>) {
                    return std::unexpected(ErrorT(*m_error));
                }
            }
        }
        if constexpr (detail::is_expected_v<result_type>) {
            using ErrorT = typename detail::expected_traits<result_type>::error_type;
            if constexpr (std::is_constructible_v<ErrorT, IOError>) {
                return std::unexpected(ErrorT(IOError(kNotReady, errno)));
            }
        }
        std::abort();
    }

    IOTask* front() override {
        return m_has_active_task ? &m_active_task : nullptr;
    }

    const IOTask* front() const override {
        return m_has_active_task ? &m_active_task : nullptr;
    }

    void popFront() override {
        clearActiveTask();
    }

    bool empty() const override {
        return !m_has_active_task;
    }

#ifdef USE_IOURING
    SequenceProgress prepareForSubmit() override {
        return pump();
    }

    SequenceProgress onActiveEvent(struct io_uring_cqe* cqe, GHandle handle) override {
        if (!m_has_active_task) {
            return pump();
        }
        if (m_active_kind == ActiveKind::kRead) {
            if (!m_recv_context.handleComplete(cqe, handle)) {
                return SequenceProgress::kNeedWait;
            }
            auto io_result = std::move(m_recv_context.m_result);
            clearActiveTask();
            m_machine.onRead(std::move(io_result));
            return pump();
        }
        if (m_active_kind == ActiveKind::kReadv) {
            if (!m_readv_context.handleComplete(cqe, handle)) {
                return SequenceProgress::kNeedWait;
            }
            auto io_result = std::move(m_readv_context.m_result);
            clearActiveTask();
            m_machine.onRead(std::move(io_result));
            return pump();
        }
        if (m_active_kind == ActiveKind::kWrite) {
            if (!m_send_context.handleComplete(cqe, handle)) {
                return SequenceProgress::kNeedWait;
            }
            auto io_result = std::move(m_send_context.m_result);
            clearActiveTask();
            m_machine.onWrite(std::move(io_result));
            return pump();
        }
        if (m_active_kind == ActiveKind::kWritev) {
            if (!m_writev_context.handleComplete(cqe, handle)) {
                return SequenceProgress::kNeedWait;
            }
            auto io_result = std::move(m_writev_context.m_result);
            clearActiveTask();
            m_machine.onWrite(std::move(io_result));
            return pump();
        }
        if (m_active_kind == ActiveKind::kConnect) {
            if (!m_connect_context.handleComplete(cqe, handle)) {
                return SequenceProgress::kNeedWait;
            }
            auto io_result = std::move(m_connect_context.m_result);
            clearActiveTask();
            deliverConnect(std::move(io_result));
            return pump();
        }
        m_error = IOError(kParamInvalid, 0);
        return SequenceProgress::kCompleted;
    }
#else
    SequenceProgress prepareForSubmit(GHandle handle) override {
        for (size_t i = 0; i < kInlineTransitionCap; ++i) {
            const SequenceProgress progress = pump();
            if (progress == SequenceProgress::kCompleted) {
                return progress;
            }
            if (!m_has_active_task) {
                return SequenceProgress::kCompleted;
            }
            if (m_active_kind == ActiveKind::kRead) {
                if (!m_recv_context.handleComplete(handle)) {
                    return SequenceProgress::kNeedWait;
                }
                auto io_result = std::move(m_recv_context.m_result);
                clearActiveTask();
                m_machine.onRead(std::move(io_result));
                continue;
            }
            if (m_active_kind == ActiveKind::kReadv) {
                if (!m_readv_context.handleComplete(handle)) {
                    return SequenceProgress::kNeedWait;
                }
                auto io_result = std::move(m_readv_context.m_result);
                clearActiveTask();
                m_machine.onRead(std::move(io_result));
                continue;
            }
            if (m_active_kind == ActiveKind::kWrite) {
                if (!m_send_context.handleComplete(handle)) {
                    return SequenceProgress::kNeedWait;
                }
                auto io_result = std::move(m_send_context.m_result);
                clearActiveTask();
                m_machine.onWrite(std::move(io_result));
                continue;
            }
            if (m_active_kind == ActiveKind::kWritev) {
                if (!m_writev_context.handleComplete(handle)) {
                    return SequenceProgress::kNeedWait;
                }
                auto io_result = std::move(m_writev_context.m_result);
                clearActiveTask();
                m_machine.onWrite(std::move(io_result));
                continue;
            }
            if (m_active_kind == ActiveKind::kConnect) {
                if (!m_connect_context.handleComplete(handle)) {
                    return SequenceProgress::kNeedWait;
                }
                auto io_result = std::move(m_connect_context.m_result);
                clearActiveTask();
                deliverConnect(std::move(io_result));
                continue;
            }
            m_error = IOError(kParamInvalid, 0);
            return SequenceProgress::kCompleted;
        }
        m_error = IOError(kParamInvalid, 0);
        clearActiveTask();
        return SequenceProgress::kCompleted;
    }

    SequenceProgress onActiveEvent(GHandle handle) override {
        if (!m_has_active_task) {
            return prepareForSubmit(handle);
        }
        if (m_active_kind == ActiveKind::kRead) {
            if (!m_recv_context.handleComplete(handle)) {
                return SequenceProgress::kNeedWait;
            }
            auto io_result = std::move(m_recv_context.m_result);
            clearActiveTask();
            m_machine.onRead(std::move(io_result));
            return prepareForSubmit(handle);
        }
        if (m_active_kind == ActiveKind::kReadv) {
            if (!m_readv_context.handleComplete(handle)) {
                return SequenceProgress::kNeedWait;
            }
            auto io_result = std::move(m_readv_context.m_result);
            clearActiveTask();
            m_machine.onRead(std::move(io_result));
            return prepareForSubmit(handle);
        }
        if (m_active_kind == ActiveKind::kWrite) {
            if (!m_send_context.handleComplete(handle)) {
                return SequenceProgress::kNeedWait;
            }
            auto io_result = std::move(m_send_context.m_result);
            clearActiveTask();
            m_machine.onWrite(std::move(io_result));
            return prepareForSubmit(handle);
        }
        if (m_active_kind == ActiveKind::kWritev) {
            if (!m_writev_context.handleComplete(handle)) {
                return SequenceProgress::kNeedWait;
            }
            auto io_result = std::move(m_writev_context.m_result);
            clearActiveTask();
            m_machine.onWrite(std::move(io_result));
            return prepareForSubmit(handle);
        }
        if (m_active_kind == ActiveKind::kConnect) {
            if (!m_connect_context.handleComplete(handle)) {
                return SequenceProgress::kNeedWait;
            }
            auto io_result = std::move(m_connect_context.m_result);
            clearActiveTask();
            deliverConnect(std::move(io_result));
            return prepareForSubmit(handle);
        }
        m_error = IOError(kParamInvalid, 0);
        return SequenceProgress::kCompleted;
    }
#endif

private:
    enum class ActiveKind {
        kNone,
        kRead,
        kReadv,
        kWrite,
        kWritev,
        kConnect,
    };

    static constexpr size_t kInlineTransitionCap = 64;

    SequenceProgress pump() {
        for (size_t i = 0; i < kInlineTransitionCap; ++i) {
            if (m_result_set || m_error.has_value()) {
                return SequenceProgress::kCompleted;
            }
            if (m_has_active_task) {
                return SequenceProgress::kNeedWait;
            }

            auto action = m_machine.advance();
            switch (action.signal) {
            case MachineSignal::kContinue:
                continue;
            case MachineSignal::kWaitRead:
                if (action.read_buffer == nullptr && action.read_length != 0) {
                    m_error = IOError(kParamInvalid, 0);
                    clearActiveTask();
                    return SequenceProgress::kCompleted;
                }
                if (action.read_length == 0) {
                    m_machine.onRead(std::expected<size_t, IOError>(size_t{0}));
                    continue;
                }
                m_recv_context.m_buffer = action.read_buffer;
                m_recv_context.m_length = action.read_length;
                m_active_task = IOTask{RECV, nullptr, &m_recv_context};
                m_has_active_task = true;
                m_active_kind = ActiveKind::kRead;
                return SequenceProgress::kNeedWait;
            case MachineSignal::kWaitReadv:
                if (action.iovecs == nullptr && action.iov_count != 0) {
                    m_error = IOError(kParamInvalid, 0);
                    clearActiveTask();
                    return SequenceProgress::kCompleted;
                }
                if (action.iov_count == 0) {
                    m_machine.onRead(std::expected<size_t, IOError>(size_t{0}));
                    continue;
                }
                m_readv_context.m_iovecs = std::span<const struct iovec>(action.iovecs, action.iov_count);
#ifdef USE_IOURING
                m_readv_context.initMsghdr();
#endif
                m_active_task = IOTask{READV, nullptr, &m_readv_context};
                m_has_active_task = true;
                m_active_kind = ActiveKind::kReadv;
                return SequenceProgress::kNeedWait;
            case MachineSignal::kWaitWrite:
                if (action.write_buffer == nullptr && action.write_length != 0) {
                    m_error = IOError(kParamInvalid, 0);
                    clearActiveTask();
                    return SequenceProgress::kCompleted;
                }
                if (action.write_length == 0) {
                    m_machine.onWrite(std::expected<size_t, IOError>(size_t{0}));
                    continue;
                }
                m_send_context.m_buffer = action.write_buffer;
                m_send_context.m_length = action.write_length;
                m_active_task = IOTask{SEND, nullptr, &m_send_context};
                m_has_active_task = true;
                m_active_kind = ActiveKind::kWrite;
                return SequenceProgress::kNeedWait;
            case MachineSignal::kWaitWritev:
                if (action.iovecs == nullptr && action.iov_count != 0) {
                    m_error = IOError(kParamInvalid, 0);
                    clearActiveTask();
                    return SequenceProgress::kCompleted;
                }
                if (action.iov_count == 0) {
                    m_machine.onWrite(std::expected<size_t, IOError>(size_t{0}));
                    continue;
                }
                m_writev_context.m_iovecs = std::span<const struct iovec>(action.iovecs, action.iov_count);
#ifdef USE_IOURING
                m_writev_context.initMsghdr();
#endif
                m_active_task = IOTask{WRITEV, nullptr, &m_writev_context};
                m_has_active_task = true;
                m_active_kind = ActiveKind::kWritev;
                return SequenceProgress::kNeedWait;
            case MachineSignal::kWaitConnect:
                m_connect_context.m_host = action.connect_host;
                m_active_task = IOTask{CONNECT, nullptr, &m_connect_context};
                m_has_active_task = true;
                m_active_kind = ActiveKind::kConnect;
                return SequenceProgress::kNeedWait;
            case MachineSignal::kComplete:
                if (!action.result.has_value()) {
                    m_error = IOError(kParamInvalid, 0);
                    clearActiveTask();
                    return SequenceProgress::kCompleted;
                }
                m_result = std::move(*action.result);
                m_result_set = true;
                clearActiveTask();
                return SequenceProgress::kCompleted;
            case MachineSignal::kFail:
                m_error = action.error.value_or(IOError(kParamInvalid, 0));
                clearActiveTask();
                return SequenceProgress::kCompleted;
            }
        }
        m_error = IOError(kParamInvalid, 0);
        clearActiveTask();
        return SequenceProgress::kCompleted;
    }

    void deliverConnect(std::expected<void, IOError> result) {
        if constexpr (requires(MachineT& machine, std::expected<void, IOError> connect_result) {
            { machine.onConnect(std::move(connect_result)) } -> std::same_as<void>;
        }) {
            m_machine.onConnect(std::move(result));
        } else {
            m_error = IOError(kParamInvalid, 0);
        }
    }

    void clearActiveTask() {
        m_active_task = IOTask{};
        m_has_active_task = false;
        m_active_kind = ActiveKind::kNone;
    }

    MachineT m_machine;
    RecvIOContext m_recv_context;
    ReadvIOContext m_readv_context;
    SendIOContext m_send_context;
    WritevIOContext m_writev_context;
    ConnectIOContext m_connect_context;
    IOTask m_active_task{};
    bool m_has_active_task = false;
    ActiveKind m_active_kind = ActiveKind::kNone;
    std::optional<result_type> m_result;
    bool m_result_set = false;
};

template <AwaitableStateMachine MachineT>
class StateMachineBuilder {
public:
    StateMachineBuilder(IOController* controller, MachineT machine)
        : m_controller(controller)
        , m_machine(std::move(machine)) {}

    auto build() & -> StateMachineAwaitable<MachineT> {
        return StateMachineAwaitable<MachineT>(m_controller, std::move(m_machine));
    }

    auto build() && -> StateMachineAwaitable<MachineT> {
        return StateMachineAwaitable<MachineT>(m_controller, std::move(m_machine));
    }

private:
    IOController* m_controller;
    MachineT m_machine;
};

template <typename ResultT, size_t InlineN, typename FlowT, typename BaseContextT, auto Handler>
struct SequenceStep : public SequenceAwaitable<ResultT, InlineN>::TaskBase, public BaseContextT {
    static_assert(std::is_base_of_v<IOContextBase, BaseContextT>,
                  "SequenceStep requires an IOContextBase-derived base context");

    template <typename... Args>
    explicit SequenceStep(FlowT* owner, Args&&... args)
        : BaseContextT(std::forward<Args>(args)...)
        , m_owner(owner) {}

    IOContextBase* contextBase() override {
        return this;
    }

    IOEventType defaultEventType() const override {
        return detail::customAwaitableDefaultEvent<BaseContextT>();
    }

    bool isLocal() const override {
        return false;
    }

#ifdef USE_IOURING
    bool onEvent(SequenceAwaitable<ResultT, InlineN>& owner, struct io_uring_cqe* cqe, GHandle handle) override {
        if (!BaseContextT::handleComplete(cqe, handle)) {
            return false;
        }
        auto ops = owner.ops();
        (m_owner->*Handler)(ops, static_cast<BaseContextT&>(*this));
        return true;
    }
#else
    bool onReady(SequenceAwaitable<ResultT, InlineN>& owner, GHandle handle) override {
        if (!BaseContextT::handleComplete(handle)) {
            return false;
        }
        auto ops = owner.ops();
        (m_owner->*Handler)(ops, static_cast<BaseContextT&>(*this));
        return true;
    }

    bool onEvent(SequenceAwaitable<ResultT, InlineN>& owner, GHandle handle) override {
        if (!BaseContextT::handleComplete(handle)) {
            return false;
        }
        auto ops = owner.ops();
        (m_owner->*Handler)(ops, static_cast<BaseContextT&>(*this));
        return true;
    }
#endif

private:
    FlowT* m_owner;
};

template <typename ResultT, size_t InlineN, typename FlowT, auto Handler>
struct LocalSequenceStep : public SequenceAwaitable<ResultT, InlineN>::TaskBase {
    explicit LocalSequenceStep(FlowT* owner)
        : m_owner(owner) {}

    IOContextBase* contextBase() override {
        return nullptr;
    }

    IOEventType defaultEventType() const override {
        return IOEventType::INVALID;
    }

    bool isLocal() const override {
        return true;
    }

#ifdef USE_IOURING
    bool onEvent(SequenceAwaitable<ResultT, InlineN>& owner, struct io_uring_cqe*, GHandle) override {
        auto ops = owner.ops();
        (m_owner->*Handler)(ops);
        return true;
    }
#else
    bool onReady(SequenceAwaitable<ResultT, InlineN>& owner, GHandle) override {
        auto ops = owner.ops();
        (m_owner->*Handler)(ops);
        return true;
    }

    bool onEvent(SequenceAwaitable<ResultT, InlineN>& owner, GHandle) override {
        auto ops = owner.ops();
        (m_owner->*Handler)(ops);
        return true;
    }
#endif

private:
    FlowT* m_owner;
};

template <typename ResultT, size_t InlineN, typename FlowT, auto Handler>
struct ParserSequenceStep : public SequenceAwaitable<ResultT, InlineN>::TaskBase {
    explicit ParserSequenceStep(FlowT* owner,
                                typename SequenceAwaitable<ResultT, InlineN>::TaskBase* rearm_step)
        : m_owner(owner)
        , m_rearm_step(rearm_step) {}

    IOContextBase* contextBase() override {
        return nullptr;
    }

    IOEventType defaultEventType() const override {
        return IOEventType::INVALID;
    }

    bool isLocal() const override {
        return true;
    }

#ifdef USE_IOURING
    bool onEvent(SequenceAwaitable<ResultT, InlineN>& owner, struct io_uring_cqe*, GHandle) override {
        return run(owner);
    }
#else
    bool onReady(SequenceAwaitable<ResultT, InlineN>& owner, GHandle) override {
        return run(owner);
    }

    bool onEvent(SequenceAwaitable<ResultT, InlineN>& owner, GHandle) override {
        return run(owner);
    }
#endif

private:
    bool run(SequenceAwaitable<ResultT, InlineN>& owner) {
        auto ops = owner.ops();
        const ParseStatus status = (m_owner->*Handler)(ops);
        switch (status) {
            case ParseStatus::kNeedMore:
                if (m_rearm_step == nullptr || m_rearm_step->isLocal()) {
                    owner.fail(IOError(kParamInvalid, 0));
                    return true;
                }
                owner.queue(*m_rearm_step);
                owner.queue(*this);
                return true;
            case ParseStatus::kContinue:
                owner.queue(*this);
                return true;
            case ParseStatus::kCompleted:
                return true;
        }
        owner.fail(IOError(kParamInvalid, 0));
        return true;
    }

    FlowT* m_owner;
    typename SequenceAwaitable<ResultT, InlineN>::TaskBase* m_rearm_step;
};

namespace detail {

template <typename ResultT, size_t InlineN, typename FlowT>
class LinearMachine {
public:
    using result_type = ResultT;
    using OpsT = SequenceOps<ResultT, InlineN>;

    static constexpr size_t kInvalidIndex = static_cast<size_t>(-1);

    enum class NodeKind : uint8_t {
        kRecv,
        kReadv,
        kSend,
        kWritev,
        kConnect,
        kParse,
        kLocal,
        kFinish,
    };

    using IOHandlerFn = void(*)(FlowT*, OpsT&, IOContextBase&);
    using LocalHandlerFn = void(*)(FlowT*, OpsT&);
    using ParseHandlerFn = ParseStatus(*)(FlowT*, OpsT&);

    struct Node {
        NodeKind kind = NodeKind::kLocal;
        IOHandlerFn io_handler = nullptr;
        LocalHandlerFn local_handler = nullptr;
        ParseHandlerFn parse_handler = nullptr;
        char* read_buffer = nullptr;
        const char* write_buffer = nullptr;
        const struct iovec* iovecs = nullptr;
        size_t iov_count = 0;
        size_t io_length = 0;
        Host connect_host{};
        size_t parse_rearm_recv_index = kInvalidIndex;
    };

    using NodeList = std::vector<Node>;

    LinearMachine(IOController* controller, FlowT* flow, NodeList nodes)
        : m_controller(controller)
        , m_flow(flow)
        , m_nodes(std::move(nodes))
        , m_ops_owner(nullptr)
        , m_recv_context(nullptr, 0)
        , m_readv_context(std::span<const struct iovec>{})
        , m_send_context(nullptr, 0)
        , m_writev_context(std::span<const struct iovec>{})
        , m_connect_context(Host{}) {}

    template <auto Handler>
    static Node makeRecvNode(char* buffer, size_t length) {
        Node node;
        node.kind = NodeKind::kRecv;
        node.io_handler = &invokeIO<RecvIOContext, Handler>;
        node.read_buffer = buffer;
        node.io_length = length;
        return node;
    }

    template <auto Handler>
    static Node makeSendNode(const char* buffer, size_t length) {
        Node node;
        node.kind = NodeKind::kSend;
        node.io_handler = &invokeIO<SendIOContext, Handler>;
        node.write_buffer = buffer;
        node.io_length = length;
        return node;
    }

    template <auto Handler>
    static Node makeReadvNode(const struct iovec* iovecs, size_t count) {
        Node node;
        node.kind = NodeKind::kReadv;
        node.io_handler = &invokeIO<ReadvIOContext, Handler>;
        node.iovecs = iovecs;
        node.iov_count = count;
        return node;
    }

    template <auto Handler>
    static Node makeWritevNode(const struct iovec* iovecs, size_t count) {
        Node node;
        node.kind = NodeKind::kWritev;
        node.io_handler = &invokeIO<WritevIOContext, Handler>;
        node.iovecs = iovecs;
        node.iov_count = count;
        return node;
    }

    template <auto Handler>
    static Node makeConnectNode(const Host& host) {
        Node node;
        node.kind = NodeKind::kConnect;
        node.io_handler = &invokeIO<ConnectIOContext, Handler>;
        node.connect_host = host;
        return node;
    }

    template <auto Handler>
    static Node makeLocalNode() {
        Node node;
        node.kind = NodeKind::kLocal;
        node.local_handler = &invokeLocal<Handler>;
        return node;
    }

    template <auto Handler>
    static Node makeFinishNode() {
        Node node;
        node.kind = NodeKind::kFinish;
        node.local_handler = &invokeLocal<Handler>;
        return node;
    }

    template <auto Handler>
    static Node makeParseNode(size_t rearm_recv_index) {
        Node node;
        node.kind = NodeKind::kParse;
        node.parse_handler = &invokeParse<Handler>;
        node.parse_rearm_recv_index = rearm_recv_index;
        return node;
    }

    MachineAction<result_type> advance() {
        if (m_result.has_value()) {
            return MachineAction<result_type>::complete(std::move(*m_result));
        }
        if (m_error.has_value()) {
            return MachineAction<result_type>::fail(*m_error);
        }
        if (m_cursor >= m_nodes.size()) {
            setIOError(IOError(kNotReady, 0));
            return emitActionFromOutcome();
        }

        const Node& node = m_nodes[m_cursor];
        switch (node.kind) {
        case NodeKind::kRecv:
            m_recv_context.m_buffer = node.read_buffer;
            m_recv_context.m_length = node.io_length;
            m_pending_io = PendingIO::kRead;
            m_pending_index = m_cursor;
            return MachineAction<result_type>::waitRead(node.read_buffer, node.io_length);
        case NodeKind::kReadv:
            m_readv_context.m_iovecs = std::span<const struct iovec>(node.iovecs, node.iov_count);
#ifdef USE_IOURING
            m_readv_context.initMsghdr();
#endif
            m_pending_io = PendingIO::kReadv;
            m_pending_index = m_cursor;
            return MachineAction<result_type>::waitReadv(node.iovecs, node.iov_count);
        case NodeKind::kSend:
            m_send_context.m_buffer = node.write_buffer;
            m_send_context.m_length = node.io_length;
            m_pending_io = PendingIO::kWrite;
            m_pending_index = m_cursor;
            return MachineAction<result_type>::waitWrite(node.write_buffer, node.io_length);
        case NodeKind::kWritev:
            m_writev_context.m_iovecs = std::span<const struct iovec>(node.iovecs, node.iov_count);
#ifdef USE_IOURING
            m_writev_context.initMsghdr();
#endif
            m_pending_io = PendingIO::kWritev;
            m_pending_index = m_cursor;
            return MachineAction<result_type>::waitWritev(node.iovecs, node.iov_count);
        case NodeKind::kConnect:
            return runConnect(node);
        case NodeKind::kParse:
            return runParse(node);
        case NodeKind::kLocal:
        case NodeKind::kFinish:
            return runLocal(node);
        }
        setIOError(IOError(kParamInvalid, 0));
        return emitActionFromOutcome();
    }

    void onRead(std::expected<size_t, IOError> result) {
        if ((m_pending_io != PendingIO::kRead && m_pending_io != PendingIO::kReadv) ||
            m_pending_index >= m_nodes.size()) {
            setIOError(IOError(kParamInvalid, 0));
            return;
        }

        const bool has_value = result.has_value();
        std::optional<IOError> io_error;
        if (!has_value) {
            io_error = result.error();
        }
        const Node& node = m_nodes[m_pending_index];
        if (m_pending_io == PendingIO::kRead) {
            m_recv_context.m_result = std::move(result);
            invokeIONode(node, m_recv_context);
        } else {
            m_readv_context.m_result = std::move(result);
            invokeIONode(node, m_readv_context);
        }
        clearPendingIO();

        if (absorbOpsOutcome()) {
            return;
        }
        if (io_error.has_value()) {
            setIOError(std::move(*io_error));
            return;
        }
        ++m_cursor;
    }

    void onWrite(std::expected<size_t, IOError> result) {
        if ((m_pending_io != PendingIO::kWrite && m_pending_io != PendingIO::kWritev) ||
            m_pending_index >= m_nodes.size()) {
            setIOError(IOError(kParamInvalid, 0));
            return;
        }

        const bool has_value = result.has_value();
        std::optional<IOError> io_error;
        if (!has_value) {
            io_error = result.error();
        }
        const Node& node = m_nodes[m_pending_index];
        if (m_pending_io == PendingIO::kWrite) {
            m_send_context.m_result = std::move(result);
            invokeIONode(node, m_send_context);
        } else {
            m_writev_context.m_result = std::move(result);
            invokeIONode(node, m_writev_context);
        }
        clearPendingIO();

        if (absorbOpsOutcome()) {
            return;
        }
        if (io_error.has_value()) {
            setIOError(std::move(*io_error));
            return;
        }
        ++m_cursor;
    }

    void onConnect(std::expected<void, IOError> result) {
        if (m_pending_io != PendingIO::kConnect || m_pending_index >= m_nodes.size()) {
            setIOError(IOError(kParamInvalid, 0));
            return;
        }

        const bool has_value = result.has_value();
        std::optional<IOError> io_error;
        if (!has_value) {
            io_error = result.error();
        }
        m_connect_context.m_result = std::move(result);

        const Node& node = m_nodes[m_pending_index];
        invokeIONode(node, m_connect_context);
        clearPendingIO();

        if (absorbOpsOutcome()) {
            return;
        }
        if (io_error.has_value()) {
            setIOError(std::move(*io_error));
            return;
        }
        ++m_cursor;
    }

private:
    enum class PendingIO : uint8_t {
        kNone,
        kRead,
        kReadv,
        kWrite,
        kWritev,
        kConnect,
    };

    template <typename ContextT, auto Handler>
    static void invokeIO(FlowT* flow, OpsT& ops, IOContextBase& context) {
        (flow->*Handler)(ops, static_cast<ContextT&>(context));
    }

    template <auto Handler>
    static void invokeLocal(FlowT* flow, OpsT& ops) {
        (flow->*Handler)(ops);
    }

    template <auto Handler>
    static ParseStatus invokeParse(FlowT* flow, OpsT& ops) {
        return (flow->*Handler)(ops);
    }

    MachineAction<result_type> runConnect(const Node& node) {
        if (node.io_handler == nullptr) {
            setIOError(IOError(kParamInvalid, 0));
            return emitActionFromOutcome();
        }

        m_connect_context.m_host = node.connect_host;
        m_pending_io = PendingIO::kConnect;
        m_pending_index = m_cursor;
        return MachineAction<result_type>::waitConnect(node.connect_host);
    }

    MachineAction<result_type> runLocal(const Node& node) {
        if (node.local_handler == nullptr) {
            setIOError(IOError(kParamInvalid, 0));
            return emitActionFromOutcome();
        }

        m_ops_owner.resetOutcomeForReuse();
        auto ops = m_ops_owner.ops();
        node.local_handler(m_flow, ops);

        if (absorbOpsOutcome()) {
            return emitActionFromOutcome();
        }
        ++m_cursor;
        return MachineAction<result_type>::continue_();
    }

    MachineAction<result_type> runParse(const Node& node) {
        if (node.parse_handler == nullptr) {
            setIOError(IOError(kParamInvalid, 0));
            return emitActionFromOutcome();
        }

        m_ops_owner.resetOutcomeForReuse();
        auto ops = m_ops_owner.ops();
        const ParseStatus status = node.parse_handler(m_flow, ops);

        if (absorbOpsOutcome()) {
            return emitActionFromOutcome();
        }

        switch (status) {
        case ParseStatus::kNeedMore:
            if (node.parse_rearm_recv_index == kInvalidIndex ||
                node.parse_rearm_recv_index >= m_nodes.size() ||
                (m_nodes[node.parse_rearm_recv_index].kind != NodeKind::kRecv &&
                 m_nodes[node.parse_rearm_recv_index].kind != NodeKind::kReadv)) {
                setIOError(IOError(kParamInvalid, 0));
                return emitActionFromOutcome();
            }
            m_cursor = node.parse_rearm_recv_index;
            return MachineAction<result_type>::continue_();
        case ParseStatus::kContinue:
            return MachineAction<result_type>::continue_();
        case ParseStatus::kCompleted:
            ++m_cursor;
            return MachineAction<result_type>::continue_();
        }
        setIOError(IOError(kParamInvalid, 0));
        return emitActionFromOutcome();
    }

    void invokeIONode(const Node& node, IOContextBase& context) {
        if (node.io_handler == nullptr) {
            setIOError(IOError(kParamInvalid, 0));
            return;
        }
        m_ops_owner.resetOutcomeForReuse();
        auto ops = m_ops_owner.ops();
        node.io_handler(m_flow, ops, context);
    }

    bool absorbOpsOutcome() {
        if (m_ops_owner.hasResultValue()) {
            auto result = m_ops_owner.takeResultValue();
            if (result.has_value()) {
                m_result = std::move(*result);
            } else {
                setIOError(IOError(kParamInvalid, 0));
            }
            return true;
        }
        if (m_ops_owner.hasFailure()) {
            auto error = m_ops_owner.takeFailure();
            if (error.has_value()) {
                setIOError(std::move(*error));
            } else {
                setIOError(IOError(kParamInvalid, 0));
            }
            return true;
        }
        if (!m_ops_owner.empty()) {
            m_ops_owner.clear();
            setIOError(IOError(kParamInvalid, 0));
            return true;
        }
        return false;
    }

    MachineAction<result_type> emitActionFromOutcome() {
        if (m_result.has_value()) {
            return MachineAction<result_type>::complete(std::move(*m_result));
        }
        if (m_error.has_value()) {
            return MachineAction<result_type>::fail(*m_error);
        }
        return MachineAction<result_type>::continue_();
    }

    void clearPendingIO() {
        m_pending_io = PendingIO::kNone;
        m_pending_index = kInvalidIndex;
    }

    void setIOError(IOError error) {
        if constexpr (detail::is_expected_v<result_type>) {
            using ErrorT = typename detail::expected_traits<result_type>::error_type;
            if constexpr (std::is_constructible_v<ErrorT, IOError>) {
                m_result = std::unexpected(ErrorT(std::move(error)));
                return;
            }
        }
        m_error = std::move(error);
    }

    IOController* m_controller;
    FlowT* m_flow;
    NodeList m_nodes;
    size_t m_cursor = 0;
    PendingIO m_pending_io = PendingIO::kNone;
    size_t m_pending_index = kInvalidIndex;

    SequenceAwaitable<ResultT, InlineN> m_ops_owner;
    RecvIOContext m_recv_context;
    ReadvIOContext m_readv_context;
    SendIOContext m_send_context;
    WritevIOContext m_writev_context;
    ConnectIOContext m_connect_context;

    std::optional<result_type> m_result;
    std::optional<IOError> m_error;
};

} // namespace detail

template <typename ResultT, size_t InlineN = 4, typename FlowT = void>
class AwaitableBuilder {
public:
    using MachineT = detail::LinearMachine<ResultT, InlineN, FlowT>;
    using MachineNode = typename MachineT::Node;

    AwaitableBuilder(IOController* controller, FlowT& flow)
        : m_controller(controller)
        , m_flow(&flow)
    {
        m_nodes.reserve(InlineN);
    }

    template <AwaitableStateMachine MachineTParam>
    static auto fromStateMachine(IOController* controller, MachineTParam machine) -> StateMachineBuilder<MachineTParam> {
        static_assert(std::same_as<typename MachineTParam::result_type, ResultT>,
                      "AwaitableBuilder::fromStateMachine requires matching result_type");
        return StateMachineBuilder<MachineTParam>(controller, std::move(machine));
    }

    template <auto Handler>
    AwaitableBuilder& local() {
        m_nodes.push_back(MachineT::template makeLocalNode<Handler>());
        return *this;
    }

    template <auto Handler>
    AwaitableBuilder& parse() {
        m_nodes.push_back(MachineT::template makeParseNode<Handler>(m_last_recv_index));
        return *this;
    }

    template <auto Handler>
    AwaitableBuilder& finish() {
        m_nodes.push_back(MachineT::template makeFinishNode<Handler>());
        return *this;
    }

    template <auto Handler>
    AwaitableBuilder& recv(char* buffer, size_t length) {
        m_nodes.push_back(MachineT::template makeRecvNode<Handler>(buffer, length));
        m_last_recv_index = m_nodes.size() - 1;
        return *this;
    }

    template <auto Handler, size_t N>
    AwaitableBuilder& readv(std::array<struct iovec, N>& iovecs, size_t count = N) {
        const size_t bounded = ReadvIOContext::validateBorrowedCountOrAbort(count, N, "readv");
        m_nodes.push_back(MachineT::template makeReadvNode<Handler>(iovecs.data(), bounded));
        m_last_recv_index = m_nodes.size() - 1;
        return *this;
    }

    template <auto Handler, size_t N>
    AwaitableBuilder& readv(struct iovec (&iovecs)[N], size_t count = N) {
        const size_t bounded = ReadvIOContext::validateBorrowedCountOrAbort(count, N, "readv");
        m_nodes.push_back(MachineT::template makeReadvNode<Handler>(iovecs, bounded));
        m_last_recv_index = m_nodes.size() - 1;
        return *this;
    }

    template <auto Handler>
    AwaitableBuilder& send(const char* buffer, size_t length) {
        m_nodes.push_back(MachineT::template makeSendNode<Handler>(buffer, length));
        return *this;
    }

    template <auto Handler, size_t N>
    AwaitableBuilder& writev(std::array<struct iovec, N>& iovecs, size_t count = N) {
        const size_t bounded = WritevIOContext::validateBorrowedCountOrAbort(count, N, "writev");
        m_nodes.push_back(MachineT::template makeWritevNode<Handler>(iovecs.data(), bounded));
        return *this;
    }

    template <auto Handler, size_t N>
    AwaitableBuilder& writev(struct iovec (&iovecs)[N], size_t count = N) {
        const size_t bounded = WritevIOContext::validateBorrowedCountOrAbort(count, N, "writev");
        m_nodes.push_back(MachineT::template makeWritevNode<Handler>(iovecs, bounded));
        return *this;
    }

    template <auto Handler>
    AwaitableBuilder& connect(const Host& host) {
        m_nodes.push_back(MachineT::template makeConnectNode<Handler>(host));
        return *this;
    }

    auto build() & -> StateMachineAwaitable<MachineT> {
        return buildImpl();
    }

    auto build() && -> StateMachineAwaitable<MachineT> {
        return buildImpl();
    }

private:
    auto buildImpl() -> StateMachineAwaitable<MachineT> {
        return StateMachineAwaitable<MachineT>(
            m_controller,
            MachineT(m_controller, m_flow, std::move(m_nodes))
        );
    }

    IOController* m_controller;
    FlowT* m_flow;
    std::vector<MachineNode> m_nodes;
    size_t m_last_recv_index = MachineT::kInvalidIndex;
};

template <typename ResultT, size_t InlineN>
class AwaitableBuilder<ResultT, InlineN, void> {
public:
    template <AwaitableStateMachine MachineT>
    static auto fromStateMachine(IOController* controller, MachineT machine) -> StateMachineBuilder<MachineT> {
        static_assert(std::same_as<typename MachineT::result_type, ResultT>,
                      "AwaitableBuilder::fromStateMachine requires matching result_type");
        return StateMachineBuilder<MachineT>(controller, std::move(machine));
    }
};


} // namespace galay::kernel

#include "Awaitable.inl"

#endif // GALAY_KERNEL_AWAITABLE_H
