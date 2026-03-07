#include "galay-kernel/concurrency/MpscChannel.h"
#include "galay-kernel/kernel/ComputeScheduler.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

using namespace galay::kernel;
using namespace std::chrono_literals;

namespace {

constexpr int64_t kMessageCount = 1'000'000;

std::atomic<bool> g_consumer_done{false};
std::atomic<bool> g_producer_done{false};
std::atomic<int64_t> g_received{0};

bool waitUntil(const std::atomic<bool>& flag,
               std::chrono::milliseconds timeout = 5000ms,
               std::chrono::milliseconds step = 2ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (flag.load(std::memory_order_acquire)) {
            return true;
        }
        std::this_thread::sleep_for(step);
    }
    return flag.load(std::memory_order_acquire);
}

Coroutine batchConsumer(MpscChannel<int64_t>* channel) {
    int64_t received = 0;
    while (received < kMessageCount) {
        auto batch = co_await channel->recvBatch(256);
        if (!batch) {
            continue;
        }
        received += static_cast<int64_t>(batch->size());
        g_received.store(received, std::memory_order_release);
    }

    g_consumer_done.store(true, std::memory_order_release);
    co_return;
}

}  // namespace

int main() {
    MpscChannel<int64_t> channel;
    ComputeScheduler scheduler;
    scheduler.start();
    scheduler.spawn(batchConsumer(&channel));

    std::thread producer([&]() {
        for (int64_t i = 0; i < kMessageCount; ++i) {
            channel.send(i);
        }
        g_producer_done.store(true, std::memory_order_release);
    });

    const bool done = waitUntil(g_consumer_done);
    producer.join();
    scheduler.stop();

    if (!done) {
        std::cerr << "[T38] batch consumer stalled, producer_done="
                  << g_producer_done.load(std::memory_order_acquire)
                  << " received=" << g_received.load(std::memory_order_acquire)
                  << " channel.size=" << channel.size() << "\n";
        return 1;
    }

    if (g_received.load(std::memory_order_acquire) != kMessageCount) {
        std::cerr << "[T38] expected received=" << kMessageCount
                  << ", got " << g_received.load(std::memory_order_acquire) << "\n";
        return 1;
    }

    std::cout << "T38-MpscBatchReceiveStress PASS\n";
    return 0;
}
