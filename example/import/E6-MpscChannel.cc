import galay.kernel;

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace galay::kernel;

namespace {
constexpr int kProducerCount = 3;
constexpr int kMessagesPerProducer = 20;
constexpr int kExpectedTotal = kProducerCount * kMessagesPerProducer;

std::atomic<int> g_received{0};
std::atomic<long long> g_sum{0};
std::atomic<bool> g_done{false};

Coroutine consumer(MpscChannel<int>* channel) {
    while (g_received.load(std::memory_order_acquire) < kExpectedTotal) {
        auto value = co_await channel->recv();
        if (!value) {
            continue;
        }
        g_sum.fetch_add(value.value(), std::memory_order_relaxed);
        g_received.fetch_add(1, std::memory_order_relaxed);
    }

    g_done.store(true, std::memory_order_release);
    co_return;
}

void producer(MpscChannel<int>* channel, int producerId) {
    for (int i = 1; i <= kMessagesPerProducer; ++i) {
        const int value = producerId * 100 + i;
        channel->send(value);
    }
}
}  // namespace

int main() {
    MpscChannel<int> channel;
    ComputeScheduler scheduler;
    scheduler.start();

    scheduler.spawn(consumer(&channel));

    std::vector<std::thread> producers;
    producers.reserve(kProducerCount);
    for (int i = 0; i < kProducerCount; ++i) {
        producers.emplace_back(producer, &channel, i + 1);
    }
    for (auto& thread : producers) {
        thread.join();
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!g_done.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    scheduler.stop();

    std::cout << "mpsc import example received=" << g_received.load()
              << ", sum=" << g_sum.load() << "\n";
    return g_done.load(std::memory_order_acquire) ? 0 : 1;
}
