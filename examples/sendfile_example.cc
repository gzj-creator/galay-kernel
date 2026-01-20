/**
 * @file sendfile_example.cc
 * @brief SendFile 零拷贝文件传输示例
 * @details 演示如何使用 TcpSocket::sendfile() 实现高效的文件传输
 *
 * 使用场景：
 *   - HTTP 文件服务器
 *   - FTP 服务器
 *   - 文件分发系统
 *   - 任何需要高效传输大文件的场景
 */

#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
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
 * @brief 获取文件大小
 */
off_t getFileSize(int fd) {
    struct stat st;
    if (fstat(fd, &st) < 0) {
        return -1;
    }
    return st.st_size;
}

/**
 * @brief 使用 sendfile 发送整个文件
 * @param socket TCP socket
 * @param file_path 文件路径
 * @return 发送的字节数，失败返回 -1
 */
Coroutine<ssize_t> sendFileToClient(TcpSocket& socket, const char* file_path) {
    // 打开文件
    int file_fd = open(file_path, O_RDONLY);
    if (file_fd < 0) {
        LogError("Failed to open file: {}", strerror(errno));
        co_return -1;
    }

    // 获取文件大小
    off_t file_size = getFileSize(file_fd);
    if (file_size < 0) {
        LogError("Failed to get file size: {}", strerror(errno));
        close(file_fd);
        co_return -1;
    }

    LogInfo("Sending file: {} ({} bytes)", file_path, file_size);

    // 使用 sendfile 发送文件
    size_t total_sent = 0;
    off_t offset = 0;

    while (total_sent < static_cast<size_t>(file_size)) {
        size_t remaining = file_size - total_sent;
        size_t chunk_size = std::min(remaining, size_t(1024 * 1024)); // 每次最多发送 1MB

        auto result = co_await socket.sendfile(file_fd, offset, chunk_size);

        if (!result) {
            LogError("Sendfile failed: {}", result.error().message());
            close(file_fd);
            co_return -1;
        }

        size_t sent = result.value();
        if (sent == 0) {
            LogError("Sendfile returned 0 bytes");
            break;
        }

        total_sent += sent;
        offset += sent;

        // 显示进度
        int progress = (total_sent * 100) / file_size;
        LogInfo("Progress: {}% ({}/{} bytes)", progress, total_sent, file_size);
    }

    close(file_fd);

    if (total_sent == static_cast<size_t>(file_size)) {
        LogInfo("File sent successfully: {} bytes", total_sent);
        co_return total_sent;
    } else {
        LogError("Incomplete transfer: sent {}/{} bytes", total_sent, file_size);
        co_return -1;
    }
}

/**
 * @brief HTTP 文件服务器示例
 * @details 简单的 HTTP 服务器，使用 sendfile 发送文件
 */
Coroutine handleHttpRequest(TcpSocket client) {
    LogInfo("Client connected");

    // 接收 HTTP 请求（简化版，只读取第一行）
    char request[1024] = {0};
    auto recvResult = co_await client.recv(request, sizeof(request) - 1);

    if (!recvResult) {
        LogError("Failed to receive request: {}", recvResult.error().message());
        co_await client.close();
        co_return;
    }

    LogInfo("Request: {}", request);

    // 解析请求（简化版）
    // 假设请求格式为: GET /filename HTTP/1.1
    const char* file_to_send = "/tmp/test_file.dat";

    // 发送 HTTP 响应头
    const char* response_header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Connection: close\r\n"
        "\r\n";

    auto sendResult = co_await client.send(response_header, strlen(response_header));
    if (!sendResult) {
        LogError("Failed to send response header: {}", sendResult.error().message());
        co_await client.close();
        co_return;
    }

    // 使用 sendfile 发送文件内容
    ssize_t sent = co_await sendFileToClient(client, file_to_send);

    if (sent > 0) {
        LogInfo("HTTP response completed: {} bytes", sent);
    } else {
        LogError("HTTP response failed");
    }

    co_await client.close();
    LogInfo("Client disconnected");
}

/**
 * @brief 简单的文件服务器
 */
Coroutine fileServer() {
    LogInfo("=== File Server Example ===");

    TcpSocket listener;

    // 设置 socket 选项
    listener.option().handleReuseAddr();
    listener.option().handleNonBlock();

    // 绑定地址
    Host bindHost(IPType::IPV4, "0.0.0.0", 8080);
    auto bindResult = listener.bind(bindHost);
    if (!bindResult) {
        LogError("Failed to bind: {}", bindResult.error().message());
        co_return;
    }

    // 监听
    auto listenResult = listener.listen(128);
    if (!listenResult) {
        LogError("Failed to listen: {}", listenResult.error().message());
        co_return;
    }

    LogInfo("Server listening on 0.0.0.0:8080");
    LogInfo("Waiting for connections...");

    // 接受连接循环
    while (true) {
        Host clientHost;
        auto acceptResult = co_await listener.accept(&clientHost);

        if (!acceptResult) {
            LogError("Accept failed: {}", acceptResult.error().message());
            break;
        }

        LogInfo("Accepted connection from {}:{}", clientHost.ip(), clientHost.port());

        // 创建客户端 socket
        TcpSocket client(acceptResult.value());
        client.option().handleNonBlock();

        // 处理请求（在新协程中）
        // 注意：这里为了简化示例，直接在当前协程中处理
        // 实际应用中应该 spawn 新协程来处理每个客户端
        co_await handleHttpRequest(std::move(client));
    }

    co_await listener.close();
}

/**
 * @brief 性能对比示例：sendfile vs read+send
 */
Coroutine performanceComparison() {
    LogInfo("\n=== Performance Comparison: sendfile vs read+send ===");

    const char* test_file = "/tmp/large_test_file.dat";

    // 创建测试文件（10MB）
    {
        int fd = open(test_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            const size_t size = 10 * 1024 * 1024; // 10MB
            char buffer[4096] = {0};
            for (size_t i = 0; i < size; i += sizeof(buffer)) {
                write(fd, buffer, sizeof(buffer));
            }
            close(fd);
            LogInfo("Created test file: {} (10MB)", test_file);
        }
    }

    // TODO: 实现性能对比测试
    // 1. 使用 sendfile 发送文件
    // 2. 使用传统的 read + send 发送文件
    // 3. 比较两者的性能差异

    LogInfo("Performance comparison completed");
    co_return;
}

int main() {
    LogInfo("=== SendFile Example ===\n");

    // 创建调度器
    IOSchedulerType scheduler;
    scheduler.start();

    // 启动文件服务器
    scheduler.spawn(fileServer());

    // 保持运行
    LogInfo("\nPress Ctrl+C to stop the server\n");

    // 简单的运行循环
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    scheduler.stop();
    return 0;
}

/**
 * 使用说明：
 *
 * 1. 编译程序：
 *    g++ -std=c++20 sendfile_example.cc -o sendfile_example -lgalay-kernel
 *
 * 2. 准备测试文件：
 *    dd if=/dev/zero of=/tmp/test_file.dat bs=1M count=10
 *
 * 3. 运行服务器：
 *    ./sendfile_example
 *
 * 4. 使用客户端测试：
 *    curl http://localhost:8080/ -o received_file.dat
 *    或
 *    wget http://localhost:8080/ -O received_file.dat
 *
 * 5. 验证文件：
 *    diff /tmp/test_file.dat received_file.dat
 *
 * 性能优势：
 *   - 零拷贝：数据直接从文件系统缓存传输到网络，不经过用户空间
 *   - 减少 CPU 使用：避免了用户空间的内存拷贝
 *   - 提高吞吐量：特别是在发送大文件时，性能提升显著
 *   - 降低延迟：减少了系统调用次数和上下文切换
 *
 * 适用场景：
 *   - 静态文件服务器（HTTP/HTTPS）
 *   - 文件下载服务
 *   - 视频流媒体服务
 *   - CDN 边缘节点
 *   - 备份和同步系统
 */
