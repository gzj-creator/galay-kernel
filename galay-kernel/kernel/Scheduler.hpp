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

#include "galay-kernel/common/Timer.hpp"
#include "Coroutine.h"
#include <atomic>
#include <cstdint>
#include <optional>
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
     * @brief 虚析构函数
     */
    virtual ~Scheduler() = default;

    /**
     * @brief 启动调度器
     * @note 子类必须实现此方法
     */
    virtual void start() = 0;

    /**
     * @brief 停止调度器
     * @note 子类必须实现此方法
     */
    virtual void stop() = 0;

    /**
     * @brief 提交协程到调度器执行
     * @param co 要执行的协程
     * @note 协程会被加入调度队列，由调度器线程执行
     */
    virtual bool spawn(Coroutine co) = 0;

    /**
     * @brief 直接提交已绑定调度器的任务引用
     * @param task 已绑定当前调度器的任务引用
     * @note 默认回退为包装成 Coroutine 再走 spawn；高性能调度器应覆写以避免额外包装
     */
    virtual bool schedule(TaskRef task) {
        return spawn(Coroutine(std::move(task)));
    }

    /**
     * @brief 延后提交协程到调度器执行
     * @param co 要执行的协程
     * @note 默认行为与 spawn 相同；支持本地 worker 的调度器可覆写为让已就绪任务先执行
     */
    virtual bool spawnDeferred(Coroutine co) {
        return spawn(std::move(co));
    }

    /**
     * @brief 延后提交已绑定调度器的任务引用
     * @param task 已绑定当前调度器的任务引用
     * @note 默认回退为包装成 Coroutine 再走 spawnDeferred
     */
    virtual bool scheduleDeferred(TaskRef task) {
        return spawnDeferred(Coroutine(std::move(task)));
    }

    /**
     * @brief 提交协程到调度器执行
     * @param co 要执行的协程
     * @note 协程立即执行，需要该接口只适合在协程内部调用以避免调度导致的延迟，已经spwan的协程调用此接口无效
     */
    virtual bool spawnImmidiately(Coroutine co) = 0;

    /**
     * @brief 添加定时器到内部时间轮
     * @param 定时器
     */
    virtual bool addTimer(Timer::ptr timer) = 0;

    /**
     * @brief 配置或取消调度器线程绑核
     * @param cpu_id 目标 CPU 核心编号（从 0 开始）；传 std::nullopt 表示取消绑核
     * @return true 配置成功；false 参数无效或平台不支持
     * @note 默认不绑核，仅在主动调用本接口后生效
     * @note 在 start() 之前调用可保证立即生效
     */
    bool setAffinity(std::optional<uint32_t> cpu_id);

    /**
     * @brief 获取调度器所属线程ID
     * @return 线程ID
     */
    std::thread::id threadId() const { return m_threadId; }

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
    void resume(TaskRef& task);
    bool applyConfiguredAffinity();
    std::thread::id m_threadId;  ///< 调度器所属线程ID，在 start() 时设置

private:
    static constexpr int32_t kNoAffinity = -1;
    std::atomic<int32_t> m_affinity_cpu{kNoAffinity};
};


inline void Scheduler::resume(Coroutine& co) {
    auto* state = co.m_task.state();
    if (!state || !state->m_handle || state->m_done.load(std::memory_order_relaxed)) {
        return;
    }
    state->m_queued.store(false, std::memory_order_release);
    state->m_handle.resume();
}

inline void Scheduler::resume(TaskRef& task) {
    auto* state = task.state();
    if (!state || !state->m_handle || state->m_done.load(std::memory_order_relaxed)) {
        return;
    }
    state->m_queued.store(false, std::memory_order_release);
    state->m_handle.resume();
}


} // namespace galay::kernel

#endif // GALAY_KERNEL_SCHEDULER_H
