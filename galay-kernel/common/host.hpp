#ifndef GALAY_KEERNEL_HOST_HPP
#define GALAY_KEERNEL_HOST_HPP

#include "defn.hpp"
#include <cstring>
#include <string>

namespace galay::kernel
{

/**
 * @brief IP 协议版本
 */
enum class IPType : uint8_t {
    IPV4 = 0,  ///< IPv4 地址
    IPV6 = 1,  ///< IPv6 地址
};

/**
 * @brief 套接字地址包装
 * @details 统一封装 IPv4/IPv6 地址、端口和 sockaddr 指针访问接口。
 */
struct Host {
    sockaddr_storage m_addr{};  ///< 底层地址存储
    socklen_t m_addr_len = sizeof(sockaddr_storage);  ///< 当前地址结构实际长度

    Host() {  ///< 构造默认 IPv4 0.0.0.0:0 地址
        std::memset(&m_addr, 0, sizeof(m_addr));
        m_addr.ss_family = AF_INET;
        m_addr_len = sizeof(sockaddr_in);
    }

    Host(IPType proto, const std::string& ip, uint16_t port) {  ///< 按协议版本、IP 和端口构造地址
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
    Host(const sockaddr_in& addr) {  ///< 从 IPv4 sockaddr 构造 Host
        std::memset(&m_addr, 0, sizeof(m_addr));
        std::memcpy(&m_addr, &addr, sizeof(addr));
        m_addr_len = sizeof(sockaddr_in);
    }

    // 从 IPv6 sockaddr 构造
    Host(const sockaddr_in6& addr) {  ///< 从 IPv6 sockaddr 构造 Host
        std::memset(&m_addr, 0, sizeof(m_addr));
        std::memcpy(&m_addr, &addr, sizeof(addr));
        m_addr_len = sizeof(sockaddr_in6);
    }

    // 从通用 sockaddr_storage 构造
    static Host fromSockAddr(const sockaddr_storage& addr) {  ///< 从通用 sockaddr_storage 构造 Host
        Host host;
        std::memcpy(&host.m_addr, &addr, sizeof(addr));
        if (addr.ss_family == AF_INET) {
            host.m_addr_len = sizeof(sockaddr_in);
        } else if (addr.ss_family == AF_INET6) {
            host.m_addr_len = sizeof(sockaddr_in6);
        }
        return host;
    }

    bool isIPv4() const { return m_addr.ss_family == AF_INET; }  ///< 当前是否为 IPv4 地址
    bool isIPv6() const { return m_addr.ss_family == AF_INET6; }  ///< 当前是否为 IPv6 地址

    std::string ip() const {  ///< 返回字符串形式的 IP 地址
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

    uint16_t port() const {  ///< 返回主机字节序端口号
        if (isIPv4()) {
            const sockaddr_in* addr4 = reinterpret_cast<const sockaddr_in*>(&m_addr);
            return ntohs(addr4->sin_port);
        } else {
            const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(&m_addr);
            return ntohs(addr6->sin6_port);
        }
    }

    sockaddr* sockAddr() { return reinterpret_cast<sockaddr*>(&m_addr); }  ///< 返回可写 sockaddr 指针供系统调用使用
    const sockaddr* sockAddr() const { return reinterpret_cast<const sockaddr*>(&m_addr); }  ///< 返回只读 sockaddr 指针
    socklen_t* addrLen() { return &m_addr_len; }  ///< 返回可写长度指针供系统调用更新
    socklen_t addrLen() const { return m_addr_len; }  ///< 返回当前地址结构长度
};

} // namespace galay::kernel

#endif // GALAY_KEERNEL_HOST_HPP
