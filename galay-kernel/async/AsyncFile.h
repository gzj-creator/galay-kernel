#ifndef GALAY_ASYNC_FILE_H
#define GALAY_ASYNC_FILE_H

// AsyncFile 仅用于 kqueue (macOS) 和 io_uring (Linux)
// epoll 平台请使用 AioFile

#if defined(USE_KQUEUE) || defined(USE_IOURING)

#include "galay-kernel/kernel/IOScheduler.hpp"
#include "galay-kernel/kernel/Awaitable.h"
#include "galay-kernel/common/Error.h"
#include <expected>
#include <string>
#include <fcntl.h>

namespace galay::async
{

using namespace galay::kernel;

enum class FileOpenMode : int {
    Read      = O_RDONLY,
    Write     = O_WRONLY | O_CREAT,
    ReadWrite = O_RDWR | O_CREAT,
    Append    = O_WRONLY | O_CREAT | O_APPEND,
    Truncate  = O_WRONLY | O_CREAT | O_TRUNC,
};

/**
 * @brief 异步文件操作类 (kqueue/io_uring 专用)
 *
 * @note epoll 平台请使用 AioFile，它提供更符合 libaio 风格的批量操作 API
 */
class AsyncFile
{
public:
    AsyncFile();
    ~AsyncFile();

    // 禁止拷贝
    AsyncFile(const AsyncFile&) = delete;
    AsyncFile& operator=(const AsyncFile&) = delete;

    // 允许移动
    AsyncFile(AsyncFile&& other) noexcept;
    AsyncFile& operator=(AsyncFile&& other) noexcept;

    // 打开文件
    std::expected<void, IOError> open(const std::string& path, FileOpenMode mode, int permissions = 0644);

    // 异步读取
    FileReadAwaitable read(char* buffer, size_t length, off_t offset = 0);

    // 异步写入
    FileWriteAwaitable write(const char* buffer, size_t length, off_t offset = 0);

    // 异步关闭
    CloseAwaitable close();

    // 获取文件句柄
    GHandle handle() const { return m_controller.m_handle; }


    // 获取文件大小
    std::expected<size_t, IOError> size() const;

    // 同步操作（用于简单场景）
    std::expected<void, IOError> sync();

private:
    IOController m_controller;
};

} // namespace galay::async

#endif // USE_KQUEUE || USE_IOURING

#endif // GALAY_ASYNC_FILE_H
