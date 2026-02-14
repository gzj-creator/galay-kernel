#include "TcpSocket.h"
#include "common/Defn.hpp"
#include <cerrno>
#include <unistd.h>

namespace galay::async
{

TcpSocket::TcpSocket(IPType type)
    : m_controller(create(type))
{
    if(m_controller.m_handle == GHandle::invalid()) {
        throw std::runtime_error(strerror(errno));
    }
}

TcpSocket::TcpSocket(GHandle handle)
    : m_controller(handle)
{
}

TcpSocket::~TcpSocket()
{
}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept
    : m_controller(std::move(other.m_controller))
{
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept
{
    if (this != &other) {
        if (m_controller.m_handle != GHandle::invalid()) {
            ::close(m_controller.m_handle.fd);
        }
        m_controller = std::move(other.m_controller);
    }
    return *this;
}

GHandle TcpSocket::create(IPType type)
{
    int domain = (type == IPType::IPV4) ? AF_INET : AF_INET6;
    int fd = socket(domain, SOCK_STREAM, 0);
    if (fd < 0) {
        return GHandle::invalid();
    }
    return {.fd = fd};
}

std::expected<void, IOError> TcpSocket::bind(const Host& host)
{
    if (::bind(m_controller.m_handle.fd, host.sockAddr(), host.addrLen()) < 0) {
        return std::unexpected(IOError(kBindFailed, errno));
    }
    return {};
}

std::expected<void, IOError> TcpSocket::listen(int backlog)
{
    if (::listen(m_controller.m_handle.fd, backlog) < 0) {
        return std::unexpected(IOError(kListenFailed, errno));
    }
    return {};
}

AcceptAwaitable TcpSocket::accept(Host* clientHost)
{
    return AcceptAwaitable(&m_controller, clientHost);
}

ConnectAwaitable TcpSocket::connect(const Host& host)
{
    return ConnectAwaitable(&m_controller, host);
}

RecvAwaitable TcpSocket::recv(char* buffer, size_t length)
{
    return RecvAwaitable(&m_controller, buffer, length);
}

SendAwaitable TcpSocket::send(const char* buffer, size_t length)
{
    return SendAwaitable(&m_controller, buffer, length);
}

ReadvAwaitable TcpSocket::readv(std::vector<struct iovec> iovecs)
{
    return ReadvAwaitable(&m_controller, std::move(iovecs));
}

WritevAwaitable TcpSocket::writev(std::vector<struct iovec> iovecs)
{
    return WritevAwaitable(&m_controller, std::move(iovecs));
}

SendFileAwaitable TcpSocket::sendfile(int file_fd, off_t offset, size_t count)
{
    return SendFileAwaitable(&m_controller, file_fd, offset, count);
}

CloseAwaitable TcpSocket::close()
{
    return CloseAwaitable(&m_controller);
}

}
