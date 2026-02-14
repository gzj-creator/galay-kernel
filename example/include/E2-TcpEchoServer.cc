/**
 * @file E2-TcpEchoServer.cc
 * @brief TCP Echo服务器示例
 * @details 演示如何使用TcpSocket创建一个简单的Echo服务器
 *
 * 使用场景：
 *   - 学习TCP服务器基本用法
 *   - 理解异步IO和协程的配合
 *   - 作为其他TCP服务器的基础模板
 */

#include <iostream>
#include "galay-kernel/async/TcpSocket.h"
#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/common/Log.h"

#ifdef USE_KQUEUE
#include "galay-kernel/kernel/KqueueScheduler.h"
using IOSchedulerType = galay::kernel::KqueueScheduler;
#endif

#ifdef USE_EPOLL
#include "galay-kernel/kernel/EpollScheduler.h"
using IOSchedulerType = galay::kernel::EpollScheduler;
#endif

#ifdef USE_IOURING
#include "galay-kernel/kernel/IOUringScheduler.h"
using IOSchedulerType = galay::kernel::IOUringScheduler;
#endif

using namespace galay::async;
using namespace galay::kernel;

/**
 * @brief Echo服务器协程
 * @details 接收客户端数据并原样返回
 */
Coroutine echoServer() {
    LogInfo("TCP Echo Server starting...");

    // 创建监听socket
    TcpSocket listener;

    // 设置socket选项
    auto optResult = listener.option().handleReuseAddr();
    if (!optResult) {
        LogError("Failed to set reuse addr: {}", optResult.error().message());
        co_return;
    }

    optResult = listener.option().handleNonBlock();
    if (!optResult) {
        LogError("Failed to set non-block: {}", optResult.error().message());
        co_return;
    }

    // 绑定地址
    Host bindHost(IPType::IPV4, "127.0.0.1", 8080);
    auto bindResult = listener.bind(bindHost);
    if (!bindResult) {
        LogError("Failed to bind: {}", bindResult.error().message());
        co_return;
    }

    // 开始监听
    auto listenResult = listener.listen(128);
    if (!listenResult) {
        LogError("Failed to listen: {}", listenResult.error().message());
        co_return;
    }

    LogInfo("Server listening on 127.0.0.1:8080");

    // 接受客户端连接
    Host clientHost;
    auto acceptResult = co_await listener.accept(&clientHost);
    if (!acceptResult) {
        LogError("Failed to accept: {}", acceptResult.error().message());
        co_return;
    }

    LogInfo("Client connected from {}:{}", clientHost.ip(), clientHost.port());

    // 创建客户端socket
    TcpSocket client(acceptResult.value());

    optResult = client.option().handleNonBlock();
    if (!optResult) {
        LogError("Failed to set client non-block: {}", optResult.error().message());
        co_await client.close();
        co_return;
    }

    // Echo循环：接收数据并回显
    char buffer[1024];
    while (true) {
        auto recvResult = co_await client.recv(buffer, sizeof(buffer));
        if (!recvResult) {
            LogError("Recv error: {}", recvResult.error().message());
            break;
        }

        auto& bytes = recvResult.value();
        if (bytes.size() == 0) {
            LogInfo("Client disconnected");
            break;
        }

        LogInfo("Received: {}", bytes.toStringView());

        // 回显数据
        auto sendResult = co_await client.send(bytes.c_str(), bytes.size());
        if (!sendResult) {
            LogError("Send error: {}", sendResult.error().message());
            break;
        }
    }

    co_await client.close();
    co_await listener.close();
    LogInfo("Server stopped");
}

int main() {
    // 创建IO调度器
    IOSchedulerType scheduler;

    // 启动调度器
    scheduler.start();

    // 提交服务器协程
    scheduler.spawn(echoServer());

    // 等待用户输入退出
    std::cout << "Press Enter to stop server..." << std::endl;
    std::cin.get();

    // 停止调度器
    scheduler.stop();

    return 0;
}
