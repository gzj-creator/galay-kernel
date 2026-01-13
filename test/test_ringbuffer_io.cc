#include <iostream>
#include <cstring>
#include <cassert>
#include <atomic>
#include "galay-kernel/async/TcpSocket.h"
#include "galay-kernel/common/Buffer.h"
#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/common/Log.h"

#ifdef USE_KQUEUE
#include "galay-kernel/kernel/KqueueScheduler.h"
#endif

#ifdef USE_EPOLL
#include "galay-kernel/kernel/EpollScheduler.h"
#endif

#ifdef USE_IOURING
#include "galay-kernel/kernel/IOUringScheduler.h"
#endif

using namespace galay::async;
using namespace galay::kernel;

std::atomic<bool> g_server_ready{false};
std::atomic<bool> g_test_passed{false};

// ============ 单元测试 ============

void testBasicOperations() {
    LogInfo("=== Test: Basic Operations ===");

    RingBuffer buf(100);
    assert(buf.capacity() == 100);
    assert(buf.readable() == 0);
    assert(buf.writable() == 100);
    assert(buf.empty());
    assert(!buf.full());

    // 写入数据
    const char* data = "Hello World";
    size_t written = buf.write(data, strlen(data));
    assert(written == 11);
    assert(buf.readable() == 11);
    assert(buf.writable() == 89);

    // 消费部分数据
    buf.consume(6);
    assert(buf.readable() == 5);

    // 清空
    buf.clear();
    assert(buf.empty());
    assert(buf.readable() == 0);

    LogInfo("Test: Basic Operations PASSED");
}

void testWrapAround() {
    LogInfo("=== Test: Wrap Around ===");

    RingBuffer buf(20);

    // 写入15字节
    buf.write("123456789012345", 15);
    assert(buf.readable() == 15);

    // 消费10字节，readIndex=10, writeIndex=15, size=5
    buf.consume(10);
    assert(buf.readable() == 5);

    // 再写入10字节，会环绕
    // writeIndex: 15 -> 20 -> 0 -> 5
    // 数据布局: [10,15)="12345", [15,20)="ABCDE", [0,5)="FGHIJ"
    buf.write("ABCDEFGHIJ", 10);
    assert(buf.readable() == 15);

    // 验证 getReadIovecs 返回两段
    auto readIovecs = buf.getReadIovecs();
    assert(readIovecs.size() == 2);

    // 第一段: [10, 20) = "12345ABCDE" (10字节)
    assert(readIovecs[0].iov_len == 10);
    assert(std::memcmp(readIovecs[0].iov_base, "12345ABCDE", 10) == 0);

    // 第二段: [0, 5) = "FGHIJ" (5字节)
    assert(readIovecs[1].iov_len == 5);
    assert(std::memcmp(readIovecs[1].iov_base, "FGHIJ", 5) == 0);

    LogInfo("Test: Wrap Around PASSED");
}

void testGetWriteIovecs() {
    LogInfo("=== Test: getWriteIovecs ===");

    RingBuffer buf(20);

    // 空缓冲区，应该返回一段 [0, 20)
    auto iovecs1 = buf.getWriteIovecs();
    assert(iovecs1.size() == 1);
    assert(iovecs1[0].iov_len == 20);

    // 写入10字节
    buf.write("0123456789", 10);

    // 应该返回一段 [10, 20)
    auto iovecs2 = buf.getWriteIovecs();
    assert(iovecs2.size() == 1);
    assert(iovecs2[0].iov_len == 10);

    // 消费5字节，readIndex=5
    buf.consume(5);

    // 应该返回两段 [10, 20) 和 [0, 5)
    auto iovecs3 = buf.getWriteIovecs();
    assert(iovecs3.size() == 2);
    assert(iovecs3[0].iov_len == 10);
    assert(iovecs3[1].iov_len == 5);

    LogInfo("Test: getWriteIovecs PASSED");
}

void testGetReadIovecs() {
    LogInfo("=== Test: getReadIovecs ===");

    RingBuffer buf(20);

    // 空缓冲区
    auto iovecs1 = buf.getReadIovecs();
    assert(iovecs1.empty());

    // 写入数据（连续）
    buf.write("Hello", 5);
    auto iovecs2 = buf.getReadIovecs();
    assert(iovecs2.size() == 1);
    assert(iovecs2[0].iov_len == 5);

    // 制造环绕情况
    buf.consume(5);
    buf.write("12345678901234567890", 20); // 写满
    buf.consume(15); // 消费15字节
    buf.write("ABCDE", 5); // 环绕写入

    auto iovecs3 = buf.getReadIovecs();
    assert(iovecs3.size() == 2);

    LogInfo("Test: getReadIovecs PASSED");
}

void testFullAndEmpty() {
    LogInfo("=== Test: Full and Empty ===");

    RingBuffer buf(10);

    assert(buf.empty());
    assert(!buf.full());

    buf.write("1234567890", 10);
    assert(!buf.empty());
    assert(buf.full());
    assert(buf.writable() == 0);

    // 满时 getWriteIovecs 应该返回空
    auto iovecs = buf.getWriteIovecs();
    assert(iovecs.empty());

    buf.consume(10);
    assert(buf.empty());
    assert(!buf.full());

    LogInfo("Test: Full and Empty PASSED");
}

void testMoveSemantics() {
    LogInfo("=== Test: Move Semantics ===");

    RingBuffer buf1(100);
    buf1.write("Test Data", 9);

    // 移动构造
    RingBuffer buf2(std::move(buf1));
    assert(buf2.readable() == 9);
    assert(buf2.capacity() == 100);

    // 移动赋值
    RingBuffer buf3(50);
    buf3 = std::move(buf2);
    assert(buf3.readable() == 9);
    assert(buf3.capacity() == 100);

    LogInfo("Test: Move Semantics PASSED");
}

// ============ 集成测试（网络 IO）============

// 服务器协程 - 使用 RingBuffer + readv 接收数据
Coroutine ringBufferServer(IOScheduler* scheduler) {
    LogInfo("[Server] Starting...");
    TcpSocket listener;

    listener.option().handleReuseAddr();
    listener.option().handleNonBlock();

    Host bindHost(IPType::IPV4, "127.0.0.1", 9091);
    auto bindResult = listener.bind(bindHost);
    if (!bindResult) {
        LogError("[Server] Failed to bind: {}", bindResult.error().message());
        co_return;
    }

    auto listenResult = listener.listen(128);
    if (!listenResult) {
        LogError("[Server] Failed to listen: {}", listenResult.error().message());
        co_return;
    }

    LogInfo("[Server] Listening on 127.0.0.1:9091");
    g_server_ready = true;

    Host clientHost;
    auto acceptResult = co_await listener.accept(&clientHost);
    if (!acceptResult) {
        LogError("[Server] Failed to accept: {}", acceptResult.error().message());
        co_return;
    }

    LogInfo("[Server] Client connected from {}:{}", clientHost.ip(), clientHost.port());

    TcpSocket client(acceptResult.value());
    client.option().handleNonBlock();

    // 使用 RingBuffer 接收数据
    RingBuffer recvBuffer(1024);

    // 获取可写 iovec 用于 readv
    auto iovecs = recvBuffer.getWriteIovecs();
    LogInfo("[Server] Prepared {} iovecs for readv", iovecs.size());

    auto readvResult = co_await client.readv(std::move(iovecs));
    if (!readvResult) {
        LogError("[Server] readv failed: {}", readvResult.error().message());
        co_await client.close();
        co_await listener.close();
        co_return;
    }

    size_t bytesRead = readvResult.value();
    recvBuffer.produce(bytesRead);
    LogInfo("[Server] readv received {} bytes, readable: {}", bytesRead, recvBuffer.readable());

    // 获取可读数据验证
    auto readIovecs = recvBuffer.getReadIovecs();
    std::string received;
    for (const auto& iov : readIovecs) {
        received.append(static_cast<const char*>(iov.iov_base), iov.iov_len);
    }
    LogInfo("[Server] Received data: '{}'", received);

    bool dataOk = (received.find("Hello from RingBuffer client!") != std::string::npos);
    if (dataOk) {
        LogInfo("[Server] Data verification PASSED!");
        g_test_passed = true;
    } else {
        LogError("[Server] Data verification FAILED!");
    }

    // 使用 RingBuffer + writev 发送响应
    RingBuffer sendBuffer(1024);
    std::string response = "Response: " + received + " [echoed]";
    sendBuffer.write(response.data(), response.size());

    LogInfo("[Server] Sending {} bytes via writev", sendBuffer.readable());

    auto writeIovecs = sendBuffer.getReadIovecs();
    if (!writeIovecs.empty()) {
        auto writevResult = co_await client.writev(std::move(writeIovecs));
        if (!writevResult) {
            LogError("[Server] writev failed: {}", writevResult.error().message());
        } else {
            sendBuffer.consume(writevResult.value());
            LogInfo("[Server] writev sent {} bytes", writevResult.value());
        }
    }

    co_await client.close();
    co_await listener.close();
    LogInfo("[Server] Stopped");
    co_return;
}

// 客户端协程 - 使用 RingBuffer + writev 发送数据
Coroutine ringBufferClient(IOScheduler* scheduler) {
    // 等待服务器就绪
    while (!g_server_ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    LogInfo("[Client] Starting...");
    TcpSocket client;
    client.option().handleNonBlock();

    Host serverHost(IPType::IPV4, "127.0.0.1", 9091);
    auto connectResult = co_await client.connect(serverHost);
    if (!connectResult) {
        LogError("[Client] Failed to connect: {}", connectResult.error().message());
        co_return;
    }

    LogInfo("[Client] Connected to server");

    // 使用 RingBuffer 准备发送数据
    RingBuffer sendBuffer(1024);
    const char* msg = "Hello from RingBuffer client!";
    sendBuffer.write(msg, strlen(msg));

    LogInfo("[Client] Sending {} bytes via writev", sendBuffer.readable());

    // 使用 writev 发送
    auto writeIovecs = sendBuffer.getReadIovecs();
    auto writevResult = co_await client.writev(std::move(writeIovecs));
    if (!writevResult) {
        LogError("[Client] writev failed: {}", writevResult.error().message());
        co_await client.close();
        co_return;
    }

    sendBuffer.consume(writevResult.value());
    LogInfo("[Client] writev sent {} bytes", writevResult.value());

    // 使用 RingBuffer + readv 接收响应
    RingBuffer recvBuffer(1024);

    auto readIovecs = recvBuffer.getWriteIovecs();
    auto readvResult = co_await client.readv(std::move(readIovecs));
    if (!readvResult) {
        LogError("[Client] readv failed: {}", readvResult.error().message());
    } else {
        recvBuffer.produce(readvResult.value());

        // 读取响应内容
        auto respIovecs = recvBuffer.getReadIovecs();
        std::string response;
        for (const auto& iov : respIovecs) {
            response.append(static_cast<const char*>(iov.iov_base), iov.iov_len);
        }
        LogInfo("[Client] Received {} bytes: '{}'", recvBuffer.readable(), response);
    }

    co_await client.close();
    LogInfo("[Client] Stopped");
    co_return;
}

int main() {
    LogInfo("=== RingBuffer Unit Tests ===");

    // 运行单元测试
    testBasicOperations();
    testWrapAround();
    testGetWriteIovecs();
    testGetReadIovecs();
    testFullAndEmpty();
    testMoveSemantics();

    LogInfo("=== All Unit Tests PASSED ===");
    LogInfo("");
    LogInfo("=== RingBuffer + readv/writev Integration Test ===");

#ifdef USE_KQUEUE
    LogInfo("Using KqueueScheduler (macOS)");
    KqueueScheduler scheduler;
#elif defined(USE_EPOLL)
    LogInfo("Using EpollScheduler (Linux)");
    EpollScheduler scheduler;
#elif defined(USE_IOURING)
    LogInfo("Using IOUringScheduler (Linux io_uring)");
    IOUringScheduler scheduler;
#else
    LogWarn("No supported scheduler available, skipping integration test");
    return 0;
#endif

    scheduler.start();

    // 启动服务器
    scheduler.spawn(ringBufferServer(&scheduler));

    // 启动客户端
    scheduler.spawn(ringBufferClient(&scheduler));

    // 等待测试完成
    std::this_thread::sleep_for(std::chrono::seconds(3));

    scheduler.stop();

    if (g_test_passed) {
        LogInfo("=== ALL TESTS PASSED ===");
        return 0;
    } else {
        LogError("=== INTEGRATION TEST FAILED ===");
        return 1;
    }
}
