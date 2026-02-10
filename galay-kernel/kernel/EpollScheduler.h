#ifndef GALAY_KERNEL_EPOLL_SCHEDULER_H
#define GALAY_KERNEL_EPOLL_SCHEDULER_H

#include "Coroutine.h"
#include "IOScheduler.hpp"

#ifdef USE_EPOLL

#include <sys/epoll.h>
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

#define OK 1

/**
 * @brief Epoll 调度器 (Linux)
 */
class EpollScheduler: public IOScheduler
{
public:
    EpollScheduler(int max_events = GALAY_SCHEDULER_MAX_EVENTS,
                   int batch_size = GALAY_SCHEDULER_BATCH_SIZE,
                   int check_interval_ms = GALAY_SCHEDULER_CHECK_INTERVAL_MS);
    ~EpollScheduler();

    EpollScheduler(const EpollScheduler&) = delete;
    EpollScheduler& operator=(const EpollScheduler&) = delete;

    void start();
    void stop();
    void notify();

    // 网络 IO
    int addAccept(IOController* controller) override;
    int addConnect(IOController* controller) override;
    int addRecv(IOController* controller) override;
    int addSend(IOController* controller) override;
    int addReadv(IOController* controller) override;
    int addWritev(IOController* controller) override;
    int addClose(IOController* controller) override;

    // 文件 IO (通过 libaio + eventfd)
    int addFileRead(IOController* controller) override;
    int addFileWrite(IOController* controller) override;

    // UDP IO
    int addRecvFrom(IOController* controller) override;
    int addSendTo(IOController* controller) override;

    // 文件监控 (通过 inotify)
    int addFileWatch(IOController* controller) override;

    // 通知事件（用于SSL等自定义IO处理）
    int addRecvNotify(IOController* controller) override;
    int addSendNotify(IOController* controller) override;

    // 零拷贝发送文件
    int addSendFile(IOController* controller) override;

    int remove(IOController* controller) override;

    bool spawn(Coroutine coro) override;

    bool spawnImmidiately(Coroutine co) override;

protected:
    int m_epoll_fd;
    std::atomic<bool> m_running;
    std::thread m_thread;

    int m_max_events;
    int m_batch_size;
    int m_check_interval_ms;

    int m_event_fd;

    moodycamel::ConcurrentQueue<Coroutine> m_coro_queue;
    std::vector<struct epoll_event> m_events;
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
    void eventLoop();
    void processPendingCoroutines();
    void processEvent(struct epoll_event& ev);
    void syncEpollEvents(IOController* controller);
    uint32_t buildEpollEvents(IOController* controller);
};

// ============ Inline function implementations ============

inline std::pair<std::expected<GHandle, IOError>, Host> EpollScheduler::handleAccept(GHandle listen_handle)
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

inline std::expected<Bytes, IOError> EpollScheduler::handleRecv(GHandle handle, char* buffer, size_t length)
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

inline std::expected<size_t, IOError> EpollScheduler::handleSend(GHandle handle, const char* buffer, size_t length)
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

inline std::expected<size_t, IOError> EpollScheduler::handleReadv(GHandle handle, struct iovec* iovecs, int iovcnt)
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

inline std::expected<size_t, IOError> EpollScheduler::handleWritev(GHandle handle, struct iovec* iovecs, int iovcnt)
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

inline std::expected<void, IOError> EpollScheduler::handleConnect(GHandle handle, const Host& host)
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

inline std::expected<size_t, IOError> EpollScheduler::handleSendFile(GHandle socket_handle, int file_fd, off_t offset, size_t count)
{
    ssize_t sentBytes = ::sendfile(socket_handle.fd, file_fd, &offset, count);
    if (sentBytes >= 0) {
        return static_cast<size_t>(sentBytes);
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return std::unexpected(IOError(kNotReady, 0));
        }
        return std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(errno)));
    }
}

inline std::expected<Bytes, IOError> EpollScheduler::handleFileRead(GHandle handle, char* buffer, size_t length, off_t offset)
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

inline std::expected<size_t, IOError> EpollScheduler::handleFileWrite(GHandle handle, const char* buffer, size_t length, off_t offset)
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

inline std::pair<std::expected<Bytes, IOError>, Host> EpollScheduler::handleRecvFrom(GHandle handle, char* buffer, size_t length)
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

inline std::expected<size_t, IOError> EpollScheduler::handleSendTo(GHandle handle, const char* buffer, size_t length, const Host& to)
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

#endif // USE_EPOLL

#endif
