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
struct IOController;

#ifdef USE_IOURING
/**
 * @brief SQE 标签，生命周期与 IOController 绑定
 * @details 用作 io_uring sqe 的 user_data，避免超时销毁 awaitable 后 CQE 解引用野指针
 */
struct SqeTag {
    IOController* owner;
    uint8_t slot;  // IOController::READ=0, IOController::WRITE=1
};
#endif

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
        : m_handle(handle)
#ifdef USE_IOURING
        , m_sqe_tag{{this, READ}, {this, WRITE}}
#endif
    {}

    IOController(const IOController& other) noexcept
        : m_handle(other.m_handle)
        , m_type(other.m_type)
        , m_awaitable{other.m_awaitable[READ], other.m_awaitable[WRITE]}
#ifdef USE_IOURING
        , m_sqe_tag{{this, READ}, {this, WRITE}}
#endif
    {}

    IOController& operator=(const IOController& other) noexcept {
        if (this != &other) {
            m_handle = other.m_handle;
            m_type = other.m_type;
            m_awaitable[READ] = other.m_awaitable[READ];
            m_awaitable[WRITE] = other.m_awaitable[WRITE];
#ifdef USE_IOURING
            m_sqe_tag[READ] = {this, READ};
            m_sqe_tag[WRITE] = {this, WRITE};
#endif
        }
        return *this;
    }

    IOController(IOController&& other) noexcept
        : m_handle(other.m_handle)
        , m_type(other.m_type)
        , m_awaitable{other.m_awaitable[READ], other.m_awaitable[WRITE]}
#ifdef USE_IOURING
        , m_sqe_tag{{this, READ}, {this, WRITE}}
#endif
    {
        other.resetMovedFrom();
    }

    IOController& operator=(IOController&& other) noexcept {
        if (this != &other) {
            m_handle = other.m_handle;
            m_type = other.m_type;
            m_awaitable[READ] = other.m_awaitable[READ];
            m_awaitable[WRITE] = other.m_awaitable[WRITE];
#ifdef USE_IOURING
            m_sqe_tag[READ] = {this, READ};
            m_sqe_tag[WRITE] = {this, WRITE};
#endif
            other.resetMovedFrom();
        }
        return *this;
    }

    void resetMovedFrom() noexcept {
        m_handle = GHandle::invalid();
        m_type = IOEventType::INVALID;
        m_awaitable[READ] = nullptr;
        m_awaitable[WRITE] = nullptr;
#ifdef USE_IOURING
        m_sqe_tag[READ] = {this, READ};
        m_sqe_tag[WRITE] = {this, WRITE};
#endif
    }

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
#ifdef USE_IOURING
    SqeTag m_sqe_tag[SIZE];
#endif

    template<typename T>
    T* getAwaitable() { return nullptr; }
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_IOCONTROLLER_HPP
