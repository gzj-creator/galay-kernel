#include <cstring>
#include <fstream>
#include <atomic>
#include "galay-kernel/common/Defn.hpp"
#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/common/Log.h"

#ifdef USE_KQUEUE
#include "galay-kernel/kernel/KqueueScheduler.h"
#include "galay-kernel/async/AsyncFile.h"
#endif

#ifdef USE_EPOLL
#include "galay-kernel/kernel/EpollScheduler.h"
#include "galay-kernel/async/AioFile.h"
#endif

#ifdef USE_IOURING
#include "galay-kernel/kernel/IOUringScheduler.h"
#include "galay-kernel/async/AsyncFile.h"
#endif

using namespace galay::kernel;

const char* TEST_FILE = "/tmp/galay_test_file.txt";
const char* TEST_CONTENT = "Hello, Galay Kernel File IO Test!\nThis is line 2.\nThis is line 3.\n";

// 创建测试文件
void createTestFile() {
    std::ofstream ofs(TEST_FILE);
    ofs << TEST_CONTENT;
    ofs.close();
    LogInfo("Created test file: {}", TEST_FILE);
}

// 清理测试文件
void cleanupTestFile() {
    std::remove(TEST_FILE);
    LogInfo("Cleaned up test file");
}

#ifdef USE_KQUEUE
// Kqueue 平台测试 - 所有测试在一个协程中完成
Coroutine testKqueueFileIO(KqueueScheduler* scheduler, std::atomic<bool>* done) {
    LogInfo("=== Kqueue (macOS) File IO Test ===");

    // 测试1: 读取已有文件
    {
        LogInfo("[Kqueue] Test 1: File read...");
        galay::async::AsyncFile file(scheduler);
        auto openResult = file.open(TEST_FILE, galay::async::FileOpenMode::Read);
        if (!openResult) {
            LogError("[Kqueue] Failed to open file for read: {}", openResult.error().message());
            *done = true;
            co_return;
        }

        char buffer[1024] = {0};
        auto result = co_await file.read(buffer, sizeof(buffer) - 1, 0);

        if (!result) {
            LogError("[Kqueue] Read failed: {}", result.error().message());
        } else {
            auto& bytes = result.value();
            LogInfo("[Kqueue] Read {} bytes: {}", bytes.size(), bytes.toStringView());
            if (bytes.toStringView() == TEST_CONTENT) {
                LogInfo("[Kqueue] Test 1 PASSED: Content matches");
            } else {
                LogError("[Kqueue] Test 1 FAILED: Content mismatch");
            }
        }
    }

    // 测试2: 写入文件然后读回验证
    {
        LogInfo("[Kqueue] Test 2: File write and read back...");
        const char* writeTestFile = "/tmp/galay_kqueue_write_test.txt";

        galay::async::AsyncFile file(scheduler);
        auto openResult = file.open(writeTestFile, galay::async::FileOpenMode::ReadWrite);
        if (!openResult) {
            LogError("[Kqueue] Failed to open file for write: {}", openResult.error().message());
            *done = true;
            co_return;
        }

        const char* writeContent = "Written by Kqueue AsyncFile test!\n";
        auto writeResult = co_await file.write(writeContent, strlen(writeContent), 0);

        if (!writeResult) {
            LogError("[Kqueue] Write failed: {}", writeResult.error().message());
        } else {
            LogInfo("[Kqueue] Written {} bytes", writeResult.value());

            // 同步到磁盘
            (void)file.sync();

            // 读回验证
            char buffer[1024] = {0};
            auto readResult = co_await file.read(buffer, sizeof(buffer) - 1, 0);
            if (readResult) {
                std::string_view written(writeContent);
                std::string_view readBack = readResult.value().toStringView();
                if (readBack.substr(0, written.size()) == written) {
                    LogInfo("[Kqueue] Test 2 PASSED: Write and read back successful");
                } else {
                    LogError("[Kqueue] Test 2 FAILED: Content mismatch");
                    LogError("[Kqueue] Expected: {}", written);
                    LogError("[Kqueue] Got: {}", readBack);
                }
            } else {
                LogError("[Kqueue] Test 2 FAILED: Read back error: {}", readResult.error().message());
            }
        }

        std::remove(writeTestFile);
    }

    LogInfo("=== Kqueue Test Finished ===\n");
    *done = true;
    co_return;
}

void runKqueueTest() {
    KqueueScheduler scheduler;
    scheduler.start();

    std::atomic<bool> done{false};
    scheduler.spawn(testKqueueFileIO(&scheduler, &done));

    // 等待协程完成
    while (!done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    scheduler.stop();
}
#endif

#ifdef USE_EPOLL
// Epoll 平台测试 - 所有测试在一个协程中完成
Coroutine testEpollFileIO(EpollScheduler* scheduler, std::atomic<bool>* done) {
    LogInfo("=== Epoll (Linux libaio) File IO Test ===");

    // 创建测试文件用于 O_DIRECT
    const char* aioTestFile = "/tmp/galay_aio_test.dat";
    {
        std::ofstream ofs(aioTestFile, std::ios::binary);
        char block[4096];
        memset(block, 'A', sizeof(block));
        strcpy(block, TEST_CONTENT);
        for (int i = 0; i < 10; ++i) {
            ofs.write(block, sizeof(block));
        }
        ofs.close();
    }

    // 测试1: 单次读取
    {
        LogInfo("[Epoll/AIO] Test 1: Single read...");
        galay::async::AioFile file(scheduler);
        auto openResult = file.open(aioTestFile, galay::async::AioOpenMode::Read);
        if (!openResult) {
            LogError("[Epoll/AIO] Failed to open file: {}", openResult.error().message());
            *done = true;
            co_return;
        }

        char* buffer = galay::async::AioFile::allocAlignedBuffer(4096);
        if (!buffer) {
            LogError("[Epoll/AIO] Failed to allocate aligned buffer");
            *done = true;
            co_return;
        }

        file.preRead(buffer, 4096, 0);
        auto result = co_await file.commit();

        if (!result) {
            LogError("[Epoll/AIO] Read failed: {}", result.error().message());
        } else {
            auto& results = result.value();
            if (!results.empty() && results[0] > 0) {
                buffer[results[0]] = '\0';
                LogInfo("[Epoll/AIO] Read {} bytes: {}", results[0], buffer);
                LogInfo("[Epoll/AIO] Test 1 PASSED");
            } else {
                LogError("[Epoll/AIO] Test 1 FAILED: No data read");
            }
        }

        galay::async::AioFile::freeAlignedBuffer(buffer);
    }

    // 测试2: 批量读取
    {
        LogInfo("[Epoll/AIO] Test 2: Batch read (3 requests)...");
        galay::async::AioFile file(scheduler);
        auto openResult = file.open(aioTestFile, galay::async::AioOpenMode::Read);
        if (!openResult) {
            LogError("[Epoll/AIO] Failed to open file: {}", openResult.error().message());
            *done = true;
            co_return;
        }

        char* buffer1 = galay::async::AioFile::allocAlignedBuffer(4096);
        char* buffer2 = galay::async::AioFile::allocAlignedBuffer(4096);
        char* buffer3 = galay::async::AioFile::allocAlignedBuffer(4096);

        if (!buffer1 || !buffer2 || !buffer3) {
            LogError("[Epoll/AIO] Failed to allocate aligned buffers");
            *done = true;
            co_return;
        }

        file.preRead(buffer1, 4096, 0);
        file.preRead(buffer2, 4096, 4096);
        file.preRead(buffer3, 4096, 8192);

        auto result = co_await file.commit();

        if (!result) {
            LogError("[Epoll/AIO] Batch read failed: {}", result.error().message());
        } else {
            auto& results = result.value();
            LogInfo("[Epoll/AIO] Batch read completed with {} results", results.size());
            for (size_t i = 0; i < results.size(); ++i) {
                LogInfo("[Epoll/AIO] Result[{}]: {} bytes", i, results[i]);
            }
            LogInfo("[Epoll/AIO] Test 2 PASSED");
        }

        galay::async::AioFile::freeAlignedBuffer(buffer1);
        galay::async::AioFile::freeAlignedBuffer(buffer2);
        galay::async::AioFile::freeAlignedBuffer(buffer3);
    }

    // 测试3: 写入
    {
        LogInfo("[Epoll/AIO] Test 3: Write...");
        const char* writeTestFile = "/tmp/galay_aio_write_test.dat";
        galay::async::AioFile file(scheduler);
        auto openResult = file.open(writeTestFile, galay::async::AioOpenMode::Write);
        if (!openResult) {
            LogError("[Epoll/AIO] Failed to open file for write: {}", openResult.error().message());
            *done = true;
            co_return;
        }

        char* buffer = galay::async::AioFile::allocAlignedBuffer(4096);
        if (!buffer) {
            LogError("[Epoll/AIO] Failed to allocate aligned buffer");
            *done = true;
            co_return;
        }

        const char* writeContent = "Written by Epoll AioFile test!\n";
        memset(buffer, 0, 4096);
        strcpy(buffer, writeContent);

        file.preWrite(buffer, 512, 0);  // O_DIRECT 需要 512 对齐
        auto result = co_await file.commit();

        if (!result) {
            LogError("[Epoll/AIO] Write failed: {}", result.error().message());
        } else {
            auto& results = result.value();
            if (!results.empty() && results[0] >= 0) {
                LogInfo("[Epoll/AIO] Written {} bytes", results[0]);
                LogInfo("[Epoll/AIO] Test 3 PASSED");
            } else {
                LogError("[Epoll/AIO] Test 3 FAILED");
            }
        }

        galay::async::AioFile::freeAlignedBuffer(buffer);
        std::remove(writeTestFile);
    }

    std::remove(aioTestFile);
    LogInfo("=== Epoll Test Finished ===\n");
    *done = true;
    co_return;
}

void runEpollTest() {
    EpollScheduler scheduler;
    scheduler.start();

    std::atomic<bool> done{false};
    scheduler.spawn(testEpollFileIO(&scheduler, &done));

    // 等待协程完成
    while (!done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    scheduler.stop();
}
#endif

#ifdef USE_IOURING
// io_uring 平台测试 - 所有测试在一个协程中完成
Coroutine testIOUringFileIO(IOUringScheduler* scheduler, std::atomic<bool>* done) {
    LogInfo("=== io_uring (Linux) File IO Test ===");

    // 测试1: 读取已有文件
    {
        LogInfo("[io_uring] Test 1: File read...");
        galay::async::AsyncFile file(scheduler);
        auto openResult = file.open(TEST_FILE, galay::async::FileOpenMode::Read);
        if (!openResult) {
            LogError("[io_uring] Failed to open file for read: {}", openResult.error().message());
            *done = true;
            co_return;
        }

        char buffer[1024] = {0};
        auto result = co_await file.read(buffer, sizeof(buffer) - 1, 0);

        if (!result) {
            LogError("[io_uring] Read failed: {}", result.error().message());
        } else {
            auto& bytes = result.value();
            LogInfo("[io_uring] Read {} bytes: {}", bytes.size(), bytes.toStringView());
            if (bytes.toStringView() == TEST_CONTENT) {
                LogInfo("[io_uring] Test 1 PASSED: Content matches");
            } else {
                LogError("[io_uring] Test 1 FAILED: Content mismatch");
            }
        }
    }

    // 测试2: 写入文件然后读回验证
    {
        LogInfo("[io_uring] Test 2: File write and read back...");
        const char* writeTestFile = "/tmp/galay_iouring_write_test.txt";

        galay::async::AsyncFile file(scheduler);
        auto openResult = file.open(writeTestFile, galay::async::FileOpenMode::ReadWrite);
        if (!openResult) {
            LogError("[io_uring] Failed to open file for write: {}", openResult.error().message());
            *done = true;
            co_return;
        }

        const char* writeContent = "Written by io_uring AsyncFile test!\n";
        auto writeResult = co_await file.write(writeContent, strlen(writeContent), 0);

        if (!writeResult) {
            LogError("[io_uring] Write failed: {}", writeResult.error().message());
        } else {
            LogInfo("[io_uring] Written {} bytes", writeResult.value());

            // 同步到磁盘
            (void)file.sync();

            // 读回验证
            char buffer[1024] = {0};
            auto readResult = co_await file.read(buffer, sizeof(buffer) - 1, 0);
            if (readResult) {
                std::string_view written(writeContent);
                std::string_view readBack = readResult.value().toStringView();
                if (readBack.substr(0, written.size()) == written) {
                    LogInfo("[io_uring] Test 2 PASSED: Write and read back successful");
                } else {
                    LogError("[io_uring] Test 2 FAILED: Content mismatch");
                    LogError("[io_uring] Expected: {}", written);
                    LogError("[io_uring] Got: {}", readBack);
                }
            } else {
                LogError("[io_uring] Test 2 FAILED: Read back error: {}", readResult.error().message());
            }
        }

        std::remove(writeTestFile);
    }

    LogInfo("=== io_uring Test Finished ===\n");
    *done = true;
    co_return;
}

void runIOUringTest() {
    IOUringScheduler scheduler;
    scheduler.start();

    std::atomic<bool> done{false};
    scheduler.spawn(testIOUringFileIO(&scheduler, &done));

    // 等待协程完成
    while (!done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    scheduler.stop();
}
#endif

int main() {
    LogInfo("========================================");
    LogInfo("Galay Kernel File IO Test");
    LogInfo("========================================\n");

    // 创建测试文件
    createTestFile();

#ifdef USE_KQUEUE
    runKqueueTest();
#elif defined(USE_EPOLL)
    runEpollTest();
#elif defined(USE_IOURING)
    runIOUringTest();
#else
    LogWarn("No supported platform detected (kqueue/epoll/io_uring)");
#endif

    // 清理
    cleanupTestFile();

    LogInfo("========================================");
    LogInfo("All File IO Tests Completed");
    LogInfo("========================================");

    return 0;
}
