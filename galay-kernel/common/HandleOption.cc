#include "HandleOption.h"
#include <fcntl.h>
#include <cerrno>

#if defined(__linux__) || defined(__APPLE__)
#include <netinet/tcp.h>
#include <sys/socket.h>
#elif defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace galay::kernel
{

HandleOption::HandleOption(GHandle handle)
    : m_handle(handle)
{
}

std::expected<void, IOError> HandleOption::handleBlock()
{
#if defined(_WIN32) || defined(_WIN64)
    u_long mode = 0;
    if (ioctlsocket(m_handle.fd, FIONBIO, &mode) != 0) {
        return std::unexpected(IOError(kBindFailed, WSAGetLastError()));
    }
#else
    int flags = fcntl(m_handle.fd, F_GETFL, 0);
    if (flags == -1) {
        return std::unexpected(IOError(kBindFailed, errno));
    }
    if (fcntl(m_handle.fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
        return std::unexpected(IOError(kBindFailed, errno));
    }
#endif
    return {};
}

std::expected<void, IOError> HandleOption::handleNonBlock()
{
#if defined(_WIN32) || defined(_WIN64)
    u_long mode = 1;
    if (ioctlsocket(m_handle.fd, FIONBIO, &mode) != 0) {
        return std::unexpected(IOError(kBindFailed, WSAGetLastError()));
    }
#else
    int flags = fcntl(m_handle.fd, F_GETFL, 0);
    if (flags == -1) {
        return std::unexpected(IOError(kBindFailed, errno));
    }
    if (fcntl(m_handle.fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return std::unexpected(IOError(kBindFailed, errno));
    }
#endif
    return {};
}

std::expected<void, IOError> HandleOption::handleReuseAddr()
{
    int opt = 1;
#if defined(_WIN32) || defined(_WIN64)
    if (setsockopt(m_handle.fd, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&opt), sizeof(opt)) != 0) {
        return std::unexpected(IOError(kBindFailed, WSAGetLastError()));
    }
#else
    if (setsockopt(m_handle.fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
        return std::unexpected(IOError(kBindFailed, errno));
    }
#endif
    return {};
}

std::expected<void, IOError> HandleOption::handleReusePort()
{
#if defined(_WIN32) || defined(_WIN64)
    // Windows doesn't have SO_REUSEPORT, use SO_REUSEADDR instead
    return handleReuseAddr();
#else
    int opt = 1;
    if (setsockopt(m_handle.fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) != 0) {
        return std::unexpected(IOError(kBindFailed, errno));
    }
    return {};
#endif
}

std::expected<void, IOError> HandleOption::handleTcpNoDelay()
{
    if (m_handle.fd < 0) {
        return std::unexpected(IOError(kParamInvalid, 0));
    }

    int opt = 1;
#if defined(_WIN32) || defined(_WIN64)
    if (::setsockopt(m_handle.fd, IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<const char*>(&opt), sizeof(opt)) != 0) {
        return std::unexpected(IOError(kBindFailed, WSAGetLastError()));
    }
#else
    if (::setsockopt(m_handle.fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) != 0) {
        return std::unexpected(IOError(kBindFailed, errno));
    }
#endif

    return {};
}

}
