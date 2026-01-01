/**
 * @file HandleOption.h
 * @brief Socket句柄选项配置
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details 提供对socket句柄的各种选项配置功能，包括：
 * - 阻塞/非阻塞模式设置
 * - 地址重用（SO_REUSEADDR）
 * - 端口重用（SO_REUSEPORT）
 *
 * @example
 * @code
 * TcpSocket socket(scheduler);
 * socket.create();
 *
 * // 链式调用设置选项
 * socket.option().handleReuseAddr();
 * socket.option().handleReusePort();
 * socket.option().handleNonBlock();
 *
 * // 或者检查返回值
 * auto result = socket.option().handleNonBlock();
 * if (!result) {
 *     LogError("Failed to set non-block: {}", result.error().message());
 * }
 * @endcode
 */

#ifndef GALAY_KERNEL_HANDLE_OPTION_H
#define GALAY_KERNEL_HANDLE_OPTION_H

#include "Defn.hpp"
#include "Error.h"
#include <expected>

namespace galay::kernel
{

/**
 * @brief 句柄选项配置类
 *
 * @details 提供对socket句柄的各种选项配置功能。
 * 该类是轻量级的，可以按值传递和返回。
 *
 * @note
 * - 所有方法都是同步的，立即生效
 * - 失败时返回IOError，包含系统错误码
 * - 跨平台支持：Linux/macOS/Windows
 *
 * @see TcpSocket::option()
 */
class HandleOption
{
public:
    /**
     * @brief 构造函数
     * @param handle 要配置的socket句柄
     * @note 不会验证句柄有效性，调用方需确保句柄有效
     */
    explicit HandleOption(GHandle handle);

    /**
     * @brief 设置句柄为阻塞模式
     *
     * @return std::expected<void, IOError> 成功返回void，失败返回IOError
     *
     * @details 阻塞模式下，IO操作会阻塞直到完成或出错。
     * 这是socket的默认模式。
     *
     * @note
     * - Linux/macOS: 使用fcntl清除O_NONBLOCK标志
     * - Windows: 使用ioctlsocket设置FIONBIO为0
     */
    std::expected<void, IOError> handleBlock();

    /**
     * @brief 设置句柄为非阻塞模式
     *
     * @return std::expected<void, IOError> 成功返回void，失败返回IOError
     *
     * @details 非阻塞模式下，IO操作会立即返回，如果无法完成则返回EAGAIN/EWOULDBLOCK。
     * 异步IO必须使用非阻塞模式。
     *
     * @note
     * - Linux/macOS: 使用fcntl设置O_NONBLOCK标志
     * - Windows: 使用ioctlsocket设置FIONBIO为1
     *
     * @code
     * socket.option().handleNonBlock();  // 异步IO前必须调用
     * @endcode
     */
    std::expected<void, IOError> handleNonBlock();

    /**
     * @brief 设置地址重用选项（SO_REUSEADDR）
     *
     * @return std::expected<void, IOError> 成功返回void，失败返回IOError
     *
     * @details 允许绑定到处于TIME_WAIT状态的地址。
     * 服务器重启时通常需要此选项，否则可能因"Address already in use"而绑定失败。
     *
     * @note 建议服务端在bind之前调用
     *
     * @code
     * socket.option().handleReuseAddr();
     * socket.bind(host);  // 即使地址处于TIME_WAIT也能绑定
     * @endcode
     */
    std::expected<void, IOError> handleReuseAddr();

    /**
     * @brief 设置端口重用选项（SO_REUSEPORT）
     *
     * @return std::expected<void, IOError> 成功返回void，失败返回IOError
     *
     * @details 允许多个socket绑定到同一端口。
     * 可用于实现多进程/多线程负载均衡。
     *
     * @note
     * - Linux 3.9+/macOS支持
     * - Windows不支持，会回退到SO_REUSEADDR
     * - 所有绑定到同一端口的socket都必须设置此选项
     *
     * @code
     * // 多进程负载均衡
     * socket.option().handleReusePort();
     * socket.bind(Host(IPType::IPV4, "0.0.0.0", 8080));
     * @endcode
     */
    std::expected<void, IOError> handleReusePort();

private:
    GHandle m_handle;  ///< 要配置的socket句柄
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_HANDLE_OPTION_H
