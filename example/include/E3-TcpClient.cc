/**
 * @file E3-TcpClient.cc
 * @brief TCP客户端示例
 * @details 演示如何使用TcpSocket创建TCP客户端并与服务器通信
 *
 * 使用场景：
 *   - 学习TCP客户端基本用法
 *   - 理解客户端连接和数据收发流程
 *   - 作为其他TCP客户端的基础模板
 */

#include <iostream>
#include <string>
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
 * @brief TCP客户端协程
 * @details 连接到服务器，发送消息并接收响应
 */
Coroutine tcpClient() {
    LogInfo("TCP Client starting...");

    // 创建客户端socket
    TcpSocket socket;

    // 设置非阻塞模式
    auto optResult = socket.option().handleNonBlock();
    if (!optResult) {
        LogError("Failed to set non-block: {}", optResult.error().message());
        co_return;
    }

    // 连接到服务器
    Host serverHost(IPType::IPV4, "127.0.0.1", 8080);
    LogInfo("Connecting to {}:{}...", serverHost.ip(), serverHost.port());

    auto connectResult = co_await socket.connect(serverHost);
    if (!connectResult) {
        LogError("Failed to connect: {}", connectResult.error().message());
        co_return;
    }

    LogInfo("Connected to server");

    // 发送消息
    std::string message = "Hello, Server!";
    auto sendResult = co_await socket.send(message.c_str(), message.size());
    if (!sendResult) {
        LogError("Failed to send: {}", sendResult.error().message());
        co_return;
    }

    LogInfo("Sent: {}", message);

    // 接收响应
    char buffer[1024];
    auto recvResult = co_await socket.recv(buffer, sizeof(buffer));
    if (!recvResult) {
        LogError("Failed to recv: {}", recvResult.error().message());
        co_return;
    }

    auto& bytes = recvResult.value();
    if (bytes.size() > 0) {
        LogInfo("Received: {}", bytes.toStringView());
    }

    // 关闭连接
    co_await socket.close();
    LogInfo("Client stopped");
}

int main() {
    // 创建IO调度器
    IOSchedulerType scheduler;

    // 启动调度器
    scheduler.start();

    // 提交客户端协程
    scheduler.spawn(tcpClient());

    // 等待一段时间让协程执行完成
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 停止调度器
    scheduler.stop();

    return 0;
}
