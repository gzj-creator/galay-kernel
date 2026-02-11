#ifndef GALAY_KERNEL_IOCONTROLLER_HPP
#define GALAY_KERNEL_IOCONTROLLER_HPP

#include "galay-kernel/common/Defn.hpp"

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

    GHandle m_handle = GHandle::invalid();
    IOEventType m_type = IOEventType::INVALID;  ///< 当前IO事件类型
    void* m_awaitable[IOController::SIZE] = {nullptr, nullptr};

    template<typename T>
    T* getAwaitable() { return nullptr; }
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_IOCONTROLLER_HPP
