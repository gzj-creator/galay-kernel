/**
 * @file Awaitable.h
 * @brief 异步IO可等待对象
 * @author galay-kernel
 * @version 3.1.0
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

namespace detail {

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
    std::expected<size_t, IOError> m_result;
};

struct RecvAwaitable: public RecvIOContext, public TimeoutSupport<RecvAwaitable> {
    RecvAwaitable(IOController* controller, char* buffer, size_t length)
        : RecvIOContext(buffer, length), m_controller(controller) {}

    bool await_ready() { return false; }
    bool await_suspend(std::coroutine_handle<> handle);
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
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<size_t, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

// ---- Readv ----

struct ReadvIOContext: public IOContextBase {
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
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<size_t, IOError> await_resume();

    IOController* m_controller;
    Waker m_waker;
};

// ---- Writev ----

struct WritevIOContext: public IOContextBase {
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
    bool await_suspend(std::coroutine_handle<> handle);
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
    bool await_suspend(std::coroutine_handle<> handle);
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

enum class SequenceProgress {
    kNeedWait,
    kCompleted,
};

enum class ParseStatus {
    kNeedMore,
    kContinue,
    kCompleted,
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

    bool await_suspend(std::coroutine_handle<> handle);

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

template <typename ResultT, size_t InlineN, typename FlowT>
class AwaitableBuilder : public SequenceAwaitable<ResultT, InlineN> {
    using Base = SequenceAwaitable<ResultT, InlineN>;
    using TaskBase = typename Base::TaskBase;

public:
    AwaitableBuilder(IOController* controller, FlowT& flow)
        : Base(controller)
        , m_flow(&flow)
    {
        m_owned_steps.reserve(InlineN);
    }

    template <auto Handler>
    AwaitableBuilder& local() {
        auto* step = ownStep<LocalSequenceStep<ResultT, InlineN, FlowT, Handler>>(m_flow);
        this->queue(*step);
        return *this;
    }

    template <auto Handler>
    AwaitableBuilder& parse() {
        auto* step = ownStep<ParserSequenceStep<ResultT, InlineN, FlowT, Handler>>(m_flow, m_last_io_step);
        this->queue(*step);
        return *this;
    }

    template <auto Handler>
    AwaitableBuilder& finish() {
        return local<Handler>();
    }

    template <auto Handler>
    AwaitableBuilder& recv(char* buffer, size_t length) {
        auto* step = ownStep<SequenceStep<ResultT, InlineN, FlowT, RecvIOContext, Handler>>(m_flow, buffer, length);
        this->queue(*step);
        m_last_io_step = step;
        return *this;
    }

    template <auto Handler>
    AwaitableBuilder& send(const char* buffer, size_t length) {
        auto* step = ownStep<SequenceStep<ResultT, InlineN, FlowT, SendIOContext, Handler>>(m_flow, buffer, length);
        this->queue(*step);
        m_last_io_step = step;
        return *this;
    }

    template <auto Handler>
    AwaitableBuilder& connect(const Host& host) {
        auto* step = ownStep<SequenceStep<ResultT, InlineN, FlowT, ConnectIOContext, Handler>>(m_flow, host);
        this->queue(*step);
        m_last_io_step = step;
        return *this;
    }

    AwaitableBuilder&& build() & {
        return std::move(*this);
    }

    AwaitableBuilder&& build() && {
        return std::move(*this);
    }

private:
    template <typename StepT, typename... Args>
    StepT* ownStep(Args&&... args) {
        auto step = std::make_unique<StepT>(std::forward<Args>(args)...);
        StepT* raw = step.get();
        m_owned_steps.push_back(std::move(step));
        return raw;
    }

    FlowT* m_flow;
    std::vector<std::unique_ptr<TaskBase>> m_owned_steps;
    TaskBase* m_last_io_step = nullptr;
};


} // namespace galay::kernel

#include "Awaitable.inl"

#endif // GALAY_KERNEL_AWAITABLE_H
