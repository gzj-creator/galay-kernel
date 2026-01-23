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

using namespace galay::kernel;

enum class AioOpenMode : int {
    Read      = O_RDONLY | O_DIRECT,
    Write     = O_WRONLY | O_CREAT | O_DIRECT,
    ReadWrite = O_RDWR | O_CREAT | O_DIRECT,
};

class AioFile;

/**
 * @brief AIO 提交结果的可等待对象
 */
struct AioCommitAwaitable {
    AioCommitAwaitable(IOController* controller,
                       io_context_t aio_ctx, int event_fd,
                       std::vector<struct iocb*>&& pending_ptrs, size_t pending_count);

    bool await_ready() { return m_pending_count == 0; }
    bool await_suspend(std::coroutine_handle<> handle);
    std::expected<std::vector<ssize_t>, IOError> await_resume();

    IOController* m_controller;
    io_context_t m_aio_ctx;
    int m_event_fd;
    std::vector<struct iocb*> m_pending_ptrs;  // 拥有所有权，不再是指针
    size_t m_pending_count;
    Waker m_waker;
    std::vector<ssize_t> m_results;
    std::expected<std::vector<ssize_t>, IOError> m_result;
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
    std::expected<void, IOError> open(const std::string& path, AioOpenMode mode, int permissions = 0644);

    // 准备读操作 (buffer 必须对齐到 512 字节)
    void preRead(char* buffer, size_t length, off_t offset);

    // 准备写操作 (buffer 必须对齐到 512 字节)
    void preWrite(const char* buffer, size_t length, off_t offset);

    // 批量准备读操作
    void preReadBatch(const std::vector<std::tuple<char*, size_t, off_t>>& reads);

    // 批量准备写操作
    void preWriteBatch(const std::vector<std::tuple<const char*, size_t, off_t>>& writes);

    // 提交所有准备的操作并返回等待体
    AioCommitAwaitable commit();

    // 清除已准备但未提交的操作
    void clear();

    // 关闭文件
    void close();

    // 获取文件句柄
    GHandle handle() const { return m_handle; }

    // 检查是否有效
    bool isValid() const { return m_handle.fd >= 0; }

    // 获取文件大小
    std::expected<size_t, IOError> size() const;

    // 同步到磁盘
    std::expected<void, IOError> sync();

    // 分配对齐的缓冲区 (用于 O_DIRECT)
    static char* allocAlignedBuffer(size_t size, size_t alignment = 512);
    static void freeAlignedBuffer(char* buffer);

    /*
     * @brief 获取IO控制器
     * @return IOController* IO控制器
     */
    IOController* getController() { return &m_controller; }

private:
    GHandle m_handle;
    IOController m_controller;

    // libaio 相关
    io_context_t m_aio_ctx;
    int m_event_fd;
    int m_max_events;

    // 待提交的操作
    std::vector<struct iocb> m_pending_cbs;
    std::vector<struct iocb*> m_pending_ptrs;
};

} // namespace galay::async

#endif // USE_EPOLL

#endif // GALAY_AIO_FILE_H
