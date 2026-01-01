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

#ifndef GALAY_KERNEL_SCHEDULER_H
#define GALAY_KERNEL_SCHEDULER_H

#include "galay-kernel/common/Defn.hpp"

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

protected:
    /**
     * @brief 恢复协程执行
     * @param co 要恢复的协程
     * @note 仅供调度器内部使用
     */
    void resume(Coroutine& co);
};

/**
 * @brief IO事件控制器
 *
 * @details 管理单个IO操作的状态和回调。
 * 每个异步IO操作都关联一个IOController。
 *
 * @note
 * - 由TcpSocket内部管理
 * - 存储当前IO操作类型和对应的Awaitable对象
 */
struct IOController {
    /**
     * @brief 默认构造函数
     */
    IOController() {}

    /**
     * @brief 填充Awaitable信息
     * @param type IO事件类型
     * @param awaitable 对应的Awaitable对象指针
     */
    void fillAwaitable(IOEventType type, void* awaitable) {
        m_type = type;
        m_awaitable = awaitable;
    }

    /**
     * @brief 清除Awaitable信息
     * @note IO操作完成后调用
     */
    void removeAwaitable() {
        m_type = IOEventType::INVALID;
        m_awaitable = nullptr;
    }

    IOEventType m_type = IOEventType::INVALID;  ///< 当前IO事件类型
    void* m_awaitable = nullptr;                 ///< 关联的Awaitable对象
};

/**
 * @brief IO调度器接口
 *
 * @details 扩展Scheduler，添加IO事件注册接口。
 * 具体实现需要处理平台相关的IO多路复用机制。
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
     * @brief 关闭文件描述符
     * @param fd 要关闭的文件描述符
     * @return 0表示成功，<0表示错误
     */
    virtual int addClose(int fd) = 0;

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
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_SCHEDULER_H
