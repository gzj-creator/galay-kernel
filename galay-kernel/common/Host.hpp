#ifndef GALAY_KEERNEL_HOST_HPP
#define GALAY_KEERNEL_HOST_HPP

#include "Defn.hpp"
#include <cstring>
#include <string>

namespace galay::kernel
{

enum class IPType : uint8_t {
    IPV4 = 0,
    IPV6 = 1,
};

struct Host {
    sockaddr_storage m_addr{};
    socklen_t m_addr_len = sizeof(sockaddr_storage);

    Host() {
        std::memset(&m_addr, 0, sizeof(m_addr));
        m_addr.ss_family = AF_INET;
        m_addr_len = sizeof(sockaddr_in);
    }

    Host(IPType proto, const std::string& ip, uint16_t port) {
        std::memset(&m_addr, 0, sizeof(m_addr));
        if (proto == IPType::IPV4) {
            sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(&m_addr);
            addr4->sin_family = AF_INET;
            addr4->sin_port = htons(port);
            inet_pton(AF_INET, ip.c_str(), &addr4->sin_addr);
            m_addr_len = sizeof(sockaddr_in);
        } else {
            sockaddr_in6* addr6 = reinterpret_cast<sockaddr_in6*>(&m_addr);
            addr6->sin6_family = AF_INET6;
            addr6->sin6_port = htons(port);
            inet_pton(AF_INET6, ip.c_str(), &addr6->sin6_addr);
            m_addr_len = sizeof(sockaddr_in6);
        }
    }

    // 从 IPv4 sockaddr 构造
    Host(const sockaddr_in& addr) {
        std::memset(&m_addr, 0, sizeof(m_addr));
        std::memcpy(&m_addr, &addr, sizeof(addr));
        m_addr_len = sizeof(sockaddr_in);
    }

    // 从 IPv6 sockaddr 构造
    Host(const sockaddr_in6& addr) {
        std::memset(&m_addr, 0, sizeof(m_addr));
        std::memcpy(&m_addr, &addr, sizeof(addr));
        m_addr_len = sizeof(sockaddr_in6);
    }

    // 从通用 sockaddr_storage 构造
    static Host fromSockAddr(const sockaddr_storage& addr) {
        Host host;
        std::memcpy(&host.m_addr, &addr, sizeof(addr));
        if (addr.ss_family == AF_INET) {
            host.m_addr_len = sizeof(sockaddr_in);
        } else if (addr.ss_family == AF_INET6) {
            host.m_addr_len = sizeof(sockaddr_in6);
        }
        return host;
    }

    bool isIPv4() const { return m_addr.ss_family == AF_INET; }
    bool isIPv6() const { return m_addr.ss_family == AF_INET6; }

    // 获取 IP 字符串（按需转换）
    std::string ip() const {
        if (isIPv4()) {
            const sockaddr_in* addr4 = reinterpret_cast<const sockaddr_in*>(&m_addr);
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr4->sin_addr, buf, sizeof(buf));
            return buf;
        } else {
            const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(&m_addr);
            char buf[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &addr6->sin6_addr, buf, sizeof(buf));
            return buf;
        }
    }

    // 获取端口
    uint16_t port() const {
        if (isIPv4()) {
            const sockaddr_in* addr4 = reinterpret_cast<const sockaddr_in*>(&m_addr);
            return ntohs(addr4->sin_port);
        } else {
            const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(&m_addr);
            return ntohs(addr6->sin6_port);
        }
    }

    // 获取 sockaddr 指针（用于系统调用）
    sockaddr* sockAddr() { return reinterpret_cast<sockaddr*>(&m_addr); }
    const sockaddr* sockAddr() const { return reinterpret_cast<const sockaddr*>(&m_addr); }
    socklen_t* addrLen() { return &m_addr_len; }
    socklen_t addrLen() const { return m_addr_len; }
};

}

#endif
