#include "TcpSocket.h"
#include <cerrno>
#include <unistd.h>

namespace galay::async
{

TcpSocket::TcpSocket(IOScheduler* scheduler)
    : m_handle(GHandle::invalid())
    , m_scheduler(scheduler)
    , m_controller()
{
}

TcpSocket::TcpSocket(IOScheduler* scheduler, GHandle handle)
    : m_handle(handle)
    , m_scheduler(scheduler)
    , m_controller()
{
}

TcpSocket::~TcpSocket()
{
    // 不在析构函数中关闭，由用户显式调用close()
}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept
    : m_handle(other.m_handle)
    , m_scheduler(other.m_scheduler)
    , m_controller(std::move(other.m_controller))
{
    other.m_handle = GHandle::invalid();
    other.m_scheduler = nullptr;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept
{
    if (this != &other) {
        m_handle = other.m_handle;
        m_scheduler = other.m_scheduler;
        m_controller = std::move(other.m_controller);
        other.m_handle = GHandle::invalid();
        other.m_scheduler = nullptr;
    }
    return *this;
}

std::expected<void, IOError> TcpSocket::create(IPType type)
{
    int domain = (type == IPType::IPV4) ? AF_INET : AF_INET6;
    int fd = socket(domain, SOCK_STREAM, 0);
    if (fd < 0) {
        return std::unexpected(IOError(kBindFailed, errno));
    }
    m_handle.fd = fd;
    return {};
}

std::expected<void, IOError> TcpSocket::bind(const Host& host)
{
    if (::bind(m_handle.fd, host.sockAddr(), host.addrLen()) < 0) {
        return std::unexpected(IOError(kBindFailed, errno));
    }
    return {};
}

std::expected<void, IOError> TcpSocket::listen(int backlog)
{
    if (::listen(m_handle.fd, backlog) < 0) {
        return std::unexpected(IOError(kListenFailed, errno));
    }
    return {};
}

AcceptAwaitable TcpSocket::accept(Host* clientHost)
{
    return AcceptAwaitable(m_scheduler, &m_controller, m_handle, clientHost);
}

ConnectAwaitable TcpSocket::connect(const Host& host)
{
    return ConnectAwaitable(m_scheduler, &m_controller, m_handle, host);
}

RecvAwaitable TcpSocket::recv(char* buffer, size_t length)
{
    return RecvAwaitable(m_scheduler, &m_controller, m_handle, buffer, length);
}

SendAwaitable TcpSocket::send(const char* buffer, size_t length)
{
    return SendAwaitable(m_scheduler, &m_controller, m_handle, buffer, length);
}

CloseAwaitable TcpSocket::close()
{
    return CloseAwaitable(m_scheduler, m_handle);
}

}
