/**
 * @file test_sendfile.cc
 * @brief SendFile 零拷贝文件传输测试
 * @details 测试 TcpSocket::sendfile() 功能，包括：
 *   - 服务器使用 sendfile 发送文件
 *   - 客户端接收并验证文件内容
 *   - 测试大文件传输性能
 */

#include <iostream>
#include <fstream>
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <chrono>
#include "galay-kernel/async/TcpSocket.h"
#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/common/Log.h"
#include "test_result_writer.h"

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

// 测试配置
const char* TEST_FILE = "/tmp/galay_sendfile_test.dat";
const char* RECEIVED_FILE = "/tmp/galay_sendfile_received.dat";
const size_t SMALL_FILE_SIZE = 1024;           // 1KB
const size_t MEDIUM_FILE_SIZE = 1024 * 1024;   // 1MB
const size_t LARGE_FILE_SIZE = 10 * 1024 * 1024; // 10MB
const uint16_t TEST_PORT = 9090;

std::atomic<int> g_passed{0};
std::atomic<int> g_failed{0};
std::atomic<int> g_total{0};
std::atomic<bool> g_server_ready{false};
std::atomic<bool> g_test_done{false};

// 创建测试文件
void createTestFile(size_t size) {
    std::ofstream ofs(TEST_FILE, std::ios::binary);
    if (!ofs) {
        LogError("Failed to create test file");
        return;
    }

    // 写入可识别的模式数据
    for (size_t i = 0; i < size; ++i) {
        ofs.put(static_cast<char>(i % 256));
    }
    ofs.close();
    LogInfo("Created test file: {} ({} bytes)", TEST_FILE, size);
}

// 验证接收的文件
bool verifyReceivedFile(size_t expected_size) {
    std::ifstream ifs(RECEIVED_FILE, std::ios::binary);
    if (!ifs) {
        LogError("Failed to open received file");
        return false;
    }

    size_t actual_size = 0;
    char ch;
    while (ifs.get(ch)) {
        if (static_cast<unsigned char>(ch) != (actual_size % 256)) {
            LogError("Data mismatch at byte {}: expected {}, got {}",
                     actual_size, actual_size % 256, static_cast<unsigned char>(ch));
            return false;
        }
        actual_size++;
    }

    if (actual_size != expected_size) {
        LogError("Size mismatch: expected {}, got {}", expected_size, actual_size);
        return false;
    }

    LogInfo("File verification passed: {} bytes", actual_size);
    return true;
}

// 清理测试文件
void cleanupTestFiles() {
    std::remove(TEST_FILE);
    std::remove(RECEIVED_FILE);
    LogInfo("Cleaned up test files");
}

// 处理客户端连接的协程
Coroutine handleClient(TcpSocket client, size_t file_size) {
    LogInfo("Client connected, preparing to send file...");

    // 打开测试文件
    int file_fd = open(TEST_FILE, O_RDONLY);
    if (file_fd < 0) {
        LogError("Failed to open test file: {}", strerror(errno));
        co_await client.close();
        co_return;
    }

    // 先发送文件大小（8字节）
    uint64_t size_to_send = file_size;
    auto sendResult = co_await client.send(reinterpret_cast<const char*>(&size_to_send), sizeof(size_to_send));
    if (!sendResult) {
        LogError("Failed to send file size: {}", sendResult.error().message());
        close(file_fd);
        co_await client.close();
        co_return;
    }

    // 使用 sendfile 发送文件内容
    LogInfo("Starting sendfile transfer ({} bytes)...", file_size);
    auto start_time = std::chrono::high_resolution_clock::now();

    size_t total_sent = 0;
    off_t offset = 0;

    while (total_sent < file_size) {
        size_t remaining = file_size - total_sent;
        size_t chunk_size = std::min(remaining, size_t(1024 * 1024)); // 1MB chunks

        auto result = co_await client.sendfile(file_fd, offset, chunk_size);
        if (!result) {
            LogError("Sendfile failed at offset {}: {}", offset, result.error().message());
            break;
        }

        size_t sent = result.value();
        if (sent == 0) {
            LogError("Sendfile returned 0 bytes");
            break;
        }

        total_sent += sent;
        offset += sent;

        LogDebug("Sent {} bytes, total: {}/{}", sent, total_sent, file_size);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    close(file_fd);

    if (total_sent == file_size) {
        double speed_mbps = (file_size / 1024.0 / 1024.0) / (duration.count() / 1000.0);
        LogInfo("Sendfile completed: {} bytes in {} ms ({:.2f} MB/s)",
                total_sent, duration.count(), speed_mbps);
        g_passed++;
    } else {
        LogError("Sendfile incomplete: sent {}/{} bytes", total_sent, file_size);
        g_failed++;
    }

    co_await client.close();
    LogInfo("Client connection closed");
}

// 服务器协程
Coroutine sendfileServer(size_t file_size) {
    g_total++;
    LogInfo("=== SendFile Server starting ===");

    TcpSocket listener;

    // 设置选项
    auto optResult = listener.option().handleReuseAddr();
    if (!optResult) {
        LogError("Failed to set reuse addr: {}", optResult.error().message());
        g_failed++;
        g_test_done = true;
        co_return;
    }

    optResult = listener.option().handleNonBlock();
    if (!optResult) {
        LogError("Failed to set non-block: {}", optResult.error().message());
        g_failed++;
        g_test_done = true;
        co_return;
    }

    // 绑定地址
    Host bindHost(IPType::IPV4, "127.0.0.1", TEST_PORT);
    auto bindResult = listener.bind(bindHost);
    if (!bindResult) {
        LogError("Failed to bind: {}", bindResult.error().message());
        g_failed++;
        g_test_done = true;
        co_return;
    }

    // 监听
    auto listenResult = listener.listen(128);
    if (!listenResult) {
        LogError("Failed to listen: {}", listenResult.error().message());
        g_failed++;
        g_test_done = true;
        co_return;
    }

    LogInfo("Server listening on 127.0.0.1:{}", TEST_PORT);
    g_server_ready = true;

    // 接受一个连接
    Host clientHost;
    auto acceptResult = co_await listener.accept(&clientHost);
    if (!acceptResult) {
        LogError("Accept failed: {}", acceptResult.error().message());
        g_failed++;
        g_test_done = true;
        co_return;
    }

    LogInfo("Accepted connection from {}:{}", clientHost.ip(), clientHost.port());

    // 处理客户端
    TcpSocket client(acceptResult.value());
    client.option().handleNonBlock();

    co_await handleClient(std::move(client), file_size);

    co_await listener.close();
    g_test_done = true;
    LogInfo("Server stopped");
}

// 客户端协程
Coroutine sendfileClient() {
    LogInfo("=== SendFile Client starting ===");

    // 等待服务器就绪
    while (!g_server_ready.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    TcpSocket socket;
    socket.option().handleNonBlock();

    // 连接服务器
    Host serverHost(IPType::IPV4, "127.0.0.1", TEST_PORT);
    auto connectResult = co_await socket.connect(serverHost);
    if (!connectResult) {
        LogError("Connect failed: {}", connectResult.error().message());
        g_failed++;
        co_return;
    }

    LogInfo("Connected to server");

    // 接收文件大小
    uint64_t file_size = 0;
    char size_buf[sizeof(file_size)];
    auto recvResult = co_await socket.recv(size_buf, sizeof(size_buf));
    if (!recvResult) {
        LogError("Failed to receive file size: {}", recvResult.error().message());
        g_failed++;
        co_await socket.close();
        co_return;
    }

    std::memcpy(&file_size, size_buf, sizeof(file_size));
    LogInfo("Expecting to receive {} bytes", file_size);

    // 接收文件内容
    std::ofstream ofs(RECEIVED_FILE, std::ios::binary);
    if (!ofs) {
        LogError("Failed to create received file");
        g_failed++;
        co_await socket.close();
        co_return;
    }

    size_t total_received = 0;
    char buffer[8192];

    while (total_received < file_size) {
        size_t to_recv = std::min(sizeof(buffer), file_size - total_received);
        auto result = co_await socket.recv(buffer, to_recv);

        if (!result) {
            LogError("Recv failed: {}", result.error().message());
            break;
        }

        auto& bytes = result.value();
        if (bytes.size() == 0) {
            LogError("Connection closed by server");
            break;
        }

        ofs.write(bytes.data(), bytes.size());
        total_received += bytes.size();

        if (total_received % (1024 * 1024) == 0 || total_received == file_size) {
            LogDebug("Received {}/{} bytes", total_received, file_size);
        }
    }

    ofs.close();

    if (total_received == file_size) {
        LogInfo("File received successfully: {} bytes", total_received);

        // 验证文件内容
        if (verifyReceivedFile(file_size)) {
            LogInfo("✓ File content verification PASSED");
        } else {
            LogError("✗ File content verification FAILED");
            g_failed++;
        }
    } else {
        LogError("Incomplete file: received {}/{} bytes", total_received, file_size);
        g_failed++;
    }

    co_await socket.close();
    LogInfo("Client disconnected");
}

// 运行测试
void runTest(size_t file_size, const char* test_name) {
    LogInfo("\n========================================");
    LogInfo("Test: {}", test_name);
    LogInfo("========================================");

    g_server_ready = false;
    g_test_done = false;

    // 创建测试文件
    createTestFile(file_size);

    // 创建调度器
    IOSchedulerType scheduler;
    scheduler.start();

    // 启动服务器和客户端
    scheduler.spawn(sendfileServer(file_size));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    scheduler.spawn(sendfileClient());

    // 等待测试完成
    while (!g_test_done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    scheduler.stop();

    LogInfo("Test completed: {}", test_name);
}

int main() {
    LogInfo("=== SendFile Zero-Copy Transfer Test ===\n");

    // 测试1: 小文件 (1KB)
    runTest(SMALL_FILE_SIZE, "Small File (1KB)");

    // 测试2: 中等文件 (1MB)
    runTest(MEDIUM_FILE_SIZE, "Medium File (1MB)");

    // 测试3: 大文件 (10MB)
    runTest(LARGE_FILE_SIZE, "Large File (10MB)");

    // 清理
    cleanupTestFiles();

    // 输出结果
    LogInfo("\n========================================");
    LogInfo("Test Summary:");
    LogInfo("  Total:  {}", g_total.load());
    LogInfo("  Passed: {}", g_passed.load());
    LogInfo("  Failed: {}", g_failed.load());
    LogInfo("========================================");

    // 写入测试结果
    TestResultWriter writer("test_sendfile");
    writer.writeResult(g_passed.load(), g_failed.load(), g_total.load());

    return (g_failed.load() == 0) ? 0 : 1;
}
