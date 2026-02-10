#ifndef GALAY_KERNEL_KQUEUE_SCHEDULER_H
#define GALAY_KERNEL_KQUEUE_SCHEDULER_H

#include "Coroutine.h"
#include "IOScheduler.hpp"

#ifdef USE_KQUEUE

#include <sys/event.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <cerrno>
#include <vector>
#include <atomic>
#include <thread>
#include <concurrentqueue/moodycamel/concurrentqueue.h>
#include "galay-kernel/common/Error.h"
#include "galay-kernel/common/Bytes.h"
#include "galay-kernel/common/Host.hpp"

// Scheduler configuration macros
#ifndef GALAY_SCHEDULER_MAX_EVENTS
#define GALAY_SCHEDULER_MAX_EVENTS 1024
#endif

#ifndef GALAY_SCHEDULER_BATCH_SIZE
#define GALAY_SCHEDULER_BATCH_SIZE 256
#endif

#ifndef GALAY_SCHEDULER_CHECK_INTERVAL_MS
#define GALAY_SCHEDULER_CHECK_INTERVAL_MS 1
#endif

namespace galay::kernel
{

#define  OK 1


class KqueueScheduler: public IOScheduler
{
public:
    KqueueScheduler(int max_events = GALAY_SCHEDULER_MAX_EVENTS,
                    int batch_size = GALAY_SCHEDULER_BATCH_SIZE,
                    int check_interval_ms = GALAY_SCHEDULER_CHECK_INTERVAL_MS);
    ~KqueueScheduler();

    KqueueScheduler(const KqueueScheduler&) = delete;
    KqueueScheduler& operator=(const KqueueScheduler&) = delete;

    // Lifecycle
    void start() override;
    void stop() override;

    // Notify to wake up the scheduler
    void notify();

    // 返回1表示不用阻塞
    int addAccept(IOController* controller) override;
    int addConnect(IOController* controller) override;
    int addRecv(IOController* controller) override;
    int addSend(IOController* controller) override;
    int addReadv(IOController* controller) override;
    int addWritev(IOController* controller) override;
    int addClose(IOController* controller) override;
    int addFileRead(IOController* controller) override;
    int addFileWrite(IOController* controller) override;
    int addRecvFrom(IOController* controller) override;
    int addSendTo(IOController* controller) override;
    int addFileWatch(IOController* controller) override;
    int addRecvNotify(IOController* controller) override;
    int addSendNotify(IOController* controller) override;
    int addSendFile(IOController* controller) override;

    int remove(IOController* controller) override;
    // Coroutine scheduling
    bool spawn(Coroutine coro) override;

    bool spawnImmidiately(Coroutine co) override;
protected:
    int m_kqueue_fd;
    std::atomic<bool> m_running;
    std::thread m_thread;

    // Configuration
    int m_max_events;
    int m_batch_size;
    int m_check_interval_ms;

    // Pipe for notification (kqueue uses pipe)
    int m_notify_pipe[2];
    // Lock-free queue for coroutines
    moodycamel::ConcurrentQueue<Coroutine> m_coro_queue;
    // Event buffer
    std::vector<struct kevent> m_events;
    // Coroutine buffer for batch processing
    std::vector<Coroutine> m_coro_buffer;

    // Pure IO functions (inline for performance)
    inline std::pair<std::expected<GHandle, IOError>, Host> handleAccept(GHandle listen_handle);
    inline std::expected<void, IOError> handleConnect(GHandle handle, const Host& host);
    inline std::expected<Bytes, IOError> handleRecv(GHandle handle, char* buffer, size_t length);
    inline std::expected<size_t, IOError> handleSend(GHandle handle, const char* buffer, size_t length);
    inline std::expected<size_t, IOError> handleReadv(GHandle handle, struct iovec* iovecs, int iovcnt);
    inline std::expected<size_t, IOError> handleWritev(GHandle handle, struct iovec* iovecs, int iovcnt);
    inline std::expected<size_t, IOError> handleSendFile(GHandle socket_handle, int file_fd, off_t offset, size_t count);
    inline std::expected<Bytes, IOError> handleFileRead(GHandle handle, char* buffer, size_t length, off_t offset);
    inline std::expected<size_t, IOError> handleFileWrite(GHandle handle, const char* buffer, size_t length, off_t offset);
    inline std::pair<std::expected<Bytes, IOError>, Host> handleRecvFrom(GHandle handle, char* buffer, size_t length);
    inline std::expected<size_t, IOError> handleSendTo(GHandle handle, const char* buffer, size_t length, const Host& to);

private:
    // Main controller loop
    void eventLoop();
    void processPendingCoroutines();
    void processEvent(struct kevent& ev);
};

// ============ Inline function implementations ============

inline std::pair<std::expected<GHandle, IOError>, Host> KqueueScheduler::handleAccept(GHandle listen_handle)
{
    sockaddr_storage addr{};
    socklen_t addr_len = sizeof(addr);
    GHandle handle {
        .fd = accept(listen_handle.fd, reinterpret_cast<sockaddr*>(&addr), &addr_len),
    };
    if( handle.fd < 0 ) {
        if( static_cast<uint32_t>(errno) == EAGAIN || static_cast<uint32_t>(errno) == EWOULDBLOCK || static_cast<uint32_t>(errno) == EINTR ) {
            return {std::unexpected(IOError(kNotReady, 0)), {}};
        }
        return {std::unexpected(IOError(kAcceptFailed, static_cast<uint32_t>(errno))), {}};
    }
    Host host = Host::fromSockAddr(addr);
    return {handle, std::move(host)};
}

inline std::expected<Bytes, IOError> KqueueScheduler::handleRecv(GHandle handle, char* buffer, size_t length)
{
    int recvBytes = recv(handle.fd, buffer, length, 0);
    if (recvBytes > 0) {
        return Bytes::fromCString(buffer, recvBytes, recvBytes);
    } else if (recvBytes == 0) {
        return std::unexpected(IOError(kDisconnectError, static_cast<uint32_t>(errno)));
    } else {
        if(static_cast<uint32_t>(errno) == EAGAIN || static_cast<uint32_t>(errno) == EWOULDBLOCK || static_cast<uint32_t>(errno) == EINTR) {
            return std::unexpected(IOError(kNotReady, 0));
        }
        return std::unexpected(IOError(kRecvFailed, static_cast<uint32_t>(errno)));
    }
}

inline std::expected<size_t, IOError> KqueueScheduler::handleSend(GHandle handle, const char* buffer, size_t length)
{
    ssize_t sentBytes = send(handle.fd, buffer, length, 0);
    if (sentBytes >= 0) {
        return static_cast<size_t>(sentBytes);
    } else {
        if (static_cast<uint32_t>(errno) == EAGAIN || static_cast<uint32_t>(errno) == EWOULDBLOCK || static_cast<uint32_t>(errno) == EINTR) {
            return std::unexpected(IOError(kNotReady, 0));
        }
        return std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(errno)));
    }
}

inline std::expected<size_t, IOError> KqueueScheduler::handleReadv(GHandle handle, struct iovec* iovecs, int iovcnt)
{
    ssize_t readBytes = readv(handle.fd, iovecs, iovcnt);
    if (readBytes > 0) {
        return static_cast<size_t>(readBytes);
    } else if (readBytes == 0) {
        return std::unexpected(IOError(kDisconnectError, 0));
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return std::unexpected(IOError(kNotReady, 0));
        }
        return std::unexpected(IOError(kRecvFailed, static_cast<uint32_t>(errno)));
    }
}

inline std::expected<size_t, IOError> KqueueScheduler::handleWritev(GHandle handle, struct iovec* iovecs, int iovcnt)
{
    ssize_t writtenBytes = writev(handle.fd, iovecs, iovcnt);
    if (writtenBytes >= 0) {
        return static_cast<size_t>(writtenBytes);
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return std::unexpected(IOError(kNotReady, 0));
        }
        return std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(errno)));
    }
}

inline std::expected<void, IOError> KqueueScheduler::handleConnect(GHandle handle, const Host& host)
{
    int result = ::connect(handle.fd, host.sockAddr(), host.addrLen());
    if (result == 0) {
        return {};
    } else if (errno == EINPROGRESS) {
        return std::unexpected(IOError(kNotReady, 0));
    } else if (errno == EISCONN) {
        return {};
    } else {
        return std::unexpected(IOError(kConnectFailed, static_cast<uint32_t>(errno)));
    }
}

inline std::expected<size_t, IOError> KqueueScheduler::handleSendFile(GHandle socket_handle, int file_fd, off_t offset, size_t count)
{
    off_t len = static_cast<off_t>(count);
    int result = ::sendfile(file_fd, socket_handle.fd, offset, &len, nullptr, 0);
    if (result == 0) {
        return static_cast<size_t>(len);
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            if (len > 0) {
                return static_cast<size_t>(len);
            }
            return std::unexpected(IOError(kNotReady, 0));
        }
        return std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(errno)));
    }
}

inline std::expected<Bytes, IOError> KqueueScheduler::handleFileRead(GHandle handle, char* buffer, size_t length, off_t offset)
{
    ssize_t readBytes = pread(handle.fd, buffer, length, offset);
    if (readBytes > 0) {
        return Bytes::fromCString(buffer, readBytes, readBytes);
    } else if (readBytes == 0) {
        return Bytes();
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return std::unexpected(IOError(kNotReady, 0));
        }
        return std::unexpected(IOError(kReadFailed, static_cast<uint32_t>(errno)));
    }
}

inline std::expected<size_t, IOError> KqueueScheduler::handleFileWrite(GHandle handle, const char* buffer, size_t length, off_t offset)
{
    ssize_t writtenBytes = pwrite(handle.fd, buffer, length, offset);
    if (writtenBytes >= 0) {
        return static_cast<size_t>(writtenBytes);
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return std::unexpected(IOError(kNotReady, 0));
        }
        return std::unexpected(IOError(kWriteFailed, static_cast<uint32_t>(errno)));
    }
}

inline std::pair<std::expected<Bytes, IOError>, Host> KqueueScheduler::handleRecvFrom(GHandle handle, char* buffer, size_t length)
{
    sockaddr_storage addr{};
    socklen_t addr_len = sizeof(addr);
    ssize_t recvBytes = recvfrom(handle.fd, buffer, length, 0, reinterpret_cast<sockaddr*>(&addr), &addr_len);
    if (recvBytes > 0) {
        Bytes bytes = Bytes::fromCString(buffer, recvBytes, recvBytes);
        Host host = Host::fromSockAddr(addr);
        return {std::move(bytes), std::move(host)};
    } else if (recvBytes == 0) {
        return {std::unexpected(IOError(kRecvFailed, 0)), {}};
    } else {
        if (static_cast<uint32_t>(errno) == EAGAIN || static_cast<uint32_t>(errno) == EWOULDBLOCK || static_cast<uint32_t>(errno) == EINTR) {
            return {std::unexpected(IOError(kNotReady, 0)), {}};
        }
        return {std::unexpected(IOError(kRecvFailed, static_cast<uint32_t>(errno))), {}};
    }
}

inline std::expected<size_t, IOError> KqueueScheduler::handleSendTo(GHandle handle, const char* buffer, size_t length, const Host& to)
{
    ssize_t sentBytes = sendto(handle.fd, buffer, length, 0, to.sockAddr(), to.addrLen());
    if (sentBytes >= 0) {
        return static_cast<size_t>(sentBytes);
    } else {
        if (static_cast<uint32_t>(errno) == EAGAIN || static_cast<uint32_t>(errno) == EWOULDBLOCK || static_cast<uint32_t>(errno) == EINTR) {
            return std::unexpected(IOError(kNotReady, 0));
        }
        return std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(errno)));
    }
}

}

#endif // USE_KQUEUE

#endif
