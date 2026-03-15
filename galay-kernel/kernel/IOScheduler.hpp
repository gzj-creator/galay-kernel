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

struct IOSchedulerWorkerState {
    explicit IOSchedulerWorkerState(size_t inject_batch_size = GALAY_SCHEDULER_BATCH_SIZE,
                                    uint32_t lifo_limit = 8,
                                    uint32_t inject_interval = 8)
        : inject_buffer(std::max<size_t>(1, inject_batch_size))
        , lifo_poll_limit(lifo_limit)
        , inject_check_interval(inject_interval)
    {
    }

    void resizeInjectBuffer(size_t inject_batch_size) {
        inject_buffer.resize(std::max<size_t>(1, inject_batch_size));
    }

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

    void scheduleLocalDeferred(TaskRef task) {
        if (!task.isValid()) {
            return;
        }
        local_queue.push_back(std::move(task));
    }

    bool scheduleInjected(TaskRef task) {
        if (!task.isValid()) {
            return false;
        }
        const bool was_empty =
            injected_outstanding.fetch_add(1, std::memory_order_acq_rel) == 0;
        inject_queue.enqueue(std::move(task));
        return was_empty;
    }

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

    bool hasPendingInjected() const {
        return injected_outstanding.load(std::memory_order_acquire) > 0;
    }

    bool shouldCheckInjected() const {
        return polls_since_inject >= inject_check_interval;
    }

    bool hasLocalWork() const {
        return lifo_slot.has_value() || !local_queue.empty();
    }

    void prepareForRun() {
        if (lifo_enabled && lifo_slot.has_value() && consecutive_lifo_polls >= lifo_poll_limit) {
            local_queue.push_back(std::move(*lifo_slot));
            lifo_slot.reset();
            lifo_enabled = false;
            consecutive_lifo_polls = 0;
        }
    }

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

    std::optional<TaskRef> lifo_slot;
    std::deque<TaskRef> local_queue;
    moodycamel::ConcurrentQueue<TaskRef> inject_queue;
    std::vector<TaskRef> inject_buffer;
    std::atomic<uint64_t> injected_outstanding{0};
    uint32_t consecutive_lifo_polls = 0;
    uint32_t lifo_poll_limit = 8;
    uint32_t polls_since_inject = 0;
    uint32_t inject_check_interval = 8;
    bool lifo_enabled = true;
};

// ========== getAwaitable 模板特化 ==========

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

template<>
inline auto IOController::getAwaitable() -> SequenceAwaitableBase* {
    return static_cast<SequenceAwaitableBase*>(m_awaitable[READ]);
};

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

    virtual int addSequence(IOController* controller) = 0;

    /**
     * @brief 删除fd的所有事件
     * @param controller IO控制器
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
    case IOEventType::SEQUENCE:
        m_awaitable[READ] = awaitable;
#ifdef USE_IOURING
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
    case IOEventType::SEQUENCE:
        m_awaitable[READ] = nullptr;
#ifdef USE_IOURING
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
