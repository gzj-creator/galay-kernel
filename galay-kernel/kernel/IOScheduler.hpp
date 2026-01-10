#ifndef GALAY_KERNEL_IOSCHEDULER_HPP
#define GALAY_KERNEL_IOSCHEDULER_HPP

#include "galay-kernel/common/Defn.hpp"
#include "Scheduler.hpp"
#include "Awaitable.h"

namespace galay::kernel 
{

/**
 * @brief IO事件控制器
 *
 * @details 管理单个IO操作的状态和回调。
 * 每个异步IO操作都关联一个IOController。
 *
 * @note
 * - 存储当前IO操作类型和对应的Awaitable对象
 * - 支持超时机制，通过 generation 和 state 防止重复唤醒
 * - 支持同时 RECV 和 SEND
 * - !!! 不允许跨调度器执行(非线程安全)
 */
 struct IOController {
    /**
     * @brief IO操作索引（用于数组访问）
     */
    enum Index : uint8_t {
        READ = 0,             ///< Read 操作
        WRITE = 1,            ///< Write 操作
        SIZE
    };

    /**
     * @brief 构造函数
     */
    IOController(GHandle handle) 
        : m_handle(handle) {}

    /**
     * @brief 填充Awaitable信息（支持 RECVWITHSEND 状态机）
     * @param type IO事件类型
     * @param awaitable 对应的Awaitable对象指针
     */
    bool fillAwaitable(IOEventType type, void* awaitable);

    /**
     * @brief 清除Awaitable信息（支持 RECVWITHSEND 状态机）
     * @param type IO事件类型
     */
    void removeAwaitable(IOEventType type);

    template<typename T>
    T* getAwaitable() { return nullptr; }

    GHandle m_handle = GHandle::invalid();
    IOEventType m_type = IOEventType::INVALID;  ///< 当前IO事件类型
    void* m_awaitable[IOController::SIZE] = {nullptr, nullptr};
    uint64_t m_generation[IOController::SIZE] = {0, 0};  ///< 操作代数，每次注册递增
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
inline auto IOController::getAwaitable() -> RecvNotifyAwaitable* {
    return static_cast<RecvNotifyAwaitable*>(m_awaitable[READ]);
}

template<>
inline auto IOController::getAwaitable() -> SendNotifyAwaitable* {
    return static_cast<SendNotifyAwaitable*>(m_awaitable[WRITE]);
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
     * @brief 注册Recv通知事件（仅通知可读，不执行IO操作）
     * @param controller IO控制器
     * @return 0表示已注册等待，<0表示错误
     * @note 用于SSL等需要自定义IO处理的场景，事件就绪时只唤醒协程
     */
    virtual int addRecvNotify(IOController* controller) = 0;

    /**
     * @brief 注册Send通知事件（仅通知可写，不执行IO操作）
     * @param controller IO控制器
     * @return 0表示已注册等待，<0表示错误
     * @note 用于SSL等需要自定义IO处理的场景，事件就绪时只唤醒协程
     */
    virtual int addSendNotify(IOController* controller) = 0;

     /**
     * @brief 删除fd的所有事件
     * @param controller IO控制器
     */
    virtual int remove(IOController* controller) = 0;
};


inline bool IOController::fillAwaitable(IOEventType type, void* awaitable) {
    m_type = type;
    switch (m_type) {
    case IOEventType::RECV:
    case IOEventType::FILEREAD:
    case IOEventType::RECVFROM:
    case IOEventType::ACCEPT:
    case IOEventType::FILEWATCH:
    case IOEventType::RECV_NOTIFY:
        m_awaitable[READ] = awaitable;
        ++m_generation[READ];
        break;
    case IOEventType::SEND:
    case IOEventType::FILEWRITE:
    case IOEventType::SENDTO:
    case IOEventType::CONNECT:
    case IOEventType::SEND_NOTIFY:
        m_awaitable[WRITE] = awaitable;
        ++m_generation[WRITE];
        break;
    default:
        return false;
    }
    return true;
}

inline void IOController::removeAwaitable(IOEventType type) {
    switch (m_type) {
    case IOEventType::RECV:
    case IOEventType::FILEREAD:
    case IOEventType::RECVFROM:
    case IOEventType::ACCEPT:
    case IOEventType::FILEWATCH:
    case IOEventType::RECV_NOTIFY:
        m_awaitable[READ] = nullptr;
        break;
    case IOEventType::SEND:
    case IOEventType::FILEWRITE:
    case IOEventType::SENDTO:
    case IOEventType::CONNECT:
    case IOEventType::SEND_NOTIFY:
        m_awaitable[WRITE] = nullptr;
        break;
    default:
        break;
    }
}   



}

#endif