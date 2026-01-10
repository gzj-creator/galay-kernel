#include "UdpSocket.h"
#include "common/Defn.hpp"
#include "common/Host.hpp"
#include <cerrno>
#include <stdexcept>
#include <unistd.h>

namespace galay::async
{

UdpSocket::UdpSocket(IPType type)
    : m_controller(create(type))
{
    if(m_controller.m_handle == GHandle::invalid()) {
        throw std::runtime_error(strerror(errno));
    }
}

UdpSocket::UdpSocket(GHandle handle)
    : m_controller(handle)
{
}

UdpSocket::~UdpSocket()
{
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : m_controller(std::move(other.m_controller))
{
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept
{
    if (this != &other) {
        m_controller = std::move(other.m_controller);
    }
    return *this;
}


GHandle UdpSocket::create(IPType type)
{
    int domain = (type == IPType::IPV4) ? AF_INET : AF_INET6;
    int fd = ::socket(domain, SOCK_DGRAM, 0);  // SOCK_DGRAM for UDP
    if (fd < 0) {
        return GHandle::invalid();
    }
    return {.fd = fd};
}

std::expected<void, IOError> UdpSocket::bind(const Host& host)
{
    if (::bind(m_controller.m_handle.fd, host.sockAddr(), host.addrLen()) < 0) {
        return std::unexpected(IOError(kBindFailed, errno));
    }
    return {};
}

RecvFromAwaitable UdpSocket::recvfrom(char* buffer, size_t length, Host* from)
{
    return RecvFromAwaitable(&m_controller, buffer, length, from);
}

SendToAwaitable UdpSocket::sendto(const char* buffer, size_t length, const Host& to)
{
    return SendToAwaitable(&m_controller, buffer, length, to);
}

CloseAwaitable UdpSocket::close()
{
    return CloseAwaitable(&m_controller);
}

}
