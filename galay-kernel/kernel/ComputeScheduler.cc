#include "ComputeScheduler.h"

namespace galay::kernel
{

ComputeScheduler::ComputeScheduler(size_t thread_count)
    : m_thread_count(thread_count > 0 ? thread_count : 1)
{
}

ComputeScheduler::~ComputeScheduler()
{
    stop();
}

void ComputeScheduler::start()
{
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;  // 已经在运行
    }

    m_threads.reserve(m_thread_count);
    for (size_t i = 0; i < m_thread_count; ++i) {
        m_threads.emplace_back(&ComputeScheduler::workerLoop, this);
    }
}

void ComputeScheduler::stop()
{
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;  // 已经停止
    }

    // 发送停止信号唤醒所有等待的线程
    for (size_t i = 0; i < m_thread_count; ++i) {
        m_queue.enqueue(ComputeTask{Coroutine{}, true});
    }

    // 等待所有线程结束
    for (auto& thread : m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_threads.clear();
}

void ComputeScheduler::spawn(Coroutine coro)
{
    // 如果协程未绑定 scheduler，绑定到当前 scheduler
    if (!coro.belongScheduler()) {
        coro.belongScheduler(this);
    }
    m_queue.enqueue(ComputeTask{std::move(coro)});
}

void ComputeScheduler::workerLoop()
{
    ComputeTask task;

    while (m_running.load(std::memory_order_acquire)) {
        // 阻塞等待任务，超时 100ms 用于检查 m_running 状态
        if (!m_queue.wait_dequeue_timed(task, std::chrono::milliseconds(100))) {
            continue;
        }

        // 停止信号
        if (task.is_stop_signal) {
            continue;
        }

        // 获取所属调度器
        Scheduler* belong_scheduler = task.coro.belongScheduler();

        // 执行协程
        Scheduler::resume(task.coro);

        // 协程未完成且所属调度器不是自己，spawn 回所属调度器
        // 如果所属调度器是自己，协程会通过其他方式（如 AsyncWaiter）被重新唤醒
        if (!task.coro.isDone() && belong_scheduler && belong_scheduler != this) {
            belong_scheduler->spawn(std::move(task.coro));
        }
    }

    // 退出前处理剩余任务
    while (m_queue.try_dequeue(task)) {
        if (task.is_stop_signal) {
            continue;
        }
        Scheduler* belong_scheduler = task.coro.belongScheduler();
        Scheduler::resume(task.coro);
        if (!task.coro.isDone() && belong_scheduler && belong_scheduler != this) {
            belong_scheduler->spawn(std::move(task.coro));
        }
    }
}

} // namespace galay::kernel
