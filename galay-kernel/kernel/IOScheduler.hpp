#ifndef GALAY_KERNEL_IOSCHEDULER_HPP
#define GALAY_KERNEL_IOSCHEDULER_HPP

#include "galay-kernel/common/Defn.hpp"
#include "galay-kernel/common/Error.h"
#include "Scheduler.hpp"
#include "IOController.hpp"
#include "Awaitable.h"
#include "galay-kernel/common/TimerManager.hpp"
#include <algorithm>
#include <atomic>
#include <deque>
#include <optional>
#include <concurrentqueue/moodycamel/concurrentqueue.h>

namespace galay::kernel
{

/**
 * @brief IO 调度器执行线程的本地状态
 * @details 保存工作窃取缓冲、本地队列以及 LIFO 调度策略状态。
 * 该结构仅应在所属调度器线程内访问；`inject_queue` 允许其他线程安全地注入任务。
 */
struct IOSchedulerWorkerState {
    /**
     * @brief 构造工作线程局部队列状态
     * @param inject_batch_size 单次从跨线程注入队列中批量拉取的最大数量
     * @param lifo_limit 连续走 LIFO 槽位的最大次数，超过后回退到 FIFO
     * @param inject_interval 轮询多少次本地任务后检查一次注入队列
     */
    explicit IOSchedulerWorkerState(size_t inject_batch_size = GALAY_SCHEDULER_BATCH_SIZE,
                                    uint32_t lifo_limit = 8,
                                    uint32_t inject_interval = 8)
        : inject_buffer(std::max<size_t>(1, inject_batch_size))
        , lifo_poll_limit(lifo_limit)
        , inject_check_interval(inject_interval)
    {
    }

    /**
     * @brief 调整跨线程注入批量缓冲区大小
     * @param inject_batch_size 目标批量大小；最小会被修正为 1
     */
    void resizeInjectBuffer(size_t inject_batch_size) {
        inject_buffer.resize(std::max<size_t>(1, inject_batch_size));
    }

    /**
     * @brief 将任务推入本地执行队列
     * @param task 待入队的任务；无效任务会被忽略
     * @details 优先复用 LIFO 槽位以减少最近恢复任务的调度延迟
     */
    void scheduleLocal(TaskRef task) {
        if (!task.isValid()) {
            return;
        }
        if (lifo_enabled) {
            if (lifo_slot.has_value()) {
                local_queue.push_back(std::move(*lifo_slot));
            }
            lifo_slot = std::move(task);
            return;
        }
        local_queue.push_back(std::move(task));
    }

    /**
     * @brief 将任务以 FIFO 方式追加到本地队列尾部
     * @param task 待入队的任务；无效任务会被忽略
     */
    void scheduleLocalDeferred(TaskRef task) {
        if (!task.isValid()) {
            return;
        }
        local_queue.push_back(std::move(task));
    }

    /**
     * @brief 从其他线程安全地注入任务
     * @param task 待入队任务；无效任务会返回 false
     * @return true 注入前队列为空，调用方通常应唤醒阻塞中的调度器；false 注入后无需额外唤醒
     */
    bool scheduleInjected(TaskRef task) {
        if (!task.isValid()) {
            return false;
        }
        const bool was_empty =
            injected_outstanding.fetch_add(1, std::memory_order_acq_rel) == 0;
        inject_queue.enqueue(std::move(task));
        return was_empty;
    }

    /**
     * @brief 将跨线程注入队列中的任务搬运到本地队列
     * @param max_batch 单次最多拉取的任务数；传 0 表示使用缓冲区上限
     * @return 实际拉取并转移到本地队列的任务数量
     */
    size_t drainInjected(size_t max_batch = 0) {
        if (inject_buffer.empty()) {
            return 0;
        }
        const size_t limit =
            (max_batch == 0) ? inject_buffer.size() : std::min(max_batch, inject_buffer.size());
        const size_t count = inject_queue.try_dequeue_bulk(inject_buffer.data(), limit);
        for (size_t i = 0; i < count; ++i) {
            local_queue.push_back(std::move(inject_buffer[i]));
        }
        if (count > 0) {
            injected_outstanding.fetch_sub(count, std::memory_order_acq_rel);
        }
        polls_since_inject = 0;
        return count;
    }

    /**
     * @brief 判断是否仍有跨线程注入任务待处理
     * @return true 仍有任务未从注入队列转移到本地队列
     */
    bool hasPendingInjected() const {
        return injected_outstanding.load(std::memory_order_acquire) > 0;
    }

    /**
     * @brief 判断当前轮询周期是否应该检查注入队列
     * @return true 已达到检查阈值
     */
    bool shouldCheckInjected() const {
        return polls_since_inject >= inject_check_interval;
    }

    /**
     * @brief 判断本地执行队列是否仍有任务
     * @return true LIFO 槽位或 FIFO 队列中仍有待执行任务
     */
    bool hasLocalWork() const {
        return lifo_slot.has_value() || !local_queue.empty();
    }

    /**
     * @brief 在取任务前整理本地调度状态
     * @details 当连续命中 LIFO 槽位过多时，将其回退到 FIFO 队列避免饥饿
     */
    void prepareForRun() {
        if (lifo_enabled && lifo_slot.has_value() && consecutive_lifo_polls >= lifo_poll_limit) {
            local_queue.push_back(std::move(*lifo_slot));
            lifo_slot.reset();
            lifo_enabled = false;
            consecutive_lifo_polls = 0;
        }
    }

    /**
     * @brief 从本地状态中取出下一条待执行任务
     * @param out 成功时写入取出的任务
     * @return true 取到了任务；false 本地无任务可执行
     */
    bool popNext(TaskRef& out) {
        prepareForRun();

        if (lifo_slot.has_value()) {
            out = std::move(*lifo_slot);
            lifo_slot.reset();
            ++consecutive_lifo_polls;
            ++polls_since_inject;
            return true;
        }

        if (!local_queue.empty()) {
            out = std::move(local_queue.front());
            local_queue.pop_front();
            lifo_enabled = true;
            consecutive_lifo_polls = 0;
            ++polls_since_inject;
            return true;
        }

        return false;
    }

    std::optional<TaskRef> lifo_slot;  ///< 最近一次入队的任务，优先以内联 LIFO 方式调度
    std::deque<TaskRef> local_queue;  ///< 调度器线程本地 FIFO 队列
    moodycamel::ConcurrentQueue<TaskRef> inject_queue;  ///< 其他线程注入任务的无锁队列
    std::vector<TaskRef> inject_buffer;  ///< 从 inject_queue 批量转移任务时复用的临时缓冲
    std::atomic<uint64_t> injected_outstanding{0};  ///< 尚未搬运到本地队列的注入任务数
    uint32_t consecutive_lifo_polls = 0;  ///< 连续命中 lifo_slot 的次数
    uint32_t lifo_poll_limit = 8;  ///< 允许连续走 LIFO 的最大次数
    uint32_t polls_since_inject = 0;  ///< 距离上次检查 inject_queue 已轮询的任务数
    uint32_t inject_check_interval = 8;  ///< 检查 inject_queue 的轮询间隔
    bool lifo_enabled = true;  ///< 是否允许优先从 lifo_slot 取任务
};

/**
 * @brief IOController::getAwaitable 的显式特化集合
 * @details 这些访问器把 READ/WRITE 槽位上的 `void*` awaitable 安全转换为具体类型。
 */

template<>
inline auto IOController::getAwaitable() -> AcceptAwaitable* {
    return static_cast<AcceptAwaitable*>(m_awaitable[READ]);
}

template<>
inline auto IOController::getAwaitable() -> RecvAwaitable* {
    return static_cast<RecvAwaitable*>(m_awaitable[READ]);
}

template<>
inline auto IOController::getAwaitable() -> SendAwaitable* {
    return static_cast<SendAwaitable*>(m_awaitable[WRITE]);
}

template<>
inline auto IOController::getAwaitable() -> ConnectAwaitable* {
    return static_cast<ConnectAwaitable*>(m_awaitable[WRITE]);
}

template<>
inline auto IOController::getAwaitable() -> RecvFromAwaitable* {
    return static_cast<RecvFromAwaitable*>(m_awaitable[READ]);
}

template<>
inline auto IOController::getAwaitable() -> SendToAwaitable* {
    return static_cast<SendToAwaitable*>(m_awaitable[WRITE]);
}

template<>
inline auto IOController::getAwaitable() -> FileReadAwaitable* {
    return static_cast<FileReadAwaitable*>(m_awaitable[READ]);
}

template<>
inline auto IOController::getAwaitable() -> FileWriteAwaitable* {
    return static_cast<FileWriteAwaitable*>(m_awaitable[WRITE]);
}

template<>
inline auto IOController::getAwaitable() -> FileWatchAwaitable* {
    return static_cast<FileWatchAwaitable*>(m_awaitable[READ]);
}

template<>
inline auto IOController::getAwaitable() -> ReadvAwaitable* {
    return static_cast<ReadvAwaitable*>(m_awaitable[READ]);
}

template<>
inline auto IOController::getAwaitable() -> WritevAwaitable* {
    return static_cast<WritevAwaitable*>(m_awaitable[WRITE]);
}

template<>
inline auto IOController::getAwaitable() -> SendFileAwaitable* {
    return static_cast<SendFileAwaitable*>(m_awaitable[WRITE]);
}

/**
 * @brief 完成 awaitable 并在需要时唤醒关联协程
 * @tparam Awaitable 具体 awaitable 类型
 * @param controller 触发完成的 IO 控制器
 * @param awaitable 与该 IO 完成事件关联的 awaitable
 * @note 仅当 `handleComplete()` 返回 true 时才真正唤醒协程
 */
template<typename Awaitable>
inline void completeAwaitableAndWake(IOController* controller, Awaitable* awaitable) {
    if (awaitable && awaitable->handleComplete(controller->m_handle)) {
        awaitable->m_waker.wakeUp();
    }
}

/**
 * @brief IO调度器接口
 *
 * @details 扩展Scheduler，添加IO事件注册接口。
 * 具体实现需要处理平台相关的IO多路复用机制。
 * 接口全都是非线程安全的，只能在调度器内部使用或绑定当前调度器的协程使用
 *
 * @note
 * - macOS: KqueueScheduler (kqueue)
 * - Linux: EpollScheduler (epoll) 或 IOUringScheduler (io_uring)
 * - Windows: IOCPScheduler (IOCP)
 *
 * @see KqueueScheduler
 */
class IOScheduler: public Scheduler
{
public:
    /**
     * @brief 返回调度器类型
     * @return 固定返回 kIOScheduler
     */
    SchedulerType type() override {
        return kIOScheduler;
    }

    /**
     * @brief 注册Accept事件
     * @param controller IO控制器
     * @return 1表示立即完成，0表示已注册等待，<0表示错误
     */
    virtual int addAccept(IOController* controller) = 0;

    /**
     * @brief 注册Connect事件
     * @param controller IO控制器
     * @return 1表示立即完成，0表示已注册等待，<0表示错误
     */
    virtual int addConnect(IOController* controller) = 0;

    /**
     * @brief 注册Recv事件
     * @param controller IO控制器
     * @return 1表示立即完成，0表示已注册等待，<0表示错误
     */
    virtual int addRecv(IOController* controller) = 0;

    /**
     * @brief 注册Send事件
     * @param controller IO控制器
     * @return 1表示立即完成，0表示已注册等待，<0表示错误
     */
    virtual int addSend(IOController* controller) = 0;

    /**
     * @brief 注册Readv事件（scatter-gather 读取）
     * @param controller IO控制器
     * @return 1表示立即完成，0表示已注册等待，<0表示错误
     */
    virtual int addReadv(IOController* controller) = 0;

    /**
     * @brief 注册Writev事件（scatter-gather 写入）
     * @param controller IO控制器
     * @return 1表示立即完成，0表示已注册等待，<0表示错误
     */
    virtual int addWritev(IOController* controller) = 0;

    /**
     * @brief 关闭文件描述符
     * @param controller IO控制器
     * @return 0表示成功，<0表示错误
     */
    virtual int addClose(IOController* contoller) = 0;

    /**
     * @brief 注册文件读取事件
     * @param controller IO控制器
     * @return 1表示立即完成，0表示已注册等待，<0表示错误
     */
    virtual int addFileRead(IOController* controller) = 0;

    /**
     * @brief 注册文件写入事件
     * @param controller IO控制器
     * @return 1表示立即完成，0表示已注册等待，<0表示错误
     */
    virtual int addFileWrite(IOController* controller) = 0;

    /**
     * @brief 注册RecvFrom事件（UDP接收）
     * @param controller IO控制器
     * @return 1表示立即完成，0表示已注册等待，<0表示错误
     */
    virtual int addRecvFrom(IOController* controller) = 0;

    /**
     * @brief 注册SendTo事件（UDP发送）
     * @param controller IO控制器
     * @return 1表示立即完成，0表示已注册等待，<0表示错误
     */
    virtual int addSendTo(IOController* controller) = 0;

    /**
     * @brief 注册文件监控事件
     * @param controller IO控制器
     * @return 1表示立即完成，0表示已注册等待，<0表示错误
     */
    virtual int addFileWatch(IOController* controller) = 0;

    /**
     * @brief 注册SendFile事件（零拷贝发送文件）
     * @param controller IO控制器
     * @return 1表示立即完成，0表示已注册等待，<0表示错误
     */
    virtual int addSendFile(IOController* controller) = 0;

    /**
     * @brief 注册组合式序列 IO 事件
     * @param controller IO 控制器
     * @return 1 表示序列立即完成，0 表示已登记等待，<0 表示注册失败
     * @note 主要服务于需要 READ/WRITE 配对推进的 Sequence awaitable
     */
    virtual int addSequence(IOController* controller) = 0;

    /**
     * @brief 删除fd的所有事件
     * @param controller IO控制器
     * @return 0 表示删除成功，<0 表示删除失败
     */
    virtual int remove(IOController* controller) = 0;

    /**
     * @brief 查询调度器最近一次内部错误
     * @return 如果存在内部错误则返回 IOError；否则返回 std::nullopt
     */
    virtual std::optional<IOError> lastError() const {
        return std::nullopt;
    }


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
     * @return true 定时器已加入当前调度器的时间轮；false 插入失败
     */
    bool addTimer(Timer::ptr timer) override {
        return m_timer_manager.push(timer);
    }
protected:
    TimingWheelTimerManager m_timer_manager;
};


inline bool IOController::fillAwaitable(IOEventType type, void* awaitable) {
    m_type |= type;
    switch (type) {
    case IOEventType::RECV:
    case IOEventType::READV:
    case IOEventType::FILEREAD:
    case IOEventType::RECVFROM:
    case IOEventType::ACCEPT:
    case IOEventType::FILEWATCH:
        m_awaitable[READ] = awaitable;
#ifdef USE_IOURING
        advanceSqeGeneration(READ);
#endif
        break;
    case IOEventType::SEQUENCE:
#ifdef USE_IOURING
        m_awaitable[READ] = awaitable;
        advanceSqeGeneration(READ);
#endif
        break;
    case IOEventType::SEND:
    case IOEventType::WRITEV:
    case IOEventType::SENDFILE:
    case IOEventType::FILEWRITE:
    case IOEventType::SENDTO:
    case IOEventType::CONNECT:
        m_awaitable[WRITE] = awaitable;
#ifdef USE_IOURING
        advanceSqeGeneration(WRITE);
#endif
        break;
    default:
        return false;
    }
    return true;
}

inline void IOController::removeAwaitable(IOEventType type) {
    m_type &= ~type;
    switch (type) {
    case IOEventType::RECV:
    case IOEventType::READV:
    case IOEventType::FILEREAD:
    case IOEventType::RECVFROM:
    case IOEventType::ACCEPT:
    case IOEventType::FILEWATCH:
        m_awaitable[READ] = nullptr;
#ifdef USE_IOURING
        advanceSqeGeneration(READ);
#endif
        break;
    case IOEventType::SEQUENCE:
#ifdef USE_IOURING
        m_awaitable[READ] = nullptr;
        advanceSqeGeneration(READ);
#endif
        break;
    case IOEventType::SEND:
    case IOEventType::WRITEV:
    case IOEventType::SENDFILE:
    case IOEventType::FILEWRITE:
    case IOEventType::SENDTO:
    case IOEventType::CONNECT:
        m_awaitable[WRITE] = nullptr;
#ifdef USE_IOURING
        advanceSqeGeneration(WRITE);
#endif
        break;
    default:
        break;
    }
}   



}

#endif
