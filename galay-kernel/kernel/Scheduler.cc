#include "Scheduler.h"
#include "Coroutine.h"

namespace galay::kernel
{

void Scheduler::resume(Coroutine& co) {
    if (!co.m_data || !co.m_data->m_handle) {
        return;
    }

    co.m_data->m_status.store(CoroutineStatus::Running, std::memory_order_relaxed);
    co.m_data->m_handle.resume();

    // 检查协程是否完成
    if (co.m_data->m_handle.done()) {
        co.m_data->m_status.store(CoroutineStatus::Finished, std::memory_order_relaxed);

        // 如果有后续协程，提交到调度器执行
        if (co.m_data->m_next.m_data && co.m_data->m_next.m_data->m_handle) {
            co.m_data->m_next.belongScheduler(co.m_data->m_scheduler);
            spawn(std::move(co.m_data->m_next));
        }
    } else {
        co.m_data->m_status.store(CoroutineStatus::Suspended, std::memory_order_relaxed);
    }
}

}