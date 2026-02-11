#ifndef GALAY_KERNEL_IOHANDLERS_HPP
#define GALAY_KERNEL_IOHANDLERS_HPP

#if defined(USE_KQUEUE) || defined(USE_EPOLL)

#include "galay-kernel/common/Defn.hpp"
#include "galay-kernel/common/Error.h"
#include "galay-kernel/common/Bytes.h"
#include "galay-kernel/common/Host.hpp"
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <cerrno>
#include <expected>
#include <utility>

namespace galay::kernel::io
{

inline std::pair<std::expected<GHandle, IOError>, Host> handleAccept(GHandle listen_handle)
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

inline std::expected<Bytes, IOError> handleRecv(GHandle handle, char* buffer, size_t length)
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

inline std::expected<size_t, IOError> handleSend(GHandle handle, const char* buffer, size_t length)
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

inline std::expected<size_t, IOError> handleReadv(GHandle handle, struct iovec* iovecs, int iovcnt)
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

inline std::expected<size_t, IOError> handleWritev(GHandle handle, struct iovec* iovecs, int iovcnt)
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

inline std::expected<void, IOError> handleConnect(GHandle handle, const Host& host)
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

inline std::pair<std::expected<Bytes, IOError>, Host> handleRecvFrom(GHandle handle, char* buffer, size_t length)
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

inline std::expected<size_t, IOError> handleSendTo(GHandle handle, const char* buffer, size_t length, const Host& to)
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

inline std::expected<Bytes, IOError> handleFileRead(GHandle handle, char* buffer, size_t length, off_t offset)
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

inline std::expected<size_t, IOError> handleFileWrite(GHandle handle, const char* buffer, size_t length, off_t offset)
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

inline std::expected<size_t, IOError> handleSendFile(GHandle socket_handle, int file_fd, off_t offset, size_t count)
{
#ifdef USE_KQUEUE
    // macOS: sendfile(in_fd, out_fd, offset, &len, hdtr, flags)
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
#elif defined(USE_EPOLL)
    // Linux: sendfile(out_fd, in_fd, &offset, count)
    ssize_t sentBytes = ::sendfile(socket_handle.fd, file_fd, &offset, count);
    if (sentBytes >= 0) {
        return static_cast<size_t>(sentBytes);
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return std::unexpected(IOError(kNotReady, 0));
        }
        return std::unexpected(IOError(kSendFailed, static_cast<uint32_t>(errno)));
    }
#endif
}

} // namespace galay::kernel::io

#endif // defined(USE_KQUEUE) || defined(USE_EPOLL)

#endif // GALAY_KERNEL_IOHANDLERS_HPP
