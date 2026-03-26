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

/**
 * @brief 自适应阻塞任务执行器
 * @details 为 Runtime 的 `spawnBlocking()` 提供线程池，适合执行不可协程化的阻塞调用。
 */
class BlockingExecutor
{
public:
    BlockingExecutor();  ///< 使用默认线程数与空闲超时配置构造执行器
    BlockingExecutor(size_t minWorkers, size_t maxWorkers, std::chrono::milliseconds keepAlive);  ///< 自定义最小/最大线程数和空闲超时时间
    ~BlockingExecutor();  ///< 停止执行器并等待工作线程全部退出

    BlockingExecutor(const BlockingExecutor&) = delete;
    BlockingExecutor& operator=(const BlockingExecutor&) = delete;

    void submit(std::function<void()> task);  ///< 提交一个阻塞任务；必要时会拉起额外工作线程

private:
    void workerLoop(std::function<void()> initialTask);  ///< 工作线程主循环，持续拉取并执行阻塞任务
    void retireWorkerLocked();  ///< 在持锁状态下回收一个空闲工作线程计数
    static size_t defaultMaxWorkers();  ///< 根据当前机器并发度推导默认最大线程数

    size_t m_minWorkers;  ///< 最少保留的工作线程数
    size_t m_maxWorkers;  ///< 允许扩张到的最大工作线程数
    std::chrono::milliseconds m_keepAlive;  ///< 空闲线程超过该时间后允许退出

    std::mutex m_mutex;  ///< 保护任务队列和线程状态
    std::condition_variable m_taskCv;  ///< 新任务到达时唤醒工作线程
    std::condition_variable m_shutdownCv;  ///< 析构等待所有线程退出时使用
    std::deque<std::function<void()>> m_tasks;  ///< 待执行阻塞任务队列

    size_t m_workerCount{0};  ///< 当前已创建的工作线程数
    size_t m_idleWorkers{0};  ///< 当前空闲工作线程数
    bool m_stopping{false};  ///< 执行器是否处于停止中
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_BLOCKING_EXECUTOR_H
