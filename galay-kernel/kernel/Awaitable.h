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
 *
 * @note 这些类型由TcpSocket内部创建，用户通常不需要直接使用
 */

#ifndef GALAY_KERNEL_AWAITABLE_H
#define GALAY_KERNEL_AWAITABLE_H

#include "galay-kernel/common/Defn.hpp"
#include "galay-kernel/common/Error.h"
#include "galay-kernel/common/Bytes.h"
#include "galay-kernel/common/Host.hpp"
#include "Scheduler.h"
#include "Waker.h"
#include <coroutine>
#include <cstddef>
#include <expected>

#ifdef USE_EPOLL
#include <libaio.h>
#endif

namespace galay::kernel
{

class IOScheduler;

/**
 * @brief Accept操作的可等待对象
 *
 * @details 用于异步接受新的TCP连接。
 * co_await后返回新连接的句柄或错误。
 *
 * @note 由TcpSocket::accept()创建
 */
struct AcceptAwaitable {
    /**
     * @brief 构造函数
     * @param scheduler IO调度器
     * @param controller IO控制器
     * @param listen_handle 监听socket句柄
     * @param host 输出参数，接收客户端地址
     */
    AcceptAwaitable(IOScheduler* scheduler, IOController* controller, GHandle listen_handle, Host* host)
        : m_scheduler(scheduler), m_listen_handle(listen_handle), m_host(host), m_controller(controller) {}

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false，需要异步等待
     */
    bool await_ready() { return false; }

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
    std::expected<GHandle, IOError> await_resume() {
        m_controller->removeAwaitable();
        return std::move(m_result);
    }

    IOScheduler* m_scheduler;                    ///< IO调度器
    GHandle m_listen_handle;                     ///< 监听socket句柄
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
struct RecvAwaitable {
    /**
     * @brief 构造函数
     * @param scheduler IO调度器
     * @param controller IO控制器
     * @param handle socket句柄
     * @param buffer 接收缓冲区
     * @param length 缓冲区大小
     */
    RecvAwaitable(IOScheduler* scheduler, IOController* controller, GHandle handle, char* buffer, size_t length)
        : m_scheduler(scheduler), m_controller(controller), m_handle(handle), m_buffer(buffer), m_length(length) {}

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false
     */
    bool await_ready() { return false; }

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
    std::expected<Bytes, IOError> await_resume() {
        m_controller->removeAwaitable();
        return std::move(m_result);
    }

    IOScheduler* m_scheduler;                  ///< IO调度器
    IOController* m_controller;                ///< IO控制器
    GHandle m_handle;                          ///< socket句柄
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
struct SendAwaitable {
    /**
     * @brief 构造函数
     * @param scheduler IO调度器
     * @param controller IO控制器
     * @param handle socket句柄
     * @param buffer 发送数据
     * @param length 数据长度
     */
    SendAwaitable(IOScheduler* scheduler, IOController* controller, GHandle handle, const char* buffer, size_t length)
        : m_scheduler(scheduler), m_controller(controller), m_handle(handle), m_buffer(buffer), m_length(length) {}

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false
     */
    bool await_ready() { return false; }

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
    std::expected<size_t, IOError> await_resume() {
        m_controller->removeAwaitable();
        return std::move(m_result);
    }

    IOScheduler* m_scheduler;                   ///< IO调度器
    IOController* m_controller;                 ///< IO控制器
    GHandle m_handle;                           ///< socket句柄
    const char* m_buffer;                       ///< 发送数据
    size_t m_length;                            ///< 数据长度
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
struct ConnectAwaitable {
    /**
     * @brief 构造函数
     * @param scheduler IO调度器
     * @param controller IO控制器
     * @param handle socket句柄
     * @param host 目标服务器地址
     */
    ConnectAwaitable(IOScheduler* scheduler, IOController* controller, GHandle handle, const Host& host)
        : m_scheduler(scheduler), m_controller(controller), m_handle(handle), m_host(host) {}

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false
     */
    bool await_ready() { return false; }

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
    std::expected<void, IOError> await_resume() {
        m_controller->removeAwaitable();
        return std::move(m_result);
    }

    IOScheduler* m_scheduler;                  ///< IO调度器
    IOController* m_controller;                ///< IO控制器
    GHandle m_handle;                          ///< socket句柄
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
struct CloseAwaitable {
    /**
     * @brief 构造函数
     * @param scheduler IO调度器
     * @param handle 要关闭的socket句柄
     */
    CloseAwaitable(IOScheduler* scheduler, GHandle handle)
        : m_scheduler(scheduler), m_handle(handle) {}

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
    std::expected<void, IOError> await_resume() { return std::move(m_result); }

    IOScheduler* m_scheduler;                  ///< IO调度器
    GHandle m_handle;                          ///< socket句柄
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
struct RecvFromAwaitable {
    /**
     * @brief 构造函数
     * @param scheduler IO调度器
     * @param controller IO控制器
     * @param handle socket句柄
     * @param buffer 接收缓冲区
     * @param length 缓冲区大小
     * @param from 输出参数，接收发送方地址
     */
    RecvFromAwaitable(IOScheduler* scheduler, IOController* controller, GHandle handle, char* buffer, size_t length, Host* from)
        : m_scheduler(scheduler), m_controller(controller), m_handle(handle), m_buffer(buffer), m_length(length), m_from(from) {}

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false
     */
    bool await_ready() { return false; }

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
    std::expected<Bytes, IOError> await_resume() {
        m_controller->removeAwaitable();
        return std::move(m_result);
    }

    IOScheduler* m_scheduler;                  ///< IO调度器
    IOController* m_controller;                ///< IO控制器
    GHandle m_handle;                          ///< socket句柄
    char* m_buffer;                            ///< 接收缓冲区
    size_t m_length;                           ///< 缓冲区大小
    Host* m_from;                              ///< 发送方地址输出
    Waker m_waker;                             ///< 协程唤醒器
    std::expected<Bytes, IOError> m_result;    ///< 操作结果
};

/**
 * @brief SendTo操作的可等待对象
 *
 * @details 用于异步发送UDP数据报。
 * co_await后返回实际发送的字节数或错误。
 *
 * @note 由UdpSocket::sendto()创建
 */
struct SendToAwaitable {
    /**
     * @brief 构造函数
     * @param scheduler IO调度器
     * @param controller IO控制器
     * @param handle socket句柄
     * @param buffer 发送数据
     * @param length 数据长度
     * @param to 目标地址
     */
    SendToAwaitable(IOScheduler* scheduler, IOController* controller, GHandle handle, const char* buffer, size_t length, const Host& to)
        : m_scheduler(scheduler), m_controller(controller), m_handle(handle), m_buffer(buffer), m_length(length), m_to(to) {}

    /**
     * @brief 检查是否可以立即返回
     * @return 始终返回false
     */
    bool await_ready() { return false; }

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
    std::expected<size_t, IOError> await_resume() {
        m_controller->removeAwaitable();
        return std::move(m_result);
    }

    IOScheduler* m_scheduler;                   ///< IO调度器
    IOController* m_controller;                 ///< IO控制器
    GHandle m_handle;                           ///< socket句柄
    const char* m_buffer;                       ///< 发送数据
    size_t m_length;                            ///< 数据长度
    Host m_to;                                  ///< 目标地址
    Waker m_waker;                              ///< 协程唤醒器
    std::expected<size_t, IOError> m_result;    ///< 操作结果
};

/**
 * @brief 文件读取操作的可等待对象
 *
 * @details 用于异步读取文件数据。
 * co_await后返回读取到的数据或错误。
 *
 * @note 由AsyncFile::read()创建
 */
struct FileReadAwaitable {
#ifdef USE_EPOLL
    // epoll 平台：需要 eventfd 和 libaio context
    FileReadAwaitable(IOScheduler* scheduler, IOController* controller, GHandle handle,
                      char* buffer, size_t length, off_t offset,
                      int event_fd, io_context_t aio_ctx, size_t expect_count = 1)
        : m_scheduler(scheduler), m_controller(controller), m_handle(handle),
          m_buffer(buffer), m_length(length), m_offset(offset),
          m_event_fd(event_fd), m_aio_ctx(aio_ctx), m_expect_count(expect_count) {}
#else
    FileReadAwaitable(IOScheduler* scheduler, IOController* controller, GHandle handle,
                      char* buffer, size_t length, off_t offset)
        : m_scheduler(scheduler), m_controller(controller), m_handle(handle),
          m_buffer(buffer), m_length(length), m_offset(offset) {}
#endif

    bool await_ready() { return false; }
    bool await_suspend(std::coroutine_handle<> handle);

    std::expected<Bytes, IOError> await_resume() {
        m_controller->removeAwaitable();
        return std::move(m_result);
    }

    IOScheduler* m_scheduler;
    IOController* m_controller;
    GHandle m_handle;
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
struct FileWriteAwaitable {
#ifdef USE_EPOLL
    // epoll 平台：需要 eventfd 和 libaio context
    FileWriteAwaitable(IOScheduler* scheduler, IOController* controller, GHandle handle,
                       const char* buffer, size_t length, off_t offset,
                       int event_fd, io_context_t aio_ctx, size_t expect_count = 1)
        : m_scheduler(scheduler), m_controller(controller), m_handle(handle),
          m_buffer(buffer), m_length(length), m_offset(offset),
          m_event_fd(event_fd), m_aio_ctx(aio_ctx), m_expect_count(expect_count) {}
#else
    FileWriteAwaitable(IOScheduler* scheduler, IOController* controller, GHandle handle,
                       const char* buffer, size_t length, off_t offset)
        : m_scheduler(scheduler), m_controller(controller), m_handle(handle),
          m_buffer(buffer), m_length(length), m_offset(offset) {}
#endif

    bool await_ready() { return false; }
    bool await_suspend(std::coroutine_handle<> handle);

    std::expected<size_t, IOError> await_resume() {
        m_controller->removeAwaitable();
        return std::move(m_result);
    }

    IOScheduler* m_scheduler;
    IOController* m_controller;
    GHandle m_handle;
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

} // namespace galay::kernel

#endif // GALAY_KERNEL_AWAITABLE_H
