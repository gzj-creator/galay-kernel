#include "galay-kernel/async/FileWatcher.h"
#include "galay-kernel/kernel/Coroutine.h"

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

// 持续监控文件变化的协程
Coroutine watchFileCoroutine(IOScheduler* scheduler, const std::string& path)
{
    FileWatcher watcher(scheduler);

    auto result = watcher.addWatch(path, FileWatchEvent::All);
    if (!result) {
        std::cerr << "Failed to add watch: " << result.error().message() << std::endl;
        co_return;
    }

    std::cout << "Watching: " << path << " (wd=" << result.value() << ")" << std::endl;

    // 持续监听文件变化
    while (g_running) {
        auto event = co_await watcher.watch();
        if (!event) {
            std::cerr << "Watch error: " << event.error().message() << std::endl;
            break;
        }

        std::cout << "[Event] ";

        if (event->has(FileWatchEvent::Access)) {
            std::cout << "Access ";
        }
        if (event->has(FileWatchEvent::Modify)) {
            std::cout << "Modify ";
        }
        if (event->has(FileWatchEvent::Attrib)) {
            std::cout << "Attrib ";
        }
        if (event->has(FileWatchEvent::CloseWrite)) {
            std::cout << "CloseWrite ";
        }
        if (event->has(FileWatchEvent::CloseNoWrite)) {
            std::cout << "CloseNoWrite ";
        }
        if (event->has(FileWatchEvent::Open)) {
            std::cout << "Open ";
        }
        if (event->has(FileWatchEvent::Create)) {
            std::cout << "Create ";
        }
        if (event->has(FileWatchEvent::Delete)) {
            std::cout << "Delete ";
        }
        if (event->has(FileWatchEvent::DeleteSelf)) {
            std::cout << "DeleteSelf ";
        }
        if (event->has(FileWatchEvent::MoveSelf)) {
            std::cout << "MoveSelf ";
        }

        if (!event->name.empty()) {
            std::cout << "file=" << event->name;
        }
        if (event->isDir) {
            std::cout << " (dir)";
        }
        std::cout << std::endl;
    }

    std::cout << "Watcher stopped." << std::endl;
}

// 模拟文件操作
void fileOperationThread(const std::string& path)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "\n--- File operations start ---\n" << std::endl;

    // 写入
    std::cout << "> Writing..." << std::endl;
    {
        std::ofstream ofs(path);
        ofs << "Hello" << std::endl;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // 追加
    std::cout << "> Appending..." << std::endl;
    {
        std::ofstream ofs(path, std::ios::app);
        ofs << "World" << std::endl;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // 读取
    std::cout << "> Reading..." << std::endl;
    {
        std::ifstream ifs(path);
        std::string line;
        while (std::getline(ifs, line)) {}
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    std::cout << "\n--- File operations done ---\n" << std::endl;

    // 停止监控
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    g_running = false;
}

int main()
{
    std::cout << "=== FileWatcher Test ===" << std::endl;

#ifdef USE_IOURING
    std::cout << "Backend: io_uring" << std::endl;
#elif defined(USE_EPOLL)
    std::cout << "Backend: epoll + inotify" << std::endl;
#elif defined(USE_KQUEUE)
    std::cout << "Backend: kqueue" << std::endl;
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
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    scheduler.stop();
    std::remove(testFile.c_str());

    std::cout << "=== Test Done ===" << std::endl;
    return 0;
}
