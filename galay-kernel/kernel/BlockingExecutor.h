#ifndef GALAY_KERNEL_BLOCKING_EXECUTOR_H
#define GALAY_KERNEL_BLOCKING_EXECUTOR_H

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>

namespace galay::kernel
{

class BlockingExecutor
{
public:
    BlockingExecutor();
    BlockingExecutor(size_t minWorkers, size_t maxWorkers, std::chrono::milliseconds keepAlive);
    ~BlockingExecutor();

    BlockingExecutor(const BlockingExecutor&) = delete;
    BlockingExecutor& operator=(const BlockingExecutor&) = delete;

    void submit(std::function<void()> task);

private:
    void workerLoop(std::function<void()> initialTask);
    void retireWorkerLocked();
    static size_t defaultMaxWorkers();

    size_t m_minWorkers;
    size_t m_maxWorkers;
    std::chrono::milliseconds m_keepAlive;

    std::mutex m_mutex;
    std::condition_variable m_taskCv;
    std::condition_variable m_shutdownCv;
    std::deque<std::function<void()>> m_tasks;

    size_t m_workerCount{0};
    size_t m_idleWorkers{0};
    bool m_stopping{false};
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_BLOCKING_EXECUTOR_H
