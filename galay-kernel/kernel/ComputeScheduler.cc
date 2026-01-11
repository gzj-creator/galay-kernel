#include "ComputeScheduler.h"

namespace galay::kernel
{

ComputeScheduler::ComputeScheduler()
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

    m_thread = std::thread([this]() {
        m_threadId = std::this_thread::get_id();  // 设置调度器线程ID
        workerLoop();
    });
}

void ComputeScheduler::stop()
{
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;  // 已经停止
    }

    // 发送停止信号唤醒等待的线程
    m_queue.enqueue(ComputeTask{Coroutine{}, true});

    // 等待线程结束
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void ComputeScheduler::spawn(Coroutine coro)
{
    // 如果协程未绑定 scheduler，绑定到当前 scheduler
    if (!coro.belongScheduler()) {
        coro.belongScheduler(this);
        coro.threadId(m_threadId);
    }
    m_queue.enqueue(ComputeTask{std::move(coro)});
}

void ComputeScheduler::workerLoop()
{
    ComputeTask task;

    while (true) {
        // 阻塞等待任务（无超时，由任务驱动）
        if (!m_queue.wait_dequeue_timed(task, std::chrono::milliseconds(1))) {
            continue;
        }
        // 停止信号
        if (task.is_stop_signal) {
            break;
        }
        // 执行协程
        Scheduler::resume(task.coro);
    }

    // 退出前处理剩余任务
    while (m_queue.try_dequeue(task)) {
        if (task.is_stop_signal) {
            continue;
        }
        Scheduler::resume(task.coro);
    }
}

} // namespace galay::kernel
