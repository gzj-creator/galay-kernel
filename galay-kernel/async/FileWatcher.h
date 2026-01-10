#ifndef GALAY_ASYNC_FILE_WATCHER_H
#define GALAY_ASYNC_FILE_WATCHER_H


// FileWatcher 支持:
// - Linux: inotify (io_uring/epoll)
// - macOS: kqueue EVFILT_VNODE

#if defined(USE_IOURING) || defined(USE_EPOLL) || defined(USE_KQUEUE)

#include "galay-kernel/kernel/IOScheduler.hpp"
#include "galay-kernel/kernel/Awaitable.h"
#include "galay-kernel/common/Error.h"
#include <expected>
#include <string>
#include <unordered_map>

namespace galay::async
{

using namespace galay::kernel;

/**
 * @brief 异步文件监控类
 *
 * @details 监控文件或目录的变化。
 * - Linux: 使用 inotify
 * - macOS: 使用 kqueue EVFILT_VNODE
 *
 * @code
 * Coroutine watchFile(IOScheduler* scheduler) {
 *     FileWatcher watcher(scheduler);
 *     auto result = watcher.addWatch("/path/to/file", FileWatchEvent::Modify);
 *     if (!result) {
 *         // 处理错误
 *         co_return;
 *     }
 *
 *     while (true) {
 *         auto event = co_await watcher.watch();
 *         if (event) {
 *             // 处理事件
 *             if (event->has(FileWatchEvent::Modify)) {
 *                 // 文件被修改
 *             }
 *         }
 *     }
 * }
 * @endcode
 */
class FileWatcher
{
public:
    /**
     * @brief 构造函数
     */
    FileWatcher();

    /**
     * @brief 析构函数
     */
    ~FileWatcher();

    // 禁止拷贝
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    // 允许移动
    FileWatcher(FileWatcher&& other) noexcept;
    FileWatcher& operator=(FileWatcher&& other) noexcept;

    /**
     * @brief 添加监控路径
     * @param path 要监控的文件或目录路径
     * @param events 要监控的事件类型（可用 | 组合多个事件）
     * @return 成功返回监控描述符，失败返回IOError
     *
     * @note Linux: 监控目录时，FileWatchResult::name 包含变化的文件名
     * @note macOS: kqueue 需要为每个文件单独添加监控
     */
    std::expected<int, IOError> addWatch(const std::string& path, FileWatchEvent events = FileWatchEvent::All);

    /**
     * @brief 移除监控
     * @param wd 监控描述符（addWatch返回的值）
     * @return 成功返回void，失败返回IOError
     */
    std::expected<void, IOError> removeWatch(int wd);

    /**
     * @brief 异步等待文件事件
     * @return FileWatchAwaitable 可等待对象
     *
     * @note co_await 后返回 FileWatchResult 或 IOError
     */
    FileWatchAwaitable watch();

    /**
     * @brief 检查是否有效
     * @return 监控是否初始化成功
     */
    bool isValid() const { return m_watch_fd >= 0; }

    /**
     * @brief 获取监控文件描述符
     * @return Linux: inotify fd, macOS: 第一个监控的文件 fd
     */
    int fd() const { return m_watch_fd; }

    /**
     * @brief 获取监控路径
     * @param wd 监控描述符
     * @return 对应的路径，如果不存在返回空字符串
     */
    std::string getPath(int wd) const;

private:
    int m_watch_fd;                                ///< Linux: inotify fd, macOS: 当前监控的 fd
    IOController m_controller;                     ///< IO控制器
    std::unordered_map<int, std::string> m_watches; ///< wd/fd -> path 映射

    static constexpr size_t BUFFER_SIZE = 4096;    ///< 事件缓冲区大小
    char m_buffer[BUFFER_SIZE];                    ///< 事件缓冲区

#ifdef USE_KQUEUE
    FileWatchEvent m_current_events;               ///< macOS: 当前监控的事件类型
#endif
};

} // namespace galay::async

#endif // USE_IOURING || USE_EPOLL || USE_KQUEUE

#endif // GALAY_ASYNC_FILE_WATCHER_H
