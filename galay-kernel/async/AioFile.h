#ifndef GALAY_AIO_FILE_H
#define GALAY_AIO_FILE_H

#ifdef USE_EPOLL

#include "galay-kernel/kernel/IOScheduler.hpp"
#include "galay-kernel/kernel/Waker.h"
#include "galay-kernel/common/Error.h"
#include <expected>
#include <string>
#include <vector>
#include <fcntl.h>
#include <libaio.h>

namespace galay::kernel {
    class EpollScheduler;
}

namespace galay::async
{
/**
 * @brief AIO 文件打开模式
 * @details 基于 `O_DIRECT` 打开文件，要求调用方使用对齐缓冲区。
 */
enum class AioOpenMode : int {
    Read      = O_RDONLY | O_DIRECT,  ///< 只读直通模式
    Write     = O_WRONLY | O_CREAT | O_DIRECT,  ///< 只写直通模式，必要时创建文件
    ReadWrite = O_RDWR | O_CREAT | O_DIRECT,  ///< 读写直通模式，必要时创建文件
};

class AioFile;

/**
 * @brief AIO 提交结果的可等待对象
 */
struct AioCommitAwaitable {
    /**
     * @brief 构造批量 AIO 提交等待体
     * @param controller 关联的 IO 控制器
     * @param aio_ctx libaio 上下文
     * @param event_fd 用于完成通知的 eventfd
     * @param pending_ptrs 本次待提交的 iocb 指针集合，所有权转移到等待体
     * @param pending_count 本次实际需要提交的操作数
     */
    AioCommitAwaitable(galay::kernel::IOController* controller,
                       io_context_t aio_ctx, int event_fd,
                       std::vector<struct iocb*>&& pending_ptrs, size_t pending_count);

    bool await_ready() { return m_pending_count == 0; }  ///< 没有待提交操作时无需挂起
    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle);  ///< 提交所有挂起 AIO 操作，并在需要时挂起当前协程
    std::expected<std::vector<ssize_t>, galay::kernel::IOError> await_resume();  ///< 返回各提交操作的结果；失败时返回 IOError

    galay::kernel::IOController* m_controller;  ///< 关联的 IO 控制器
    io_context_t m_aio_ctx;  ///< libaio 上下文
    int m_event_fd;  ///< 完成通知用 eventfd
    std::vector<struct iocb*> m_pending_ptrs;  ///< 待提交 iocb 指针集合，等待体拥有其生命周期
    size_t m_pending_count;  ///< 本次需要提交的操作数
    galay::kernel::Waker m_waker;  ///< 完成后恢复提交协程的唤醒器
    std::vector<ssize_t> m_results;  ///< 每个 iocb 对应的原始返回值
    std::expected<std::vector<ssize_t>, galay::kernel::IOError> m_result;  ///< 聚合后的提交结果
};

/**
 * @brief Linux AIO 风格的异步文件操作
 *
 * 使用方式:
 *   AioFile file(scheduler);
 *   file.open("test.txt", AioOpenMode::Read);
 *
 *   // 准备多个操作
 *   file.preRead(buf1, len1, offset1);
 *   file.preRead(buf2, len2, offset2);
 *
 *   // 批量提交并等待完成
 *   auto results = co_await file.commit();
 */
class AioFile
{
public:
    AioFile(int max_events = 64);
    ~AioFile();

    // 禁止拷贝
    AioFile(const AioFile&) = delete;
    AioFile& operator=(const AioFile&) = delete;

    // 允许移动
    AioFile(AioFile&& other) noexcept;
    AioFile& operator=(AioFile&& other) noexcept;

    // 打开文件 (必须使用 O_DIRECT)
    std::expected<void, galay::kernel::IOError> open(
        const std::string& path,
        AioOpenMode mode,
        int permissions = 0644);

    void preRead(char* buffer, size_t length, off_t offset);  ///< 预登记一次异步读；buffer 必须满足 O_DIRECT 对齐要求
    void preWrite(const char* buffer, size_t length, off_t offset);  ///< 预登记一次异步写；buffer 必须满足 O_DIRECT 对齐要求
    void preReadBatch(const std::vector<std::tuple<char*, size_t, off_t>>& reads);  ///< 批量预登记读操作
    void preWriteBatch(const std::vector<std::tuple<const char*, size_t, off_t>>& writes);  ///< 批量预登记写操作
    AioCommitAwaitable commit();  ///< 提交当前累计的所有操作并返回等待体
    void clear();  ///< 清空已预登记但尚未提交的操作
    void close();  ///< 关闭文件句柄并释放相关 AIO 资源

    // 获取文件句柄
    GHandle handle() const { return m_handle; }

    // 检查是否有效
    bool isValid() const { return m_handle.fd >= 0; }

    std::expected<size_t, galay::kernel::IOError> size() const;  ///< 返回文件当前大小
    std::expected<void, galay::kernel::IOError> sync();  ///< 把文件内容同步到磁盘
    static char* allocAlignedBuffer(size_t size, size_t alignment = 512);  ///< 分配满足 O_DIRECT 要求的对齐缓冲区
    static void freeAlignedBuffer(char* buffer);  ///< 释放由 allocAlignedBuffer() 返回的缓冲区

    /*
     * @brief 获取IO控制器
     * @return IOController* IO控制器
     */
    galay::kernel::IOController* getController() { return &m_controller; }  ///< 返回内部 IO 控制器指针供高级用法访问

private:
    GHandle m_handle;  ///< 当前文件句柄
    galay::kernel::IOController m_controller;  ///< 批量提交完成通知使用的 IO 控制器

    io_context_t m_aio_ctx;  ///< libaio 上下文
    int m_event_fd;  ///< 完成通知 eventfd
    int m_max_events;  ///< libaio 队列深度上限

    std::vector<struct iocb> m_pending_cbs;  ///< 待提交操作对象集合
    std::vector<struct iocb*> m_pending_ptrs;  ///< 指向 m_pending_cbs 的提交指针数组
};

} // namespace galay::async

template <typename Promise>
inline bool galay::async::AioCommitAwaitable::await_suspend(std::coroutine_handle<Promise> handle)
{
    m_waker = galay::kernel::Waker(handle);

    if (m_pending_count == 0) {
        m_result = std::vector<ssize_t>{};
        return false;
    }

    int ret = io_submit(m_aio_ctx, m_pending_count, m_pending_ptrs.data());
    if (ret < 0) {
        m_result = std::unexpected(
            galay::kernel::IOError(galay::kernel::kWriteFailed, static_cast<uint32_t>(-ret)));
        return false;
    }

    auto scheduler = m_waker.getScheduler();
    if (scheduler == nullptr || scheduler->type() != galay::kernel::kIOScheduler) {
        m_result = std::unexpected(
            galay::kernel::IOError(galay::kernel::kNotRunningOnIOScheduler, errno));
        return false;
    }
    auto io_scheduler = static_cast<galay::kernel::IOScheduler*>(scheduler);

    m_controller->m_handle.fd = m_event_fd;
    m_controller->fillAwaitable(FILEREAD, this);
    if (io_scheduler->addFileRead(m_controller) < 0) {
        m_result = std::unexpected(
            galay::kernel::IOError(galay::kernel::kReadFailed, errno));
        return false;
    }
    return true;
}

#endif // USE_EPOLL

#endif // GALAY_AIO_FILE_H
