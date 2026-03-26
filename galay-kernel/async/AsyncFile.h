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
/**
 * @brief 普通异步文件打开模式
 */
enum class FileOpenMode : int {
    Read      = O_RDONLY,  ///< 只读
    Write     = O_WRONLY | O_CREAT,  ///< 只写，必要时创建文件
    ReadWrite = O_RDWR | O_CREAT,  ///< 读写，必要时创建文件
    Append    = O_WRONLY | O_CREAT | O_APPEND,  ///< 追加写
    Truncate  = O_WRONLY | O_CREAT | O_TRUNC,  ///< 打开时截断文件
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
    std::expected<void, galay::kernel::IOError> open(
        const std::string& path,
        FileOpenMode mode,
        int permissions = 0644);

    /**
     * @brief 异步读取文件
     *
     * @param buffer 读取缓冲区
     * @param length 缓冲区大小
     * @param offset 文件偏移
     * @return FileReadAwaitable 可等待对象，co_await 后返回读取到的字节数
     *
     * @note
     * - 返回值为0表示 EOF
     * - 缓冲区生命周期必须持续到 co_await 完成
     */
    galay::kernel::FileReadAwaitable read(char* buffer, size_t length, off_t offset = 0);  ///< 异步读取文件，恢复后返回实际读取字节数

    galay::kernel::FileWriteAwaitable write(const char* buffer, size_t length, off_t offset = 0);  ///< 异步写入文件，恢复后返回实际写入字节数
    galay::kernel::CloseAwaitable close();  ///< 异步关闭文件句柄

    // 获取文件句柄
    GHandle handle() const { return m_controller.m_handle; }


    std::expected<size_t, galay::kernel::IOError> size() const;  ///< 返回文件当前大小
    std::expected<void, galay::kernel::IOError> sync();  ///< 把文件内容同步到磁盘

    /*
     * @brief 获取IO控制器
     * @return IOController* IO控制器
     */
    galay::kernel::IOController* getController() { return &m_controller; }  ///< 返回内部 IO 控制器指针供高级用法访问

private:
    galay::kernel::IOController m_controller;
};

} // namespace galay::async

#endif // USE_KQUEUE || USE_IOURING

#endif // GALAY_ASYNC_FILE_H
