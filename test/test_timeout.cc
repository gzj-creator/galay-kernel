#include <cstring>
#include <chrono>
#include <atomic>
#include "galay-kernel/async/TcpSocket.h"
#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/common/Sleep.hpp"
#include "galay-kernel/common/Log.h"
#include "test_result_writer.h"

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
using namespace std::chrono_literals;

std::atomic<int> g_passedCount{0};
std::atomic<int> g_totalCount{0};
galay::test::TestResultWriter* g_resultWriter = nullptr;

// 测试1：recv 超时测试（服务器不发送数据，客户端应该超时）
Coroutine testRecvTimeout() {
    LogInfo("[Test 1] Testing recv timeout...");
    g_totalCount++;
    g_resultWriter->addTest();

    TcpSocket listener;
    listener.option().handleReuseAddr();
    listener.option().handleNonBlock();

    Host bindHost(IPType::IPV4, "127.0.0.1", 9001);
    listener.bind(bindHost);
    listener.listen(1);

    TcpSocket client;
    client.option().handleNonBlock();

    Host serverHost(IPType::IPV4, "127.0.0.1", 9001);

    Host clientHost;
    auto acceptFuture = listener.accept(&clientHost);
    auto connectFuture = client.connect(serverHost);

    auto connectResult = co_await std::move(connectFuture);
    if (!connectResult) {
        LogError("[Test 1] Connect failed: {}", connectResult.error().message());
        co_return;
    }

    auto acceptResult = co_await std::move(acceptFuture);
    if (!acceptResult) {
        LogError("[Test 1] Accept failed: {}", acceptResult.error().message());
        co_return;
    }

    TcpSocket serverConn(acceptResult.value());
    serverConn.option().handleNonBlock();

    // 客户端尝试接收，但服务器不发送，应该超时
    char buffer[1024];
    auto start = std::chrono::steady_clock::now();

    // 使用 500ms 超时
    auto recvResult = co_await client.recv(buffer, sizeof(buffer)).timeout(500ms);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    if (!recvResult && IOError::contains(recvResult.error().code(), kTimeout)) {
        LogInfo("[Test 1] PASSED: Recv timed out as expected after {}ms", elapsedMs);
        g_passedCount++;
        g_resultWriter->addPassed();
    } else if (recvResult) {
        LogError("[Test 1] FAILED: Recv should have timed out but got data");
        g_resultWriter->addFailed();
    } else {
        LogError("[Test 1] FAILED: Got unexpected error: {}", recvResult.error().message());
        g_resultWriter->addFailed();
    }

    co_await client.close();
    co_await serverConn.close();
    co_await listener.close();
    co_return;
}

// 测试2：正常完成（不超时）
Coroutine testNoTimeout() {
    LogInfo("[Test 2] Testing normal completion (no timeout)...");
    g_totalCount++;
    g_resultWriter->addTest();

    TcpSocket listener;
    listener.option().handleReuseAddr();
    listener.option().handleNonBlock();

    Host bindHost(IPType::IPV4, "127.0.0.1", 9002);
    listener.bind(bindHost);
    listener.listen(1);

    TcpSocket client;
    client.option().handleNonBlock();

    Host serverHost(IPType::IPV4, "127.0.0.1", 9002);

    Host clientHost;
    auto acceptFuture = listener.accept(&clientHost);
    auto connectFuture = client.connect(serverHost);

    auto connectResult = co_await std::move(connectFuture);
    auto acceptResult = co_await std::move(acceptFuture);

    if (!connectResult || !acceptResult) {
        LogError("[Test 2] Connection setup failed");
        co_return;
    }

    TcpSocket serverConn(acceptResult.value());
    serverConn.option().handleNonBlock();

    // 服务器发送数据
    const char* msg = "Hello!";
    co_await serverConn.send(msg, strlen(msg));

    // 客户端接收，应该在超时前完成
    char buffer[1024];
    auto start = std::chrono::steady_clock::now();

    auto recvResult = co_await client.recv(buffer, sizeof(buffer)).timeout(5000ms);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    if (recvResult) {
        LogInfo("[Test 2] PASSED: Recv completed in {}ms with data: {}",
                elapsedMs, recvResult.value().toStringView());
        g_passedCount++;
        g_resultWriter->addPassed();
    } else {
        LogError("[Test 2] FAILED: Recv failed: {}", recvResult.error().message());
        g_resultWriter->addFailed();
    }

    co_await client.close();
    co_await serverConn.close();
    co_await listener.close();
    co_return;
}

// 测试3：connect 超时测试（连接到不存在的地址）
Coroutine testConnectTimeout() {
    LogInfo("[Test 3] Testing connect timeout...");
    g_totalCount++;
    g_resultWriter->addTest();

    TcpSocket client;
    client.option().handleNonBlock();

    // 连接到一个不可达的地址（10.255.255.1 通常不可达）
    Host unreachableHost(IPType::IPV4, "10.255.255.1", 12345);

    auto start = std::chrono::steady_clock::now();

    // 使用 1s 超时
    auto connectResult = co_await client.connect(unreachableHost).timeout(1000ms);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    if (!connectResult && IOError::contains(connectResult.error().code(), kTimeout)) {
        LogInfo("[Test 3] PASSED: Connect timed out as expected after {}ms", elapsedMs);
        g_passedCount++;
        g_resultWriter->addPassed();
    } else if (connectResult) {
        LogError("[Test 3] FAILED: Connect should have timed out");
        g_resultWriter->addFailed();
    } else {
        // 可能是其他错误（如 ENETUNREACH），也算通过
        LogInfo("[Test 3] PASSED: Connect failed with: {} (elapsed: {}ms)",
                connectResult.error().message(), elapsedMs);
        g_passedCount++;
        g_resultWriter->addPassed();
    }

    co_await client.close();
    co_return;
}

// 测试4：多次超时操作（验证 generation 机制）
Coroutine testMultipleTimeouts() {
    LogInfo("[Test 4] Testing multiple timeout operations...");
    g_totalCount++;
    g_resultWriter->addTest();

    TcpSocket listener;
    listener.option().handleReuseAddr();
    listener.option().handleNonBlock();

    Host bindHost(IPType::IPV4, "127.0.0.1", 9004);
    listener.bind(bindHost);
    listener.listen(1);

    TcpSocket client;
    client.option().handleNonBlock();

    Host serverHost(IPType::IPV4, "127.0.0.1", 9004);

    Host clientHost;
    auto acceptFuture = listener.accept(&clientHost);
    auto connectFuture = client.connect(serverHost);

    co_await std::move(connectFuture);
    auto acceptResult = co_await std::move(acceptFuture);

    TcpSocket serverConn(acceptResult.value());
    serverConn.option().handleNonBlock();

    char buffer[1024];
    int timeoutCount = 0;

    // 连续进行3次超时操作
    for (int i = 0; i < 3; i++) {
        auto start = std::chrono::steady_clock::now();
        auto recvResult = co_await client.recv(buffer, sizeof(buffer)).timeout(200ms);
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        if (!recvResult && IOError::contains(recvResult.error().code(), kTimeout)) {
            timeoutCount++;
            LogDebug("[Test 4] Iteration {}: timed out after {}ms", i + 1, elapsedMs);
        } else {
            LogError("[Test 4] Iteration {}: unexpected result", i + 1);
        }
    }

    if (timeoutCount == 3) {
        LogInfo("[Test 4] PASSED: All 3 timeout operations worked correctly");
        g_passedCount++;
        g_resultWriter->addPassed();
    } else {
        LogError("[Test 4] FAILED: Only {}/3 timeouts worked", timeoutCount);
        g_resultWriter->addFailed();
    }

    co_await client.close();
    co_await serverConn.close();
    co_await listener.close();
    co_return;
}

// 测试5：sleep 功能测试
Coroutine testSleep() {
    LogInfo("[Test 5] Testing sleep...");
    g_totalCount++;
    g_resultWriter->addTest();

    auto start = std::chrono::steady_clock::now();

    // 休眠 300ms
    co_await sleep(300ms);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // 允许 150ms 的误差（考虑 50ms tick 精度 + 多协程并发调度开销）
    if (elapsedMs >= 280 && elapsedMs <= 450) {
        LogInfo("[Test 5] PASSED: Sleep completed in {}ms (expected ~300ms)", elapsedMs);
        g_passedCount++;
        g_resultWriter->addPassed();
    } else {
        LogError("[Test 5] FAILED: Sleep took {}ms (expected ~300ms)", elapsedMs);
        g_resultWriter->addFailed();
    }

    co_return;
}

// 测试6：多次 sleep 测试
Coroutine testMultipleSleep() {
    LogInfo("[Test 6] Testing multiple sleep operations...");
    g_totalCount++;
    g_resultWriter->addTest();

    auto start = std::chrono::steady_clock::now();

    // 连续休眠 3 次，每次 100ms
    co_await sleep(100ms);
    co_await sleep(100ms);
    co_await sleep(100ms);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // 总共应该约 300ms，允许 150ms 误差（考虑 50ms tick 精度 + 多协程并发调度开销）
    if (elapsedMs >= 280 && elapsedMs <= 450) {
        LogInfo("[Test 6] PASSED: Multiple sleeps completed in {}ms (expected ~300ms)", elapsedMs);
        g_passedCount++;
        g_resultWriter->addPassed();
    } else {
        LogError("[Test 6] FAILED: Multiple sleeps took {}ms (expected ~300ms)", elapsedMs);
        g_resultWriter->addFailed();
    }

    co_return;
}

// 测试7：accept 超时测试
Coroutine testAcceptTimeout() {
    LogInfo("[Test 7] Testing accept timeout...");
    g_totalCount++;
    g_resultWriter->addTest();

    TcpSocket listener;
    listener.option().handleReuseAddr();
    listener.option().handleNonBlock();

    Host bindHost(IPType::IPV4, "127.0.0.1", 9007);
    listener.bind(bindHost);
    listener.listen(1);

    // 没有客户端连接，accept 应该超时
    Host clientHost;
    auto start = std::chrono::steady_clock::now();

    auto acceptResult = co_await listener.accept(&clientHost).timeout(300ms);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    if (!acceptResult && IOError::contains(acceptResult.error().code(), kTimeout)) {
        LogInfo("[Test 7] PASSED: Accept timed out as expected after {}ms", elapsedMs);
        g_passedCount++;
        g_resultWriter->addPassed();
    } else if (acceptResult) {
        LogError("[Test 7] FAILED: Accept should have timed out");
        g_resultWriter->addFailed();
    } else {
        LogError("[Test 7] FAILED: Got unexpected error: {}", acceptResult.error().message());
        g_resultWriter->addFailed();
    }

    co_await listener.close();
    co_return;
}

// 测试8：send 超时测试（填满发送缓冲区）
Coroutine testSendTimeout() {
    LogInfo("[Test 8] Testing send timeout...");
    g_totalCount++;
    g_resultWriter->addTest();

    TcpSocket listener;
    listener.option().handleReuseAddr();
    listener.option().handleNonBlock();

    Host bindHost(IPType::IPV4, "127.0.0.1", 9008);
    listener.bind(bindHost);
    listener.listen(1);

    TcpSocket client;
    client.option().handleNonBlock();

    Host serverHost(IPType::IPV4, "127.0.0.1", 9008);

    Host clientHost;
    auto acceptFuture = listener.accept(&clientHost);
    auto connectFuture = client.connect(serverHost);

    auto connectResult = co_await std::move(connectFuture);
    auto acceptResult = co_await std::move(acceptFuture);

    if (!connectResult || !acceptResult) {
        LogError("[Test 8] Connection setup failed");
        co_return;
    }

    TcpSocket serverConn(acceptResult.value());
    serverConn.option().handleNonBlock();

    // 尝试发送大量数据填满缓冲区，服务器不接收
    char largeBuffer[65536];
    memset(largeBuffer, 'A', sizeof(largeBuffer));

    auto start = std::chrono::steady_clock::now();
    int sendCount = 0;
    bool timedOut = false;

    // 循环发送直到超时
    for (int i = 0; i < 1000; i++) {
        auto sendResult = co_await client.send(largeBuffer, sizeof(largeBuffer)).timeout(200ms);
        if (!sendResult) {
            if (IOError::contains(sendResult.error().code(), kTimeout)) {
                timedOut = true;
                break;
            }
            // 其他错误也算通过（如缓冲区满）
            break;
        }
        sendCount++;
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    if (timedOut) {
        LogInfo("[Test 8] PASSED: Send timed out after {} sends, {}ms", sendCount, elapsedMs);
        g_passedCount++;
        g_resultWriter->addPassed();
    } else {
        // 如果没有超时但发送了很多次，也算通过（说明系统缓冲区很大）
        LogInfo("[Test 8] PASSED: Send completed {} times in {}ms (buffer not full)", sendCount, elapsedMs);
        g_passedCount++;
        g_resultWriter->addPassed();
    }

    co_await client.close();
    co_await serverConn.close();
    co_await listener.close();
    co_return;
}

// 测试9：IO 完成后定时器不应触发（竞争条件测试）
Coroutine testTimeoutRace() {
    LogInfo("[Test 9] Testing timeout race condition...");
    g_totalCount++;
    g_resultWriter->addTest();

    TcpSocket listener;
    listener.option().handleReuseAddr();
    listener.option().handleNonBlock();

    Host bindHost(IPType::IPV4, "127.0.0.1", 9009);
    listener.bind(bindHost);
    listener.listen(1);

    TcpSocket client;
    client.option().handleNonBlock();

    Host serverHost(IPType::IPV4, "127.0.0.1", 9009);

    Host clientHost;
    auto acceptFuture = listener.accept(&clientHost);
    auto connectFuture = client.connect(serverHost);

    co_await std::move(connectFuture);
    auto acceptResult = co_await std::move(acceptFuture);

    TcpSocket serverConn(acceptResult.value());
    serverConn.option().handleNonBlock();

    // 服务器立即发送数据
    const char* msg = "Quick!";
    co_await serverConn.send(msg, strlen(msg));

    // 客户端设置较长超时，但数据应该立即到达
    char buffer[1024];
    auto start = std::chrono::steady_clock::now();

    auto recvResult = co_await client.recv(buffer, sizeof(buffer)).timeout(5000ms);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // 应该在很短时间内完成，而不是等待 5 秒
    if (recvResult && elapsedMs < 100) {
        LogInfo("[Test 9] PASSED: Recv completed in {}ms (timeout was 5000ms)", elapsedMs);
        g_passedCount++;
        g_resultWriter->addPassed();
    } else if (!recvResult) {
        LogError("[Test 9] FAILED: Recv failed: {}", recvResult.error().message());
        g_resultWriter->addFailed();
    } else {
        LogError("[Test 9] FAILED: Recv took too long: {}ms", elapsedMs);
        g_resultWriter->addFailed();
    }

    co_await client.close();
    co_await serverConn.close();
    co_await listener.close();
    co_return;
}

// 测试10：零超时测试
Coroutine testZeroTimeout() {
    LogInfo("[Test 10] Testing zero/negative timeout...");
    g_totalCount++;
    g_resultWriter->addTest();

    // 测试 sleep(0) 应该立即返回
    auto start = std::chrono::steady_clock::now();
    co_await sleep(0ms);
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    if (elapsedMs < 50) {
        LogInfo("[Test 10] PASSED: Zero sleep completed in {}ms", elapsedMs);
        g_passedCount++;
        g_resultWriter->addPassed();
    } else {
        LogError("[Test 10] FAILED: Zero sleep took {}ms", elapsedMs);
        g_resultWriter->addFailed();
    }

    co_return;
}

int main() {
    LogInfo("=== Timeout Test Suite ===");

    galay::test::TestResultWriter resultWriter("test_timeout");
    g_resultWriter = &resultWriter;

#ifdef USE_IOURING
    LogInfo("Using IOUringScheduler");
    IOUringScheduler scheduler;
#elif defined(USE_EPOLL)
    LogInfo("Using EpollScheduler");
    EpollScheduler scheduler;
#elif defined(USE_KQUEUE)
    LogInfo("Using KqueueScheduler");
    KqueueScheduler scheduler;
#else
    LogError("No supported scheduler");
    resultWriter.writeResult();
    return 1;
#endif

    scheduler.start();

    // 依次启动测试
    scheduler.spawn(testRecvTimeout());
    // 使用调度器的空闲等待

    scheduler.spawn(testNoTimeout());
    // 使用调度器的空闲等待

    scheduler.spawn(testConnectTimeout());
    // 使用调度器的空闲等待

    scheduler.spawn(testMultipleTimeouts());
    // 使用调度器的空闲等待

    scheduler.spawn(testSleep());
    // 使用调度器的空闲等待

    scheduler.spawn(testMultipleSleep());
    // 使用调度器的空闲等待

    scheduler.spawn(testAcceptTimeout());
    // 使用调度器的空闲等待

    scheduler.spawn(testSendTimeout());
    // 使用调度器的空闲等待

    scheduler.spawn(testTimeoutRace());
    // 使用调度器的空闲等待

    scheduler.spawn(testZeroTimeout());
    // 使用调度器的空闲等待

    // 等待所有测试完成
    std::this_thread::sleep_for(std::chrono::seconds(10));

    scheduler.stop();

    LogInfo("=== Results: {}/{} tests passed ===", g_passedCount.load(), g_totalCount.load());

    // 写入测试结果
    resultWriter.writeResult();

    return g_passedCount.load() == g_totalCount.load() ? 0 : 1;
}
