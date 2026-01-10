#include "galay-kernel/async/FileWatcher.h"
#include "galay-kernel/kernel/Coroutine.h"
#include "test_result_writer.h"
#include "galay-kernel/common/Log.h"

#ifdef USE_IOURING
#include "galay-kernel/kernel/IOUringScheduler.h"
using TestScheduler = galay::kernel::IOUringScheduler;
#elif defined(USE_EPOLL)
#include "galay-kernel/kernel/EpollScheduler.h"
using TestScheduler = galay::kernel::EpollScheduler;
#elif defined(USE_KQUEUE)
#include "galay-kernel/kernel/KqueueScheduler.h"
using TestScheduler = galay::kernel::KqueueScheduler;
#endif

#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdio>

using namespace galay::kernel;
using namespace galay::async;

std::atomic<bool> g_running{true};
std::atomic<int> g_passed{0};
std::atomic<int> g_failed{0};
std::atomic<int> g_total{0};
std::atomic<int> g_event_count{0};

// 持续监控文件变化的协程
Coroutine watchFileCoroutine(IOScheduler* scheduler, const std::string& path)
{
    g_total++;
    FileWatcher watcher;

    auto result = watcher.addWatch(path, FileWatchEvent::All);
    if (!result) {
        LogError("Failed to add watch: {}", result.error().message());
        g_failed++;
        co_return;
    }

    LogInfo("Watching: {} (wd={})", path, result.value());

    // 持续监听文件变化
    while (g_running) {
        auto event = co_await watcher.watch();
        if (!event) {
            LogError("Watch error: {}", event.error().message());
            g_failed++;
            break;
        }

        g_event_count++;
        LogInfo("[Event {}] ", g_event_count.load());

        if (event->has(FileWatchEvent::Access)) {
            LogInfo("Access ");
        }
        if (event->has(FileWatchEvent::Modify)) {
            LogInfo("Modify ");
        }
        if (event->has(FileWatchEvent::Attrib)) {
            LogInfo("Attrib ");
        }
        if (event->has(FileWatchEvent::CloseWrite)) {
            LogInfo("CloseWrite ");
        }
        if (event->has(FileWatchEvent::CloseNoWrite)) {
            LogInfo("CloseNoWrite ");
        }
        if (event->has(FileWatchEvent::Open)) {
            LogInfo("Open ");
        }
        if (event->has(FileWatchEvent::Create)) {
            LogInfo("Create ");
        }
        if (event->has(FileWatchEvent::Delete)) {
            LogInfo("Delete ");
        }
        if (event->has(FileWatchEvent::DeleteSelf)) {
            LogInfo("DeleteSelf ");
        }
        if (event->has(FileWatchEvent::MoveSelf)) {
            LogInfo("MoveSelf ");
        }

        if (!event->name.empty()) {
            LogInfo("file={}", event->name);
        }
        if (event->isDir) {
            LogInfo(" (dir)");
        }
    }

    if (g_event_count > 0) {
        LogInfo("Test PASSED: Received {} file events", g_event_count.load());
        g_passed++;
    } else {
        LogError("Test FAILED: No events received");
        g_failed++;
    }

    LogInfo("Watcher stopped.");
}

// 模拟文件操作
void fileOperationThread(const std::string& path)
{
    // 使用 busy-wait 替代 sleep，配合原子变量
    auto wait_ms = [](int ms) {
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count() < ms) {
            std::this_thread::yield();
        }
    };

    wait_ms(500);

    LogInfo("\n--- File operations start ---\n");

    // 写入
    LogInfo("> Writing...");
    {
        std::ofstream ofs(path);
        ofs << "Hello" << std::endl;
    }
    wait_ms(300);

    // 追加
    LogInfo("> Appending...");
    {
        std::ofstream ofs(path, std::ios::app);
        ofs << "World" << std::endl;
    }
    wait_ms(300);

    // 读取
    LogInfo("> Reading...");
    {
        std::ifstream ifs(path);
        std::string line;
        while (std::getline(ifs, line)) {}
    }
    wait_ms(300);

    LogInfo("\n--- File operations done ---\n");

    // 停止监控
    wait_ms(500);
    g_running = false;
}

int main()
{
    LogInfo("========================================");
    LogInfo("FileWatcher Test");
    LogInfo("========================================\n");

#ifdef USE_IOURING
    LogInfo("Backend: io_uring");
#elif defined(USE_EPOLL)
    LogInfo("Backend: epoll + inotify");
#elif defined(USE_KQUEUE)
    LogInfo("Backend: kqueue");
#endif

    const std::string testFile = "/tmp/test_watcher.txt";

    // 创建初始文件
    { std::ofstream ofs(testFile); ofs << "init" << std::endl; }

    TestScheduler scheduler;
    scheduler.start();

    // 启动文件操作线程
    std::thread opThread(fileOperationThread, testFile);

    // 启动监控协程
    scheduler.spawn(watchFileCoroutine(&scheduler, testFile));

    opThread.join();

    // 等待监控完成
    while (g_running.load()) {
        // 使用调度器的空闲等待
    }

    scheduler.stop();
    std::remove(testFile.c_str());

    // 写入测试结果
    galay::test::TestResultWriter writer("test_file_watcher");
    for (int i = 0; i < g_total.load(); ++i) {
        writer.addTest();
    }
    for (int i = 0; i < g_passed.load(); ++i) {
        writer.addPassed();
    }
    for (int i = 0; i < g_failed.load(); ++i) {
        writer.addFailed();
    }
    writer.writeResult();

    LogInfo("========================================");
    LogInfo("Test Results: Total={}, Passed={}, Failed={}, Events={}",
            g_total.load(), g_passed.load(), g_failed.load(), g_event_count.load());
    LogInfo("========================================");

    return g_failed > 0 ? 1 : 0;
}
