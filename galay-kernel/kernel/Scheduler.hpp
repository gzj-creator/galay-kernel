/**
 * @file Scheduler.h
 * @brief 协程调度器基类和IO控制器
 * @author galay-kernel
 * @version 1.0.0
 *
 * @details 定义协程调度器的基类接口和IO事件控制器。
 * 包含：
 * - Scheduler: 协程调度器基类
 * - IOScheduler: IO调度器接口
 * - IOController: IO事件控制器
 *
 * @note 具体实现见 KqueueScheduler (macOS), EpollScheduler (Linux)
 */

#ifndef GALAY_KERNEL_SCHEDULER_HPP
#define GALAY_KERNEL_SCHEDULER_HPP

#include "galay-kernel/common/TimerManager.hpp"
#include "Coroutine.h"
#include <thread>

#ifdef USE_IOURING
#include <linux/time_types.h>
#endif

/**
 * @def GALAY_SCHEDULER_MAX_EVENTS
 * @brief 调度器单次处理的最大事件数
 * @note 可在编译时通过 -DGALAY_SCHEDULER_MAX_EVENTS=xxx 覆盖
 */
#ifndef GALAY_SCHEDULER_MAX_EVENTS
#define GALAY_SCHEDULER_MAX_EVENTS 1024
#endif

/**
 * @def GALAY_SCHEDULER_BATCH_SIZE
 * @brief 协程批量处理大小
 * @note 可在编译时通过 -DGALAY_SCHEDULER_BATCH_SIZE=xxx 覆盖
 */
#ifndef GALAY_SCHEDULER_BATCH_SIZE
#define GALAY_SCHEDULER_BATCH_SIZE 256
#endif

/**
 * @def GALAY_SCHEDULER_CHECK_INTERVAL_MS
 * @brief 调度器检查间隔（毫秒）
 * @note 可在编译时通过 -DGALAY_SCHEDULER_CHECK_INTERVAL_MS=xxx 覆盖
 */
#ifndef GALAY_SCHEDULER_CHECK_INTERVAL_MS
#define GALAY_SCHEDULER_CHECK_INTERVAL_MS 1
#endif

namespace galay::kernel
{

class Coroutine;

enum SchedulerType {
    kIOScheduler,
    kComputeScheduler
}; 

/**
 * @brief 协程调度器基类
 *
 * @details 定义协程调度的基本接口。
 * 所有调度器实现都必须继承此类。
 *
 * @see IOScheduler, KqueueScheduler
 */
class Scheduler {
public:
    /**
     * @brief 提交协程到调度器执行
     * @param co 要执行的协程
     * @note 协程会被加入调度队列，由调度器线程执行
     */
    virtual void spawn(Coroutine co) = 0;

    /**
     * @brief 获取调度器所属线程ID
     * @return 线程ID
     */
    std::thread::id threadId() const { return m_threadId; }

    /**
     * @brief 替换调度器的定时器管理器
     * @param manager 新的时间轮定时器管理器（右值引用）
     * @details 用于自定义时间轮配置（wheelSize、tickDuration）以适应不同的超时场景
     * @note 应在调度器启动前调用，避免运行时替换导致定时器丢失
     * @example
     * ```cpp
     * // 创建适合短超时场景的时间轮（60秒范围，1秒精度）
     * TimingWheelTimerManager manager(60, 1000000000ULL);
     * scheduler->replaceTimerManager(std::move(manager));
     * ```
     */
    void replaceTimerManager(TimingWheelTimerManager&& manager) {
        m_timer_manager = std::move(manager);
    }

    /**
     * @brief 注册定时器
     * @details 添加任务量不大且数量不是特别多的定时任务，如IO超时，sleep等
     * @param timer 定时器共享指针
     */
    bool addTimer(Timer::ptr timer) {
        return m_timer_manager.push(timer);
    }

    /**
     * @brief 返回Scheduler类型
     * @return  SchedulerType 调度器类型
     */

    virtual SchedulerType type() = 0;
protected:
    /**
     * @brief 恢复协程执行
     * @param co 要恢复的协程
     * @note 仅供调度器内部使用
     */
    void resume(Coroutine& co);

    std::thread::id m_threadId;  ///< 调度器所属线程ID，在 start() 时设置

    TimingWheelTimerManager m_timer_manager;
};


inline void Scheduler::resume(Coroutine& co) {
    if (!co.m_data || !co.m_data->m_handle || co.m_data->m_handle.done()) {
        return;
    }
    co.m_data->m_handle.resume();
}


} // namespace galay::kernel

#endif // GALAY_KERNEL_SCHEDULER_H
