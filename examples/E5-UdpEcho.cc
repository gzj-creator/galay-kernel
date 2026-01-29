/**
 * @file E5-UdpEcho.cc
 * @brief UDP Echo示例
 * @details 演示如何使用UdpSocket创建UDP服务器和客户端
 *
 * 使用场景：
 *   - 学习UDP通信基本用法
 *   - 理解无连接协议的特点
 *   - 作为UDP应用的基础模板
 */

#include <iostream>
#include <atomic>
#include "galay-kernel/async/UdpSocket.h"
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

std::atomic<bool> g_server_ready{false};

/**
 * @brief UDP Echo服务器协程
 */
Coroutine udpServer() {
    LogInfo("UDP Server starting...");

    // 创建UDP socket
    UdpSocket socket;

    // 设置socket选项
    auto optResult = socket.option().handleReuseAddr();
    if (!optResult) {
        LogError("Failed to set reuse addr: {}", optResult.error().message());
        co_return;
    }

    optResult = socket.option().handleNonBlock();
    if (!optResult) {
        LogError("Failed to set non-block: {}", optResult.error().message());
        co_return;
    }

    // 绑定地址
    Host bindHost(IPType::IPV4, "127.0.0.1", 9090);
    auto bindResult = socket.bind(bindHost);
    if (!bindResult) {
        LogError("Failed to bind: {}", bindResult.error().message());
        co_return;
    }

    LogInfo("UDP Server listening on 127.0.0.1:9090");
    g_server_ready = true;

    // 接收并回显数据
    char buffer[1024];
    Host clientHost;

    auto recvResult = co_await socket.recvfrom(buffer, sizeof(buffer), &clientHost);
    if (!recvResult) {
        LogError("Failed to recvfrom: {}", recvResult.error().message());
        co_return;
    }

    auto& bytes = recvResult.value();
    LogInfo("Received from {}:{}: {}",
            clientHost.ip(), clientHost.port(), bytes.toStringView());

    // 回显数据
    auto sendResult = co_await socket.sendto(bytes.c_str(), bytes.size(), clientHost);
    if (!sendResult) {
        LogError("Failed to sendto: {}", sendResult.error().message());
        co_return;
    }

    LogInfo("Echoed {} bytes", sendResult.value());
    co_await socket.close();
}

/**
 * @brief UDP客户端协程
 */
Coroutine udpClient() {
    // 等待服务器准备好
    while (!g_server_ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    LogInfo("UDP Client starting...");

    // 创建UDP socket
    UdpSocket socket;

    // 设置非阻塞模式
    auto optResult = socket.option().handleNonBlock();
    if (!optResult) {
        LogError("Failed to set non-block: {}", optResult.error().message());
        co_return;
    }

    // 发送数据到服务器
    Host serverHost(IPType::IPV4, "127.0.0.1", 9090);
    std::string message = "Hello, UDP Server!";

    auto sendResult = co_await socket.sendto(message.c_str(), message.size(), serverHost);
    if (!sendResult) {
        LogError("Failed to sendto: {}", sendResult.error().message());
        co_return;
    }

    LogInfo("Sent: {}", message);

    // 接收响应
    char buffer[1024];
    Host fromHost;
    auto recvResult = co_await socket.recvfrom(buffer, sizeof(buffer), &fromHost);
    if (!recvResult) {
        LogError("Failed to recvfrom: {}", recvResult.error().message());
        co_return;
    }

    auto& bytes = recvResult.value();
    LogInfo("Received from {}:{}: {}",
            fromHost.ip(), fromHost.port(), bytes.toStringView());

    co_await socket.close();
}

int main() {
    LogInfo("=== UDP Echo Example ===");

    // 创建IO调度器
    IOSchedulerType scheduler;

    // 启动调度器
    scheduler.start();

    // 启动服务器和客户端
    scheduler.spawn(udpServer());
    scheduler.spawn(udpClient());

    // 等待执行完成
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 停止调度器
    scheduler.stop();

    LogInfo("=== Example Completed ===");
    return 0;
}
