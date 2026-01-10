/**
 * @file UdpSocket.h
 * @brief 异步UDP Socket封装
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details 提供基于协程的异步UDP Socket操作，包括：
 * - 服务端：create -> bind -> recvfrom/sendto
 * - 客户端：create -> sendto/recvfrom
 *
 * @example
 * @code
 * // 服务端示例
 * Coroutine server(IOScheduler* scheduler) {
 *     UdpSocket socket(scheduler);
 *     socket.create(IPType::IPV4);
 *     socket.option().handleReuseAddr();
 *     socket.option().handleNonBlock();
 *     socket.bind(Host(IPType::IPV4, "0.0.0.0", 8080));
 *
 *     while (true) {
 *         Host clientHost;
 *         char buffer[1024];
 *         auto result = co_await socket.recvfrom(buffer, sizeof(buffer), &clientHost);
 *         if (result) {
 *             // 处理接收到的数据
 *             co_await socket.sendto(buffer, result.value().size(), clientHost);
 *         }
 *     }
 * }
 *
 * // 客户端示例
 * Coroutine client(IOScheduler* scheduler) {
 *     UdpSocket socket(scheduler);
 *     socket.create(IPType::IPV4);
 *     socket.option().handleNonBlock();
 *
 *     Host server(IPType::IPV4, "127.0.0.1", 8080);
 *     co_await socket.sendto("Hello", 5, server);
 *
 *     char buffer[1024];
 *     Host from;
 *     auto result = co_await socket.recvfrom(buffer, sizeof(buffer), &from);
 *
 *     co_await socket.close();
 * }
 * @endcode
 */

#ifndef GALAY_KERNEL_ASYNC_UDP_SOCKET_H
#define GALAY_KERNEL_ASYNC_UDP_SOCKET_H

#include "galay-kernel/common/Defn.hpp"
#include "galay-kernel/common/Error.h"
#include "galay-kernel/common/Host.hpp"
#include "galay-kernel/common/HandleOption.h"
#include "galay-kernel/kernel/Awaitable.h"
#include "galay-kernel/kernel/IOScheduler.hpp"
#include <expected>

namespace galay::async
{

using namespace galay::kernel;

/**
 * @brief 异步UDP Socket类
 *
 * @details 封装底层UDP socket操作，提供协程友好的异步IO接口。
 * 内部包含：
 * - GHandle: 底层socket句柄
 * - IOScheduler*: IO调度器指针
 * - IOController: IO事件控制器
 *
 * @note
 * - 不可拷贝，仅支持移动语义
 * - 析构时不会自动关闭socket，需显式调用close()
 * - 所有异步操作需要在协程中使用co_await
 * - UDP是无连接协议，不需要listen/accept/connect操作
 *
 * @see IOScheduler, HandleOption, Host
 */
class UdpSocket
{
public:
    /**
     * @brief 构造函数，创建Socket
     * @param type IP协议类型（IPV4/IPV6）
     * @note 创建失败会抛出异常
     */
    explicit UdpSocket(IPType type = IPType::IPV4);

    /**
     * @brief 从已有句柄构造Socket
     * @param handle 已有的socket句柄
     * @note 用于包装已存在的socket
     */
    explicit UdpSocket(GHandle handle);

    /**
     * @brief 析构函数
     * @note 不会自动关闭socket，需显式调用close()
     */
    ~UdpSocket();

    /// @brief 禁用拷贝构造
    UdpSocket(const UdpSocket&) = delete;
    /// @brief 禁用拷贝赋值
    UdpSocket& operator=(const UdpSocket&) = delete;

    /**
     * @brief 移动构造函数
     * @param other 被移动的对象，移动后other变为无效状态
     */
    UdpSocket(UdpSocket&& other) noexcept;

    /**
     * @brief 移动赋值运算符
     * @param other 被移动的对象
     * @return 当前对象的引用
     */
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    /**
     * @brief 获取底层socket句柄
     * @return GHandle 底层句柄，可用于底层操作
     */
    GHandle handle() const { return m_controller.m_handle; }

    /**
     * @brief 获取IO控制器指针
     * @return IOController* 内部IO控制器，用于高级操作
     */
    IOController* controller() { return &m_controller; }


    /**
     * @brief 绑定本地地址
     *
     * @param host 要绑定的地址（IP和端口）
     * @return std::expected<void, IOError> 成功返回void，失败返回IOError
     *
     * @note 服务端必须调用，客户端通常不需要
     *
     * @code
     * socket.bind(Host(IPType::IPV4, "0.0.0.0", 8080));
     * @endcode
     */
    std::expected<void, IOError> bind(const Host& host);

    /**
     * @brief 获取句柄选项配置器
     *
     * @return HandleOption 选项配置器对象
     *
     * @code
     * socket.option().handleReuseAddr();  // 设置地址重用
     * socket.option().handleNonBlock();   // 设置非阻塞
     * @endcode
     */
    HandleOption option() { return HandleOption(m_controller.m_handle); }

    /**
     * @brief 异步接收数据报
     *
     * @param buffer 接收缓冲区指针
     * @param length 缓冲区大小
     * @param from 输出参数，接收发送方地址信息，可为nullptr
     * @return RecvFromAwaitable 可等待对象，co_await后返回接收到的Bytes
     *
     * @note
     * - UDP是数据报协议，每次接收一个完整的数据报
     * - 如果数据报大于缓冲区，超出部分会被丢弃
     * - 缓冲区生命周期必须持续到co_await完成
     *
     * @code
     * char buffer[1024];
     * Host from;
     * auto result = co_await socket.recvfrom(buffer, sizeof(buffer), &from);
     * if (result) {
     *     auto& bytes = result.value();
     *     // 处理数据，from包含发送方地址
     * }
     * @endcode
     */
    RecvFromAwaitable recvfrom(char* buffer, size_t length, Host* from);

    /**
     * @brief 异步发送数据报
     *
     * @param buffer 发送数据指针
     * @param length 数据长度
     * @param to 目标地址
     * @return SendToAwaitable 可等待对象，co_await后返回实际发送的字节数
     *
     * @note
     * - UDP是数据报协议，每次发送一个完整的数据报
     * - 缓冲区生命周期必须持续到co_await完成
     * - UDP不保证数据送达，也不保证顺序
     *
     * @code
     * const char* msg = "Hello";
     * Host target(IPType::IPV4, "127.0.0.1", 8080);
     * auto result = co_await socket.sendto(msg, strlen(msg), target);
     * if (result) {
     *     size_t sent = result.value();
     * }
     * @endcode
     */
    SendToAwaitable sendto(const char* buffer, size_t length, const Host& to);

    /**
     * @brief 异步关闭socket
     *
     * @return CloseAwaitable 可等待对象，co_await后返回关闭结果
     *
     * @note 关闭后socket变为无效状态，不可再使用
     *
     * @code
     * co_await socket.close();
     * @endcode
     */
    CloseAwaitable close();
private:
    GHandle create(IPType type);

private:
    IOController m_controller;  ///< IO事件控制器
};

} // namespace galay::async

#endif // GALAY_KERNEL_ASYNC_UDP_SOCKET_H
