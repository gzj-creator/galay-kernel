#include "UdpSocket.h"
#include <cerrno>
#include <unistd.h>

namespace galay::async
{

UdpSocket::UdpSocket(IOScheduler* scheduler)
    : m_handle(GHandle::invalid())
    , m_scheduler(scheduler)
    , m_controller()
{
}

UdpSocket::UdpSocket(IOScheduler* scheduler, GHandle handle)
    : m_handle(handle)
    , m_scheduler(scheduler)
    , m_controller()
{
}

UdpSocket::~UdpSocket()
{
    // 不在析构函数中关闭，由用户显式调用close()
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : m_handle(other.m_handle)
    , m_scheduler(other.m_scheduler)
    , m_controller(std::move(other.m_controller))
{
    other.m_handle = GHandle::invalid();
    other.m_scheduler = nullptr;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept
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

std::expected<void, IOError> UdpSocket::create(IPType type)
{
    int domain = (type == IPType::IPV4) ? AF_INET : AF_INET6;
    int fd = socket(domain, SOCK_DGRAM, 0);  // SOCK_DGRAM for UDP
    if (fd < 0) {
        return std::unexpected(IOError(kBindFailed, errno));
    }
    m_handle.fd = fd;
    return {};
}

std::expected<void, IOError> UdpSocket::bind(const Host& host)
{
    if (::bind(m_handle.fd, host.sockAddr(), host.addrLen()) < 0) {
        return std::unexpected(IOError(kBindFailed, errno));
    }
    return {};
}

RecvFromAwaitable UdpSocket::recvfrom(char* buffer, size_t length, Host* from)
{
    return RecvFromAwaitable(m_scheduler, &m_controller, m_handle, buffer, length, from);
}

SendToAwaitable UdpSocket::sendto(const char* buffer, size_t length, const Host& to)
{
    return SendToAwaitable(m_scheduler, &m_controller, m_handle, buffer, length, to);
}

CloseAwaitable UdpSocket::close()
{
    return CloseAwaitable(m_scheduler, m_handle);
}

}
