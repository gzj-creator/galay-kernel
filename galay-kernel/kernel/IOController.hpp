#ifndef GALAY_KERNEL_IOCONTROLLER_HPP
#define GALAY_KERNEL_IOCONTROLLER_HPP

#include "galay-kernel/common/Defn.hpp"

#ifdef USE_IOURING
#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#endif

namespace galay::kernel
{

struct SequenceAwaitableBase;

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
struct SqeState {
    explicit SqeState(IOController* controller, uint8_t index)
        : owner(controller)
        , slot(index) {}

    std::atomic<IOController*> owner;
    const uint8_t slot;
    std::atomic<uint64_t> generation{1};
};

struct SqeRequestToken {
    std::shared_ptr<SqeState> state;
    uint64_t generation;
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
        , m_sqe_state{
            std::make_shared<SqeState>(this, READ),
            std::make_shared<SqeState>(this, WRITE)}
#endif
    {}

    ~IOController() {
#ifdef USE_IOURING
        clearSqeState();
#endif
    }

    IOController(const IOController& other) noexcept
        : m_handle(other.m_handle)
        , m_type(other.m_type)
        , m_awaitable{other.m_awaitable[READ], other.m_awaitable[WRITE]}
        , m_sequence_owner{other.m_sequence_owner[READ], other.m_sequence_owner[WRITE]}
        , m_sequence_interest_mask(other.m_sequence_interest_mask)
        , m_sequence_armed_mask(other.m_sequence_armed_mask)
#ifdef USE_EPOLL
        , m_registered_events(other.m_registered_events)
#endif
#ifdef USE_IOURING
        , m_sqe_state{
            std::make_shared<SqeState>(this, READ),
            std::make_shared<SqeState>(this, WRITE)}
#endif
    {}

    IOController& operator=(const IOController& other) noexcept {
        if (this != &other) {
            m_handle = other.m_handle;
            m_type = other.m_type;
            m_awaitable[READ] = other.m_awaitable[READ];
            m_awaitable[WRITE] = other.m_awaitable[WRITE];
            m_sequence_owner[READ] = other.m_sequence_owner[READ];
            m_sequence_owner[WRITE] = other.m_sequence_owner[WRITE];
            m_sequence_interest_mask = other.m_sequence_interest_mask;
            m_sequence_armed_mask = other.m_sequence_armed_mask;
#ifdef USE_EPOLL
            m_registered_events = other.m_registered_events;
#endif
#ifdef USE_IOURING
            clearSqeState();
            m_sqe_state[READ] = std::make_shared<SqeState>(this, READ);
            m_sqe_state[WRITE] = std::make_shared<SqeState>(this, WRITE);
#endif
        }
        return *this;
    }

    IOController(IOController&& other) noexcept
        : m_handle(other.m_handle)
        , m_type(other.m_type)
        , m_awaitable{other.m_awaitable[READ], other.m_awaitable[WRITE]}
        , m_sequence_owner{other.m_sequence_owner[READ], other.m_sequence_owner[WRITE]}
        , m_sequence_interest_mask(other.m_sequence_interest_mask)
        , m_sequence_armed_mask(other.m_sequence_armed_mask)
#ifdef USE_EPOLL
        , m_registered_events(other.m_registered_events)
#endif
#ifdef USE_IOURING
        , m_sqe_state{std::move(other.m_sqe_state[READ]), std::move(other.m_sqe_state[WRITE])}
#endif
    {
#ifdef USE_IOURING
        rebindSqeState();
#endif
        other.resetMovedFrom();
    }

    IOController& operator=(IOController&& other) noexcept {
        if (this != &other) {
            m_handle = other.m_handle;
            m_type = other.m_type;
            m_awaitable[READ] = other.m_awaitable[READ];
            m_awaitable[WRITE] = other.m_awaitable[WRITE];
            m_sequence_owner[READ] = other.m_sequence_owner[READ];
            m_sequence_owner[WRITE] = other.m_sequence_owner[WRITE];
            m_sequence_interest_mask = other.m_sequence_interest_mask;
            m_sequence_armed_mask = other.m_sequence_armed_mask;
#ifdef USE_EPOLL
            m_registered_events = other.m_registered_events;
#endif
#ifdef USE_IOURING
            clearSqeState();
            m_sqe_state[READ] = std::move(other.m_sqe_state[READ]);
            m_sqe_state[WRITE] = std::move(other.m_sqe_state[WRITE]);
            rebindSqeState();
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
        m_sequence_owner[READ] = nullptr;
        m_sequence_owner[WRITE] = nullptr;
        m_sequence_interest_mask = 0;
        m_sequence_armed_mask = 0;
#ifdef USE_EPOLL
        m_registered_events = 0;
#endif
#ifdef USE_IOURING
        clearSqeState();
        m_sqe_state[READ] = std::make_shared<SqeState>(this, READ);
        m_sqe_state[WRITE] = std::make_shared<SqeState>(this, WRITE);
#endif
    }

#ifdef USE_IOURING
    SqeRequestToken* makeSqeRequest(Index slot) const {
        const auto& state = m_sqe_state[slot];
        if (!state) {
            return nullptr;
        }
        return new (std::nothrow) SqeRequestToken{
            .state = state,
            .generation = state->generation.load(std::memory_order_acquire)};
    }

    void advanceSqeGeneration(Index slot) noexcept {
        if (auto& state = m_sqe_state[slot]; state) {
            state->generation.fetch_add(1, std::memory_order_acq_rel);
            state->owner.store(this, std::memory_order_release);
        }
    }

    void invalidateSqeRequests() noexcept {
        clearSqeState();
    }
#endif

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
    SequenceAwaitableBase* m_sequence_owner[IOController::SIZE] = {nullptr, nullptr};
    uint8_t m_sequence_interest_mask = 0;
    uint8_t m_sequence_armed_mask = 0;
#ifdef USE_EPOLL
    uint32_t m_registered_events = 0;          ///< epoll 已注册的事件掩码缓存
#endif
#ifdef USE_IOURING
    std::shared_ptr<SqeState> m_sqe_state[SIZE];
#endif

    template<typename T>
    T* getAwaitable() { return nullptr; }

#ifdef USE_IOURING
private:
    void clearSqeState() noexcept {
        for (auto& state : m_sqe_state) {
            if (!state) {
                continue;
            }
            state->owner.store(nullptr, std::memory_order_release);
            state->generation.fetch_add(1, std::memory_order_acq_rel);
        }
    }

    void rebindSqeState() noexcept {
        for (auto& state : m_sqe_state) {
            if (!state) {
                continue;
            }
            state->owner.store(this, std::memory_order_release);
        }
    }
#endif
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_IOCONTROLLER_HPP
