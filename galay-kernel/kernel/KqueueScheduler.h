#ifndef GALAY_KERNEL_KQUEUE_SCHEDULER_H
#define GALAY_KERNEL_KQUEUE_SCHEDULER_H

#include "IOScheduler.hpp"
#include "KqueueReactor.h"
#include "SchedulerCore.h"
#include "WakeCoordinator.h"

#ifdef USE_KQUEUE

#include <sys/event.h>
#include <vector>
#include <atomic>
#include <thread>
#include <cstdint>

// Scheduler configuration macros
#ifndef GALAY_SCHEDULER_MAX_EVENTS
#define GALAY_SCHEDULER_MAX_EVENTS 1024
#endif

#ifndef GALAY_SCHEDULER_BATCH_SIZE
#define GALAY_SCHEDULER_BATCH_SIZE 256
#endif

#ifndef GALAY_SCHEDULER_CHECK_INTERVAL_MS
#define GALAY_SCHEDULER_CHECK_INTERVAL_MS 1
#endif

namespace galay::kernel
{

struct SchedulerTestAccess;

#define  OK 1

/**
 * @brief Kqueue 调度器 (macOS/BSD)
 * @details 基于 kqueue 管理 socket、文件和定时相关 IO 事件。
 * 除 `schedule*()` 之外的事件注册接口默认只允许在所属调度器线程或其绑定协程内调用。
 */
class KqueueScheduler: public IOScheduler
{
public:
    /**
     * @brief 构造 kqueue 调度器
     * @param max_events kevent 单次最多处理的事件数
     * @param batch_size 跨线程注入任务的批处理大小
     * @param check_interval_ms 空闲轮询时检查超时和注入队列的间隔
     */
    KqueueScheduler(int max_events = GALAY_SCHEDULER_MAX_EVENTS,
                    int batch_size = GALAY_SCHEDULER_BATCH_SIZE,
                    int check_interval_ms = GALAY_SCHEDULER_CHECK_INTERVAL_MS);

    /**
     * @brief 析构调度器
     * @note 若事件循环仍在运行，析构会负责停止线程并回收资源
     */
    ~KqueueScheduler();

    KqueueScheduler(const KqueueScheduler&) = delete;
    KqueueScheduler& operator=(const KqueueScheduler&) = delete;

    void start() override;  ///< 启动 kqueue 事件循环线程并初始化底层资源
    void stop() override;   ///< 请求退出事件循环并等待工作线程结束

    /**
     * @brief 唤醒阻塞中的调度器线程
     * @note 供其他线程在注入任务后打断 `kevent` 阻塞等待
     */
    void notify();

    int addAccept(IOController* controller) override;     ///< 注册 accept 等待；1=立即完成，0=已挂起，<0=错误
    int addConnect(IOController* controller) override;    ///< 注册 connect 等待；1=立即完成，0=已挂起，<0=错误
    int addRecv(IOController* controller) override;       ///< 注册 recv 等待；1=立即完成，0=已挂起，<0=错误
    int addSend(IOController* controller) override;       ///< 注册 send 等待；1=立即完成，0=已挂起，<0=错误
    int addReadv(IOController* controller) override;      ///< 注册 readv 等待；1=立即完成，0=已挂起，<0=错误
    int addWritev(IOController* controller) override;     ///< 注册 writev 等待；1=立即完成，0=已挂起，<0=错误
    int addClose(IOController* controller) override;      ///< 注册关闭句柄操作；0=成功提交，<0=失败
    int addFileRead(IOController* controller) override;   ///< 注册文件读取等待；1=立即完成，0=已挂起，<0=错误
    int addFileWrite(IOController* controller) override;  ///< 注册文件写入等待；1=立即完成，0=已挂起，<0=错误
    int addRecvFrom(IOController* controller) override;   ///< 注册 recvfrom 等待；1=立即完成，0=已挂起，<0=错误
    int addSendTo(IOController* controller) override;     ///< 注册 sendto 等待；1=立即完成，0=已挂起，<0=错误
    int addFileWatch(IOController* controller) override;  ///< 注册文件监控等待；1=立即完成，0=已挂起，<0=错误
    int addSendFile(IOController* controller) override;   ///< 注册 sendfile 等待；1=立即完成，0=已挂起，<0=错误
    int addSequence(IOController* controller) override;   ///< 注册组合式序列 IO；1=立即完成，0=已挂起，<0=错误

    int remove(IOController* controller) override;        ///< 删除控制器关联的所有已注册事件；0=成功，<0=失败
    std::optional<IOError> lastError() const override;    ///< 返回最近一次内部错误；无错误时返回 std::nullopt
    bool schedule(TaskRef task) override;                 ///< 从任意线程注入任务；必要时会唤醒事件循环
    bool scheduleDeferred(TaskRef task) override;         ///< 以延后语义注入任务；当前与 schedule() 共用注入通道
    bool scheduleImmediately(TaskRef task) override;      ///< 在调度器线程内立即恢复任务；跨线程调用会失败

    friend struct SchedulerTestAccess;
protected:
    std::atomic<bool> m_running;
    std::thread m_thread;

    // Configuration
    int m_max_events;
    int m_batch_size;
    int m_check_interval_ms;

    std::atomic<uint64_t> m_last_error_code{0};
    std::atomic<bool> m_sleeping{true};
    std::atomic<bool> m_wakeup_pending{false};
    IOSchedulerWorkerState m_worker;
    WakeCoordinator m_wake_coordinator;
    SchedulerCore m_core;
    KqueueReactor m_reactor;

private:
    /**
     * @brief kqueue 事件循环主函数
     * @note 仅在调度器工作线程中运行
     */
    void eventLoop();

    /**
     * @brief 处理跨线程注入和本地排队的待执行任务
     */
    void processPendingTasks();
};

}

#endif // USE_KQUEUE

#endif
