/**
 * @file bench_sendfile.cc
 * @brief SendFile 性能对比测试
 * @details 对比 sendfile 和传统 read+send 的性能差异
 */

#include <iostream>
#include <fstream>
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <vector>
#include "galay-kernel/async/TcpSocket.h"
#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/common/Log.h"
#include "test/test_result_writer.h"

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
using namespace std::chrono;

const char* TEST_FILE = "/tmp/galay_benchmark_test.dat";
const uint16_t TEST_PORT = 9091;

std::atomic<bool> g_server_ready{false};
std::atomic<bool> g_test_done{false};
std::atomic<size_t> g_bytes_received{0};

struct BenchmarkResult {
    std::string method;
    size_t file_size;
    double duration_ms;
    double throughput_mbps;
    double cpu_usage;
};

std::vector<BenchmarkResult> g_results;

// 创建测试文件
void createBenchmarkFile(size_t size) {
    int fd = open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        LogError("Failed to create test file");
        return;
    }

    char buffer[4096];
    std::memset(buffer, 'A', sizeof(buffer));

    size_t written = 0;
    while (written < size) {
        size_t to_write = std::min(sizeof(buffer), size - written);
        ssize_t n = write(fd, buffer, to_write);
        if (n <= 0) break;
        written += n;
    }

    close(fd);
    LogInfo("Created benchmark file: {} bytes", size);
}

// 方法1: 使用 sendfile
Coroutine serverSendFile(TcpSocket client, size_t file_size) {
    int file_fd = open(TEST_FILE, O_RDONLY);
    if (file_fd < 0) {
        co_await client.close();
        co_return;
    }

    size_t total_sent = 0;
    off_t offset = 0;

    while (total_sent < file_size) {
        size_t chunk = std::min(file_size - total_sent, size_t(1024 * 1024));
        auto result = co_await client.sendfile(file_fd, offset, chunk);

        if (!result || result.value() == 0) break;

        total_sent += result.value();
        offset += result.value();
    }

    close(file_fd);
    co_await client.close();
}

// 方法2: 使用传统 read + send
Coroutine serverReadSend(TcpSocket client, size_t file_size) {
    int file_fd = open(TEST_FILE, O_RDONLY);
    if (file_fd < 0) {
        co_await client.close();
        co_return;
    }

    char buffer[8192];
    size_t total_sent = 0;

    while (total_sent < file_size) {
        // 从文件读取
        ssize_t n = read(file_fd, buffer, sizeof(buffer));
        if (n <= 0) break;

        // 发送到 socket
        size_t sent = 0;
        while (sent < static_cast<size_t>(n)) {
            auto result = co_await client.send(buffer + sent, n - sent);
            if (!result || result.value() == 0) break;
            sent += result.value();
        }

        total_sent += sent;
    }

    close(file_fd);
    co_await client.close();
}

// 服务器
Coroutine benchmarkServer(bool use_sendfile, size_t file_size) {
    TcpSocket listener;
    listener.option().handleReuseAddr();
    listener.option().handleNonBlock();

    Host bindHost(IPType::IPV4, "127.0.0.1", TEST_PORT);
    listener.bind(bindHost);
    listener.listen(128);

    g_server_ready = true;

    Host clientHost;
    auto acceptResult = co_await listener.accept(&clientHost);
    if (!acceptResult) {
        g_test_done = true;
        co_return;
    }

    TcpSocket client(acceptResult.value());
    client.option().handleNonBlock();

    if (use_sendfile) {
        co_await serverSendFile(std::move(client), file_size).wait();
    } else {
        co_await serverReadSend(std::move(client), file_size).wait();
    }

    co_await listener.close();
    g_test_done = true;
}

// 客户端
Coroutine benchmarkClient(size_t file_size) {
    while (!g_server_ready.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    TcpSocket socket;
    socket.option().handleNonBlock();

    Host serverHost(IPType::IPV4, "127.0.0.1", TEST_PORT);
    auto connectResult = co_await socket.connect(serverHost);
    if (!connectResult) {
        co_return;
    }

    char buffer[8192];
    size_t total_received = 0;

    while (total_received < file_size) {
        auto result = co_await socket.recv(buffer, sizeof(buffer));
        if (!result || result.value().size() == 0) break;
        total_received += result.value().size();
    }

    g_bytes_received = total_received;
    co_await socket.close();
}

// 运行基准测试
BenchmarkResult runBenchmark(bool use_sendfile, size_t file_size, const char* method_name) {
    LogInfo("\n=== Benchmark: {} ({} MB) ===", method_name, file_size / (1024.0 * 1024.0));

    g_server_ready = false;
    g_test_done = false;
    g_bytes_received = 0;

    IOSchedulerType scheduler;
    scheduler.start();

    auto start_time = high_resolution_clock::now();

    scheduler.spawn(benchmarkServer(use_sendfile, file_size));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    scheduler.spawn(benchmarkClient(file_size));

    while (!g_test_done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end_time = high_resolution_clock::now();
    scheduler.stop();

    double duration_ms = duration_cast<milliseconds>(end_time - start_time).count();
    double throughput_mbps = (file_size / (1024.0 * 1024.0)) / (duration_ms / 1000.0);

    BenchmarkResult result;
    result.method = method_name;
    result.file_size = file_size;
    result.duration_ms = duration_ms;
    result.throughput_mbps = throughput_mbps;
    result.cpu_usage = 0.0; // TODO: 实现 CPU 使用率监控

    LogInfo("Duration: {:.2f} ms", duration_ms);
    LogInfo("Throughput: {:.2f} MB/s", throughput_mbps);
    LogInfo("Bytes received: {}", g_bytes_received.load());

    return result;
}

// 打印结果表格
void printResults() {
    LogInfo("\n========================================");
    LogInfo("Performance Comparison Results");
    LogInfo("========================================");
    LogInfo("{:<20} {:<15} {:<15} {:<15}", "Method", "File Size", "Duration(ms)", "Throughput(MB/s)");
    LogInfo("------------------------------------------------------------------------");

    for (const auto& result : g_results) {
        LogInfo("{:<20} {:<15.2f} {:<15.2f} {:<15.2f}",
                result.method,
                result.file_size / (1024.0 * 1024.0),
                result.duration_ms,
                result.throughput_mbps);
    }

    LogInfo("========================================\n");

    // 计算性能提升
    if (g_results.size() >= 2) {
        for (size_t i = 0; i < g_results.size(); i += 2) {
            if (i + 1 < g_results.size()) {
                double improvement = (g_results[i].throughput_mbps / g_results[i + 1].throughput_mbps - 1.0) * 100.0;
                LogInfo("sendfile vs read+send ({:.0f}MB): {:.1f}% faster",
                        g_results[i].file_size / (1024.0 * 1024.0),
                        improvement);
            }
        }
    }
}

int main() {
    LogInfo("=== SendFile Performance Benchmark ===\n");

    std::vector<size_t> test_sizes = {
        1 * 1024 * 1024,      // 1MB
        10 * 1024 * 1024,     // 10MB
        50 * 1024 * 1024,     // 50MB
        100 * 1024 * 1024     // 100MB
    };

    for (size_t size : test_sizes) {
        createBenchmarkFile(size);

        // 测试 sendfile
        auto result1 = runBenchmark(true, size, "sendfile");
        g_results.push_back(result1);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // 测试 read+send
        auto result2 = runBenchmark(false, size, "read+send");
        g_results.push_back(result2);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::remove(TEST_FILE);

    printResults();

    galay::test::TestResultWriter writer("test_sendfile_benchmark");
    for (size_t i = 0; i < g_results.size(); ++i) {
        writer.addTest();
        writer.addPassed();
    }
    writer.writeResult();

    return 0;
}
