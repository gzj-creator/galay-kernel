/**
 * @file Awaitable.h
 * @brief 异步IO可等待对象
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details 定义各种异步IO操作的Awaitable类型，包括：
 * - AcceptAwaitable: 接受连接
 * - ConnectAwaitable: 建立连接
 * - RecvAwaitable: 接收数据
 * - SendAwaitable: 发送数据
 * - CloseAwaitable: 关闭连接
 *
 * 这些类型实现了C++20 Awaitable接口，可以在协程中使用co_await。
 * 所有 Awaitable 都支持超时：
 * @code
 * auto result = co_await socket.recv(buffer, size).timeout(5s);
 * @endcode
 *
 * @note 这些类型由TcpSocket内部创建，用户通常不需要直接使用
 */

#ifndef GALAY_KERNEL_AWAITABLE_H
#define GALAY_KERNEL_AWAITABLE_H

#include "galay-kernel/common/Defn.hpp"
#include "galay-kernel/common/Error.h"
#include "galay-kernel/common/Bytes.h"
#include "galay-kernel/common/Host.hpp"
#include "Timeout.hpp"
#include "Waker.h"
#include <coroutine>
#include <cstddef>
#include <expected>
#include <vector>
#include <sys/uio.h>

#ifdef USE_EPOLL
#include <libaio.h>
#endif

namespace galay::kernel
{

struct AwaitableBase {
#ifdef USE_IOURING
    IOEventType m_sqe_type = IOEventType::INVALID;
#endif
};

struct IOController;

/**
 * @brief Accept操作的可等待对象
 *
 * @details 用于异步接受新的TCP连接。
 * co_await后返回新连接的句柄或错误。
 *
 * @note 由TcpSocket::accept()创建
 */
struct AcceptAwaitable: public AwaitableBase, public TimeoutSupport<AcceptAwaitable> {
    /**
     * @brief 独立的 ready 操作
     * @return 始终返回false，需要异步等待
     */
    static bool AcceptActionReady() { return false; }

    /**
     * @brief 独立的 suspend 操作
     * @param awaitable Awaitable对象指针
     * @param controller IO控制器
     * @param waker 协程唤醒器
     * @param result 结果输出
     * @return true表示挂起，false表示立即完成
     */
    static bool AcceptActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<GHandle, IOError>& result);

    /**
     * @brief 独立的 resume 操作（仅执行清理）
     * @param controller IO控制器
     */
    static void AcceptActionResume(IOController* controller);

    /**
     * @brief 构造函数
     * @param controller IO控制器
     * @param host 输出参数，接收客户端地址
     */
    AcceptAwaitable(IOController* controller, Host* host)
        :m_host(host), m_controller(controller) {}

    /**
     * @brief 构造函数
     * @param result 异步操作结果
     * @return true 唤醒，false继续监听
     */
    bool handleComplete(std::expected<GHandle, IOError>&& result);

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false，需要异步等待
     */
    bool await_ready() { return AcceptActionReady(); }

    /**
     * @brief 挂起协程并注册IO事件
     * @param handle 当前协程句柄
     * @return true表示挂起，false表示立即完成
     */
    bool await_suspend(std::coroutine_handle<> handle);

    /**
     * @brief 恢复时获取结果
     * @return 成功返回新连接句柄，失败返回IOError
     */
    std::expected<GHandle, IOError> await_resume();


    Host* m_host;                                ///< 客户端地址输出
    Waker m_waker;                               ///< 协程唤醒器
    IOController* m_controller;                  ///< IO控制器
    std::expected<GHandle, IOError> m_result;    ///< 操作结果
};

/**
 * @brief Recv操作的可等待对象
 *
 * @details 用于异步接收TCP数据。
 * co_await后返回接收到的数据或错误。
 *
 * @note 由TcpSocket::recv()创建
 */
struct RecvAwaitable: public AwaitableBase, public TimeoutSupport<RecvAwaitable> {
    /**
     * @brief 独立的 ready 操作
     * @return 始终返回false
     */
    static bool RecvActionReady() { return false; }

    /**
     * @brief 独立的 suspend 操作
     * @param awaitable Awaitable对象指针
     * @param controller IO控制器
     * @param waker 协程唤醒器
     * @param result 结果输出
     * @return true表示挂起，false表示立即完成
     */
    static bool RecvActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<Bytes, IOError>& result);

    /**
     * @brief 独立的 resume 操作
     * @param controller IO控制器
     */
    static void RecvActionResume(IOController* controller);

    /**
     * @brief 构造函数
     * @param controller IO控制器
     * @param buffer 接收缓冲区
     * @param length 缓冲区大小
     */
    RecvAwaitable(IOController* controller, char* buffer, size_t length)
        : m_controller(controller), m_buffer(buffer), m_length(length) {}

    /**
     * @brief 处理完成回调
     * @param result 异步操作结果
     * @return true 唤醒，false继续监听
     */
    bool handleComplete(std::expected<Bytes, IOError>&& result) {
        m_result = std::move(result);
        return true;
    }

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false
     */
    bool await_ready() { return RecvActionReady(); }

    /**
     * @brief 挂起协程并注册IO事件
     * @param handle 当前协程句柄
     * @return true表示挂起，false表示立即完成
     */
    bool await_suspend(std::coroutine_handle<> handle);

    /**
     * @brief 恢复时获取结果
     * @return 成功返回Bytes数据，失败返回IOError
     */
    std::expected<Bytes, IOError> await_resume();

    IOController* m_controller;                ///< IO控制器
    char* m_buffer;                            ///< 接收缓冲区
    size_t m_length;                           ///< 缓冲区大小
    Waker m_waker;                             ///< 协程唤醒器
    std::expected<Bytes, IOError> m_result;    ///< 操作结果
};

/**
 * @brief Send操作的可等待对象
 *
 * @details 用于异步发送TCP数据。
 * co_await后返回实际发送的字节数或错误。
 *
 * @note 由TcpSocket::send()创建
 */
struct SendAwaitable: public AwaitableBase, public TimeoutSupport<SendAwaitable> {
    /**
     * @brief 独立的 ready 操作
     * @return 始终返回false，需要异步等待
     */
    static bool SendActionReady() { return false; }

    /**
     * @brief 独立的 suspend 操作
     * @param awaitable Awaitable对象指针
     * @param controller IO控制器
     * @param waker 协程唤醒器
     * @param result 结果输出
     * @return true表示挂起，false表示立即完成
     */
    static bool SendActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<size_t, IOError>& result);

    /**
     * @brief 独立的 resume 操作
     * @param controller IO控制器
     */
    static void SendActionResume(IOController* controller);

    /**
     * @brief 构造函数
     * @param controller IO控制器
     * @param buffer 发送数据
     * @param length 数据长度
     */
    SendAwaitable(IOController* controller, const char* buffer, size_t length)
        : m_controller(controller), m_buffer(buffer), m_length(length) {}

    /**
     * @brief 处理完成回调
     * @param result 异步操作结果
     * @return true 唤醒，false继续监听
     */
    bool handleComplete(std::expected<size_t, IOError>&& result) {
        m_result = std::move(result);
        return true;
    }

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false
     */
    bool await_ready() { return SendActionReady(); }

    /**
     * @brief 挂起协程并注册IO事件
     * @param handle 当前协程句柄
     * @return true表示挂起，false表示立即完成
     */
    bool await_suspend(std::coroutine_handle<> handle);

    /**
     * @brief 恢复时获取结果
     * @return 成功返回发送字节数，失败返回IOError
     */
    std::expected<size_t, IOError> await_resume();

    IOController* m_controller;                 ///< IO控制器
    const char* m_buffer;                       ///< 发送数据
    size_t m_length;                            ///< 数据长度
    Waker m_waker;                              ///< 协程唤醒器
    std::expected<size_t, IOError> m_result;    ///< 操作结果
};

/**
 * @brief Readv操作的可等待对象
 *
 * @details 用于异步接收TCP数据，支持 scatter-gather IO。
 * 使用 readv 系统调用一次读取到多个缓冲区。
 * co_await后返回接收到的总字节数或错误。
 *
 * @note 适用于需要将数据分散读取到多个缓冲区的场景
 */
struct ReadvAwaitable: public AwaitableBase, public TimeoutSupport<ReadvAwaitable> {
    /**
     * @brief 独立的 ready 操作
     * @return 始终返回false，需要异步等待
     */
    static bool ReadvActionReady() { return false; }

    /**
     * @brief 独立的 suspend 操作
     * @param awaitable Awaitable对象指针
     * @param controller IO控制器
     * @param waker 协程唤醒器
     * @param result 结果输出
     * @return true表示挂起，false表示立即完成
     */
    static bool ReadvActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<size_t, IOError>& result);

    /**
     * @brief 独立的 resume 操作
     * @param controller IO控制器
     */
    static void ReadvActionResume(IOController* controller);

    /**
     * @brief 构造函数
     * @param controller IO控制器
     * @param iovecs iovec 向量，描述多个缓冲区
     */
    ReadvAwaitable(IOController* controller, std::vector<struct iovec> iovecs)
        : m_controller(controller), m_iovecs(std::move(iovecs)) {}

    /**
     * @brief 处理完成回调
     * @param result 异步操作结果
     * @return true 唤醒，false继续监听
     */
    bool handleComplete(std::expected<size_t, IOError>&& result) {
        m_result = std::move(result);
        return true;
    }

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false
     */
    bool await_ready() { return ReadvActionReady(); }

    /**
     * @brief 挂起协程并注册IO事件
     * @param handle 当前协程句柄
     * @return true表示挂起，false表示立即完成
     */
    bool await_suspend(std::coroutine_handle<> handle);

    /**
     * @brief 恢复时获取结果
     * @return 成功返回读取的总字节数，失败返回IOError
     */
    std::expected<size_t, IOError> await_resume();

    IOController* m_controller;                 ///< IO控制器
    std::vector<struct iovec> m_iovecs;         ///< iovec 向量
    Waker m_waker;                              ///< 协程唤醒器
    std::expected<size_t, IOError> m_result;    ///< 操作结果
};

/**
 * @brief Writev操作的可等待对象
 *
 * @details 用于异步发送TCP数据，支持 scatter-gather IO。
 * 使用 writev 系统调用一次发送多个缓冲区的数据。
 * co_await后返回发送的总字节数或错误。
 *
 * @note 适用于需要将多个缓冲区的数据聚合发送的场景
 */
struct WritevAwaitable: public AwaitableBase, public TimeoutSupport<WritevAwaitable> {
    /**
     * @brief 独立的 ready 操作
     * @return 始终返回false，需要异步等待
     */
    static bool WritevActionReady() { return false; }

    /**
     * @brief 独立的 suspend 操作
     * @param awaitable Awaitable对象指针
     * @param controller IO控制器
     * @param waker 协程唤醒器
     * @param result 结果输出
     * @return true表示挂起，false表示立即完成
     */
    static bool WritevActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<size_t, IOError>& result);

    /**
     * @brief 独立的 resume 操作
     * @param controller IO控制器
     */
    static void WritevActionResume(IOController* controller);

    /**
     * @brief 构造函数
     * @param controller IO控制器
     * @param iovecs iovec 向量，描述多个缓冲区
     */
    WritevAwaitable(IOController* controller, std::vector<struct iovec> iovecs)
        : m_controller(controller), m_iovecs(std::move(iovecs)) {}

    /**
     * @brief 处理完成回调
     * @param result 异步操作结果
     * @return true 唤醒，false继续监听
     */
    bool handleComplete(std::expected<size_t, IOError>&& result) {
        m_result = std::move(result);
        return true;
    }

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false
     */
    bool await_ready() { return WritevActionReady(); }

    /**
     * @brief 挂起协程并注册IO事件
     * @param handle 当前协程句柄
     * @return true表示挂起，false表示立即完成
     */
    bool await_suspend(std::coroutine_handle<> handle);

    /**
     * @brief 恢复时获取结果
     * @return 成功返回发送的总字节数，失败返回IOError
     */
    std::expected<size_t, IOError> await_resume();

    IOController* m_controller;                 ///< IO控制器
    std::vector<struct iovec> m_iovecs;         ///< iovec 向量
    Waker m_waker;                              ///< 协程唤醒器
    std::expected<size_t, IOError> m_result;    ///< 操作结果
};

/**
 * @brief Connect操作的可等待对象
 *
 * @details 用于异步建立TCP连接。
 * co_await后返回连接结果。
 *
 * @note 由TcpSocket::connect()创建
 */
struct ConnectAwaitable: public AwaitableBase, public TimeoutSupport<ConnectAwaitable> {
    /**
     * @brief 独立的 ready 操作
     * @return 始终返回false，需要异步等待
     */
    static bool ConnectActionReady() { return false; }

    /**
     * @brief 独立的 suspend 操作
     * @param awaitable Awaitable对象指针
     * @param controller IO控制器
     * @param waker 协程唤醒器
     * @param result 结果输出
     * @return true表示挂起，false表示立即完成
     */
    static bool ConnectActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<void, IOError>& result);

    /**
     * @brief 独立的 resume 操作
     * @param controller IO控制器
     */
    static void ConnectActionResume(IOController* controller);

    /**
     * @brief 构造函数
     * @param controller IO控制器
     * @param host 目标服务器地址
     */
    ConnectAwaitable(IOController* controller, const Host& host)
        : m_controller(controller), m_host(host) {}

    /**
     * @brief 处理完成回调
     * @param result 异步操作结果
     * @return true 唤醒，false继续监听
     */
    bool handleComplete(std::expected<void, IOError>&& result) {
        m_result = std::move(result);
        return true;
    }

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false
     */
    bool await_ready() { return ConnectActionReady(); }

    /**
     * @brief 挂起协程并注册IO事件
     * @param handle 当前协程句柄
     * @return true表示挂起，false表示立即完成
     */
    bool await_suspend(std::coroutine_handle<> handle);

    /**
     * @brief 恢复时获取结果
     * @return 成功返回void，失败返回IOError
     */
    std::expected<void, IOError> await_resume();

    IOController* m_controller;                ///< IO控制器
    Host m_host;                               ///< 目标地址
    Waker m_waker;                             ///< 协程唤醒器
    std::expected<void, IOError> m_result;     ///< 操作结果
};

/**
 * @brief Close操作的可等待对象
 *
 * @details 用于异步关闭socket。
 * co_await后返回关闭结果。
 *
 * @note 由TcpSocket::close()创建
 */
struct CloseAwaitable: public AwaitableBase, public TimeoutSupport<CloseAwaitable> {
    /**
     * @brief 构造函数
     * @param controller IO控制器
     */
    CloseAwaitable(IOController* controller)
        : m_controller(controller) {}

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false
     */
    bool await_ready() { return false; }

    /**
     * @brief 挂起协程并执行关闭
     * @param handle 当前协程句柄
     * @return 始终返回false（立即完成）
     */
    bool await_suspend(std::coroutine_handle<> handle);

    /**
     * @brief 恢复时获取结果
     * @return 成功返回void，失败返回IOError
     */
    std::expected<void, IOError> await_resume();

    IOController* m_controller;                  ///< IO控制器
    Waker m_waker;                             ///< 协程唤醒器
    std::expected<void, IOError> m_result;     ///< 操作结果
};

/**
 * @brief RecvFrom操作的可等待对象
 *
 * @details 用于异步接收UDP数据报。
 * co_await后返回接收到的数据和发送方地址或错误。
 *
 * @note 由UdpSocket::recvfrom()创建
 */
struct RecvFromAwaitable: public AwaitableBase, public TimeoutSupport<RecvFromAwaitable> {
    /**
     * @brief 独立的 ready 操作
     * @return 始终返回false，需要异步等待
     */
    static bool RecvFromActionReady() { return false; }

    /**
     * @brief 独立的 suspend 操作
     * @param awaitable Awaitable对象指针
     * @param controller IO控制器
     * @param waker 协程唤醒器
     * @param result 结果输出
     * @return true表示挂起，false表示立即完成
     */
    static bool RecvFromActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<Bytes, IOError>& result);

    /**
     * @brief 独立的 resume 操作
     * @param controller IO控制器
     */
    static void RecvFromActionResume(IOController* controller);

    /**
     * @brief 构造函数
     * @param controller IO控制器
     * @param buffer 接收缓冲区
     * @param length 缓冲区大小
     * @param from 输出参数，接收发送方地址
     */
    RecvFromAwaitable(IOController* controller, char* buffer, size_t length, Host* from)
        : m_controller(controller), m_buffer(buffer), m_length(length), m_from(from) {}

    /**
     * @brief 处理完成回调
     * @param result 异步操作结果
     * @return true 唤醒，false继续监听
     */
    bool handleComplete(std::expected<Bytes, IOError>&& result) {
        m_result = std::move(result);
        return true;
    }

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false
     */
    bool await_ready() { return RecvFromActionReady(); }

    /**
     * @brief 挂起协程并注册IO事件
     * @param handle 当前协程句柄
     * @return true表示挂起，false表示立即完成
     */
    bool await_suspend(std::coroutine_handle<> handle);

    /**
     * @brief 恢复时获取结果
     * @return 成功返回Bytes数据，失败返回IOError
     */
    std::expected<Bytes, IOError> await_resume();

    IOController* m_controller;                ///< IO控制器
    char* m_buffer;                            ///< 接收缓冲区
    size_t m_length;                           ///< 缓冲区大小
    Host* m_from;                              ///< 发送方地址输出
    Waker m_waker;                             ///< 协程唤醒器
    std::expected<Bytes, IOError> m_result;    ///< 操作结果

#ifdef USE_IOURING
    // io_uring 需要的持久化结构体
    struct msghdr m_msg;                       ///< msghdr 结构体（io_uring 使用）
    struct iovec m_iov;                        ///< iovec 结构体（io_uring 使用）
    sockaddr_storage m_addr;                   ///< 地址存储（io_uring 使用）
#endif
};

/**
 * @brief SendTo操作的可等待对象
 *
 * @details 用于异步发送UDP数据报。
 * co_await后返回实际发送的字节数或错误。
 *
 * @note 由UdpSocket::sendto()创建
 */
struct SendToAwaitable: public AwaitableBase, public TimeoutSupport<SendToAwaitable> {
    /**
     * @brief 独立的 ready 操作
     * @return 始终返回false，需要异步等待
     */
    static bool SendToActionReady() { return false; }

    /**
     * @brief 独立的 suspend 操作
     * @param awaitable Awaitable对象指针
     * @param controller IO控制器
     * @param waker 协程唤醒器
     * @param result 结果输出
     * @return true表示挂起，false表示立即完成
     */
    static bool SendToActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<size_t, IOError>& result);

    /**
     * @brief 独立的 resume 操作
     * @param controller IO控制器
     */
    static void SendToActionResume(IOController* controller);

    /**
     * @brief 构造函数
     * @param controller IO控制器
     * @param buffer 发送数据
     * @param length 数据长度
     * @param to 目标地址
     */
    SendToAwaitable(IOController* controller, const char* buffer, size_t length, const Host& to)
        : m_controller(controller), m_buffer(buffer), m_length(length), m_to(to) {}

    /**
     * @brief 处理完成回调
     * @param result 异步操作结果
     * @return true 唤醒，false继续监听
     */
    bool handleComplete(std::expected<size_t, IOError>&& result) {
        m_result = std::move(result);
        return true;
    }

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false
     */
    bool await_ready() { return SendToActionReady(); }

    /**
     * @brief 挂起协程并注册IO事件
     * @param handle 当前协程句柄
     * @return true表示挂起，false表示立即完成
     */
    bool await_suspend(std::coroutine_handle<> handle);

    /**
     * @brief 恢复时获取结果
     * @return 成功返回发送字节数，失败返回IOError
     */
    std::expected<size_t, IOError> await_resume();

    IOController* m_controller;                 ///< IO控制器
    const char* m_buffer;                       ///< 发送数据
    size_t m_length;                            ///< 数据长度
    Host m_to;                                  ///< 目标地址
    Waker m_waker;                              ///< 协程唤醒器
    std::expected<size_t, IOError> m_result;    ///< 操作结果

#ifdef USE_IOURING
    // io_uring 需要的持久化结构体
    struct msghdr m_msg;                        ///< msghdr 结构体（io_uring 使用）
    struct iovec m_iov;                         ///< iovec 结构体（io_uring 使用）
#endif
};

/**
 * @brief 文件读取操作的可等待对象
 *
 * @details 用于异步读取文件数据。
 * co_await后返回读取到的数据或错误。
 *
 * @note 由AsyncFile::read()创建
 */
struct FileReadAwaitable: public AwaitableBase, public TimeoutSupport<FileReadAwaitable> {
    /**
     * @brief 独立的 ready 操作
     * @return 始终返回false，需要异步等待
     */
    static bool FileReadActionReady() { return false; }

    /**
     * @brief 独立的 suspend 操作
     * @param awaitable Awaitable对象指针
     * @param controller IO控制器
     * @param waker 协程唤醒器
     * @param result 结果输出
     * @return true表示挂起，false表示立即完成
     */
    static bool FileReadActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<Bytes, IOError>& result);

    /**
     * @brief 独立的 resume 操作
     * @param controller IO控制器
     */
    static void FileReadActionResume(IOController* controller);

#ifdef USE_EPOLL
    // epoll 平台：需要 eventfd 和 libaio context
    FileReadAwaitable(IOController* controller,
                      char* buffer, size_t length, off_t offset,
                      int event_fd, io_context_t aio_ctx, size_t expect_count = 1)
        : m_controller(controller),
          m_buffer(buffer), m_length(length), m_offset(offset),
          m_event_fd(event_fd), m_aio_ctx(aio_ctx), m_expect_count(expect_count) {}
#else
    FileReadAwaitable(IOController* controller,
                      char* buffer, size_t length, off_t offset)
        : m_controller(controller),
          m_buffer(buffer), m_length(length), m_offset(offset) {}
#endif

    /**
     * @brief 处理完成回调
     * @param result 异步操作结果
     * @return true 唤醒，false继续监听
     */
    bool handleComplete(std::expected<Bytes, IOError>&& result) {
        m_result = std::move(result);
        return true;
    }

    bool await_ready() { return FileReadActionReady(); }
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<Bytes, IOError> await_resume();

    IOController* m_controller;
    char* m_buffer;
    size_t m_length;
    off_t m_offset;
    Waker m_waker;
    std::expected<Bytes, IOError> m_result;

#ifdef USE_EPOLL
    int m_event_fd;
    io_context_t m_aio_ctx;
    size_t m_expect_count;      ///< 期望完成的事件数量
    size_t m_finished_count{0}; ///< 已完成的事件数量
#endif
};

/**
 * @brief 文件写入操作的可等待对象
 *
 * @details 用于异步写入文件数据。
 * co_await后返回写入的字节数或错误。
 *
 * @note 由AsyncFile::write()创建
 */
struct FileWriteAwaitable: public AwaitableBase, public TimeoutSupport<FileWriteAwaitable> {
    /**
     * @brief 独立的 ready 操作
     * @return 始终返回false，需要异步等待
     */
    static bool FileWriteActionReady() { return false; }

    /**
     * @brief 独立的 suspend 操作
     * @param awaitable Awaitable对象指针
     * @param controller IO控制器
     * @param waker 协程唤醒器
     * @param result 结果输出
     * @return true表示挂起，false表示立即完成
     */
    static bool FileWriteActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<size_t, IOError>& result);

    /**
     * @brief 独立的 resume 操作
     * @param controller IO控制器
     */
    static void FileWriteActionResume(IOController* controller);

#ifdef USE_EPOLL
    // epoll 平台：需要 eventfd 和 libaio context
    FileWriteAwaitable(IOController* controller,
                       const char* buffer, size_t length, off_t offset,
                       int event_fd, io_context_t aio_ctx, size_t expect_count = 1)
        : m_controller(controller),
          m_buffer(buffer), m_length(length), m_offset(offset),
          m_event_fd(event_fd), m_aio_ctx(aio_ctx), m_expect_count(expect_count) {}
#else
    FileWriteAwaitable(IOController* controller,
                       const char* buffer, size_t length, off_t offset)
        : m_controller(controller),
          m_buffer(buffer), m_length(length), m_offset(offset) {}
#endif

    /**
     * @brief 处理完成回调
     * @param result 异步操作结果
     * @return true 唤醒，false继续监听
     */
    bool handleComplete(std::expected<size_t, IOError>&& result) {
        m_result = std::move(result);
        return true;
    }

    bool await_ready() { return FileWriteActionReady(); }
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<size_t, IOError> await_resume();

    IOController* m_controller;
    const char* m_buffer;
    size_t m_length;
    off_t m_offset;
    Waker m_waker;
    std::expected<size_t, IOError> m_result;

#ifdef USE_EPOLL
    int m_event_fd;
    io_context_t m_aio_ctx;
    size_t m_expect_count;      ///< 期望完成的事件数量
    size_t m_finished_count{0}; ///< 已完成的事件数量
#endif
};

/**
 * @brief 文件监控事件类型
 */
enum class FileWatchEvent : uint32_t {
    None        = 0,
    Access      = 0x00000001,  ///< 文件被访问
    Modify      = 0x00000002,  ///< 文件被修改
    Attrib      = 0x00000004,  ///< 文件属性变化
    CloseWrite  = 0x00000008,  ///< 可写文件关闭
    CloseNoWrite= 0x00000010,  ///< 不可写文件关闭
    Open        = 0x00000020,  ///< 文件被打开
    MovedFrom   = 0x00000040,  ///< 文件被移出
    MovedTo     = 0x00000080,  ///< 文件被移入
    Create      = 0x00000100,  ///< 文件被创建
    Delete      = 0x00000200,  ///< 文件被删除
    DeleteSelf  = 0x00000400,  ///< 监控目标被删除
    MoveSelf    = 0x00000800,  ///< 监控目标被移动
    All         = 0x00000FFF,  ///< 所有事件
};

inline FileWatchEvent operator|(FileWatchEvent a, FileWatchEvent b) {
    return static_cast<FileWatchEvent>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline FileWatchEvent operator&(FileWatchEvent a, FileWatchEvent b) {
    return static_cast<FileWatchEvent>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/**
 * @brief 文件监控结果
 */
struct FileWatchResult {
    FileWatchEvent event;       ///< 触发的事件类型
    std::string name;           ///< 相关文件名（目录监控时有效）
    bool isDir;                 ///< 是否是目录

    /**
     * @brief 检查是否包含指定事件
     * @param check 要检查的事件类型
     * @return 是否包含该事件
     */
    bool has(FileWatchEvent check) const {
        return (static_cast<uint32_t>(event) & static_cast<uint32_t>(check)) != 0;
    }
};

/**
 * @brief 文件监控操作的可等待对象
 *
 * @details 用于异步监控文件或目录变化。
 * co_await后返回触发的事件或错误。
 *
 * @note 由FileWatcher::watch()创建
 */
struct FileWatchAwaitable: public AwaitableBase, public TimeoutSupport<FileWatchAwaitable> {
    /**
     * @brief 独立的 ready 操作
     * @return 始终返回false，需要异步等待
     */
    static bool FileWatchActionReady() { return false; }

    /**
     * @brief 独立的 suspend 操作
     * @param awaitable Awaitable对象指针
     * @param controller IO控制器
     * @param waker 协程唤醒器
     * @param result 结果输出
     * @return true表示挂起，false表示立即完成
     */
    static bool FileWatchActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<FileWatchResult, IOError>& result);

    /**
     * @brief 独立的 resume 操作
     * @param controller IO控制器
     */
    static void FileWatchActionResume(IOController* controller);

#ifdef USE_KQUEUE
    /**
     * @brief 构造函数 (kqueue)
     * @param controller IO控制器
     * @param buffer 事件缓冲区
     * @param buffer_size 缓冲区大小
     * @param events 要监控的事件类型
     */
    FileWatchAwaitable(IOController* controller,
                       char* buffer, size_t buffer_size,
                       FileWatchEvent events)
        : m_controller(controller),
          m_buffer(buffer), m_buffer_size(buffer_size),
          m_events(events) {}
#else
    /**
     * @brief 构造函数 (Linux)
     * @param controller IO控制器
     * @param buffer 事件缓冲区
     * @param buffer_size 缓冲区大小
     */
    FileWatchAwaitable(IOController* controller,
                       char* buffer, size_t buffer_size)
        : m_controller(controller),
          m_buffer(buffer), m_buffer_size(buffer_size) {}
#endif

    /**
     * @brief 处理完成回调
     * @param result 异步操作结果
     * @return true 唤醒，false继续监听
     */
    bool handleComplete(std::expected<FileWatchResult, IOError>&& result) {
        m_result = std::move(result);
        return true;
    }

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false
     */
    bool await_ready() { return FileWatchActionReady(); }

    /**
     * @brief 挂起协程并注册IO事件
     * @param handle 当前协程句柄
     * @return true表示挂起，false表示立即完成
     */
    bool await_suspend(std::coroutine_handle<> handle);

    /**
     * @brief 恢复时获取结果
     * @return 成功返回FileWatchResult，失败返回IOError
     */
    std::expected<FileWatchResult, IOError> await_resume();

    IOController* m_controller;                         ///< IO控制器
    char* m_buffer;                                     ///< 事件缓冲区
    size_t m_buffer_size;                               ///< 缓冲区大小
#ifdef USE_KQUEUE
    FileWatchEvent m_events;                            ///< 要监控的事件类型
#endif
    Waker m_waker;                                      ///< 协程唤醒器
    std::expected<FileWatchResult, IOError> m_result;   ///< 操作结果
};

/**
 * @brief Recv通知操作的可等待对象
 *
 * @details 仅等待fd可读，不执行实际IO操作。
 * 用于SSL等需要自定义IO处理的场景。
 * co_await后协程被唤醒，由调用者自己执行IO操作。
 *
 * @note 事件就绪时只唤醒协程，不会调用recv()
 */
struct RecvNotifyAwaitable: public AwaitableBase, public TimeoutSupport<RecvNotifyAwaitable> {
    /**
     * @brief 独立的 ready 操作
     * @return 始终返回false，需要异步等待
     */
    static bool RecvNotifyActionReady() { return false; }

    /**
     * @brief 独立的 suspend 操作
     * @param awaitable Awaitable对象指针
     * @param controller IO控制器
     * @param waker 协程唤醒器
     * @return true表示挂起，false表示立即完成
     */
    static bool RecvNotifyActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker);

    /**
     * @brief 独立的 resume 操作
     * @param controller IO控制器
     */
    static void RecvNotifyActionResume(IOController* controller);

    /**
     * @brief 构造函数
     * @param controller IO控制器
     */
    RecvNotifyAwaitable(IOController* controller)
        : m_controller(controller) {}

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false
     */
    bool await_ready() { return RecvNotifyActionReady(); }

    /**
     * @brief 挂起协程并注册IO事件
     * @param handle 当前协程句柄
     * @return true表示挂起，false表示立即完成
     */
    bool await_suspend(std::coroutine_handle<> handle);

    /**
     * @brief 恢复时返回
     * @return void（调用者需要自己执行IO操作）
     */
    void await_resume();

    IOController* m_controller;                ///< IO控制器
    Waker m_waker;                             ///< 协程唤醒器
};

/**
 * @brief Send通知操作的可等待对象
 *
 * @details 仅等待fd可写，不执行实际IO操作。
 * 用于SSL等需要自定义IO处理的场景。
 * co_await后协程被唤醒，由调用者自己执行IO操作。
 *
 * @note 事件就绪时只唤醒协程，不会调用send()
 */
struct SendNotifyAwaitable: public AwaitableBase, public TimeoutSupport<SendNotifyAwaitable> {
    /**
     * @brief 独立的 ready 操作
     * @return 始终返回false，需要异步等待
     */
    static bool SendNotifyActionReady() { return false; }

    /**
     * @brief 独立的 suspend 操作
     * @param awaitable Awaitable对象指针
     * @param controller IO控制器
     * @param waker 协程唤醒器
     * @return true表示挂起，false表示立即完成
     */
    static bool SendNotifyActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker);

    /**
     * @brief 独立的 resume 操作
     * @param controller IO控制器
     */
    static void SendNotifyActionResume(IOController* controller);

    /**
     * @brief 构造函数
     * @param controller IO控制器
     */
    SendNotifyAwaitable(IOController* controller)
        : m_controller(controller) {}

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false
     */
    bool await_ready() { return SendNotifyActionReady(); }

    /**
     * @brief 挂起协程并注册IO事件
     * @param handle 当前协程句柄
     * @return true表示挂起，false表示立即完成
     */
    bool await_suspend(std::coroutine_handle<> handle);

    /**
     * @brief 恢复时返回
     * @return void（调用者需要自己执行IO操作）
     */
    void await_resume();

    IOController* m_controller;                ///< IO控制器
    Waker m_waker;                             ///< 协程唤醒器
};

/**
 * @brief SendFile操作的可等待对象
 *
 * @details 用于异步发送文件数据，使用零拷贝技术。
 * 使用 sendfile 系统调用直接在内核空间传输数据，避免用户空间拷贝。
 * co_await后返回实际发送的字节数或错误。
 *
 * @note
 * - 适用于发送大文件的场景，性能优于普通的 read + send
 * - 不同平台的 sendfile 接口略有差异：
 *   - Linux: sendfile(out_fd, in_fd, offset, count)
 *   - macOS: sendfile(in_fd, out_fd, offset, len, hdtr, flags)
 * - offset 会被更新为发送后的位置
 */
struct SendFileAwaitable: public AwaitableBase, public TimeoutSupport<SendFileAwaitable> {
    /**
     * @brief 独立的 ready 操作
     * @return 始终返回false，需要异步等待
     */
    static bool SendFileActionReady() { return false; }

    /**
     * @brief 独立的 suspend 操作
     * @param awaitable Awaitable对象指针
     * @param controller IO控制器
     * @param waker 协程唤醒器
     * @param result 结果输出
     * @return true表示挂起，false表示立即完成
     */
    static bool SendFileActionSuspend(AwaitableBase* awaitable, IOController* controller, Waker& waker, std::expected<size_t, IOError>& result);

    /**
     * @brief 独立的 resume 操作
     * @param controller IO控制器
     */
    static void SendFileActionResume(IOController* controller);

    /**
     * @brief 构造函数
     * @param controller IO控制器（socket的控制器）
     * @param file_fd 要发送的文件描述符
     * @param offset 文件偏移量（发送起始位置）
     * @param count 要发送的字节数
     */
    SendFileAwaitable(IOController* controller, int file_fd, off_t offset, size_t count)
        : m_controller(controller), m_file_fd(file_fd),
          m_offset(offset), m_count(count) {}

    /**
     * @brief 处理完成回调
     * @param result 异步操作结果
     * @return true 唤醒，false继续监听
     */
    bool handleComplete(std::expected<size_t, IOError>&& result) {
        m_result = std::move(result);
        return true;
    }

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false
     */
    bool await_ready() { return SendFileActionReady(); }

    /**
     * @brief 挂起协程并注册IO事件
     * @param handle 当前协程句柄
     * @return true表示挂起，false表示立即完成
     */
    bool await_suspend(std::coroutine_handle<> handle);

    /**
     * @brief 恢复时获取结果
     * @return 成功返回发送字节数，失败返回IOError
     */
    std::expected<size_t, IOError> await_resume();

    IOController* m_controller;                 ///< IO控制器
    int m_file_fd;                              ///< 文件描述符
    off_t m_offset;                             ///< 文件偏移量
    size_t m_count;                             ///< 要发送的字节数
    Waker m_waker;                              ///< 协程唤醒器
    std::expected<size_t, IOError> m_result;    ///< 操作结果
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_AWAITABLE_H
