#include "FileWatcher.h"
#include "common/Defn.hpp"

#if defined(USE_IOURING) || defined(USE_EPOLL) || defined(USE_KQUEUE)

#include <unistd.h>
#include <fcntl.h>
#include <cerrno>

#if defined(USE_IOURING) || defined(USE_EPOLL)
#include <sys/inotify.h>
#endif

#ifdef USE_KQUEUE
// O_EVTONLY 是 macOS 专用标志，仅用于事件通知，不允许读写
#ifndef O_EVTONLY
#define O_EVTONLY 0x8000
#endif
#endif

namespace galay::async
{

#if defined(USE_IOURING) || defined(USE_EPOLL)
// Linux: 将 FileWatchEvent 转换为 inotify 掩码
static uint32_t toInotifyMask(FileWatchEvent events)
{
    uint32_t mask = 0;
    uint32_t e = static_cast<uint32_t>(events);
    if (e & static_cast<uint32_t>(FileWatchEvent::Access))       mask |= IN_ACCESS;
    if (e & static_cast<uint32_t>(FileWatchEvent::Modify))       mask |= IN_MODIFY;
    if (e & static_cast<uint32_t>(FileWatchEvent::Attrib))       mask |= IN_ATTRIB;
    if (e & static_cast<uint32_t>(FileWatchEvent::CloseWrite))   mask |= IN_CLOSE_WRITE;
    if (e & static_cast<uint32_t>(FileWatchEvent::CloseNoWrite)) mask |= IN_CLOSE_NOWRITE;
    if (e & static_cast<uint32_t>(FileWatchEvent::Open))         mask |= IN_OPEN;
    if (e & static_cast<uint32_t>(FileWatchEvent::MovedFrom))    mask |= IN_MOVED_FROM;
    if (e & static_cast<uint32_t>(FileWatchEvent::MovedTo))      mask |= IN_MOVED_TO;
    if (e & static_cast<uint32_t>(FileWatchEvent::Create))       mask |= IN_CREATE;
    if (e & static_cast<uint32_t>(FileWatchEvent::Delete))       mask |= IN_DELETE;
    if (e & static_cast<uint32_t>(FileWatchEvent::DeleteSelf))   mask |= IN_DELETE_SELF;
    if (e & static_cast<uint32_t>(FileWatchEvent::MoveSelf))     mask |= IN_MOVE_SELF;
    return mask;
}
#endif

FileWatcher::FileWatcher()
    : m_controller(GHandle::invalid())
#ifdef USE_KQUEUE
    , m_current_events(FileWatchEvent::None)
#endif
{
#if defined(USE_IOURING) || defined(USE_EPOLL)
    m_controller.m_handle.fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
#endif
    // macOS: m_controller 在 addWatch 时设置为第一个监控的文件 fd
}

FileWatcher::~FileWatcher()
{
#if defined(USE_IOURING) || defined(USE_EPOLL)
    if (m_controller.m_handle.fd >= 0) {
        for (const auto& [wd, path] : m_watches) {
            inotify_rm_watch(m_controller.m_handle.fd, wd);
        }
        close(m_controller.m_handle.fd);
    }
#else
    // macOS: 关闭所有打开的文件描述符
    for (const auto& [fd, path] : m_watches) {
        close(fd);
    }
#endif
}

FileWatcher::FileWatcher(FileWatcher&& other) noexcept
    : m_controller(std::move(other.m_controller))
    , m_watches(std::move(other.m_watches))
#ifdef USE_KQUEUE
    , m_current_events(other.m_current_events)
#endif
{
    other.m_controller.m_handle.fd = -1;
}

FileWatcher& FileWatcher::operator=(FileWatcher&& other) noexcept
{
    if (this != &other) {
        // 清理当前资源
#if defined(USE_IOURING) || defined(USE_EPOLL)
        if (m_controller.m_handle.fd >= 0) {
            for (const auto& [wd, path] : m_watches) {
                inotify_rm_watch(m_controller.m_handle.fd, wd);
            }
            close(m_controller.m_handle.fd);
        }
#else
        for (const auto& [fd, path] : m_watches) {
            close(fd);
        }
#endif

        // 移动资源
        m_controller.m_handle.fd = other.m_controller.m_handle.fd;
        m_controller = std::move(other.m_controller);
        m_watches = std::move(other.m_watches);
#ifdef USE_KQUEUE
        m_current_events = other.m_current_events;
#endif

        other.m_controller.m_handle.fd = -1;
    }
    return *this;
}

std::expected<int, IOError> FileWatcher::addWatch(const std::string& path, FileWatchEvent events)
{
#if defined(USE_IOURING) || defined(USE_EPOLL)
    if (m_controller.m_handle.fd < 0) {
        return std::unexpected(IOError(kOpenFailed, EBADF));
    }

    uint32_t mask = toInotifyMask(events);
    int wd = inotify_add_watch(m_controller.m_handle.fd, path.c_str(), mask);
    if (wd < 0) {
        return std::unexpected(IOError(kOpenFailed, errno));
    }

    m_watches[wd] = path;
    return wd;
#else
    // macOS: 打开文件获取 fd，用于 kqueue EVFILT_VNODE
    int fd = open(path.c_str(), O_RDONLY | O_EVTONLY);
    if (fd < 0) {
        return std::unexpected(IOError(kOpenFailed, errno));
    }

    m_watches[fd] = path;
    m_current_events = events;

    // 设置第一个监控的 fd 作为 watch_fd
    if (m_controller.m_handle.fd < 0) {
        m_controller.m_handle.fd = fd;
    }

    return fd;
#endif
}

std::expected<void, IOError> FileWatcher::removeWatch(int wd)
{
#if defined(USE_IOURING) || defined(USE_EPOLL)
    if (m_controller.m_handle.fd < 0) {
        return std::unexpected(IOError(kOpenFailed, EBADF));
    }

    auto it = m_watches.find(wd);
    if (it == m_watches.end()) {
        return std::unexpected(IOError(kOpenFailed, EINVAL));
    }

    if (inotify_rm_watch(m_controller.m_handle.fd, wd) < 0) {
        return std::unexpected(IOError(kOpenFailed, errno));
    }

    m_watches.erase(it);
    return {};
#else
    // macOS: 关闭文件描述符
    auto it = m_watches.find(wd);
    if (it == m_watches.end()) {
        return std::unexpected(IOError(kOpenFailed, EINVAL));
    }

    close(wd);
    m_watches.erase(it);

    // 如果删除的是当前 watch_fd，更新为下一个
    if (wd == m_controller.m_handle.fd) {
        if (!m_watches.empty()) {
            m_controller.m_handle.fd = m_watches.begin()->first;
        } else {
            m_controller.m_handle.fd = -1;
        }
    }

    return {};
#endif
}

FileWatchAwaitable FileWatcher::watch()
{
    return FileWatchAwaitable(&m_controller,
                              m_buffer, BUFFER_SIZE);
}

std::string FileWatcher::getPath(int wd) const
{
    auto it = m_watches.find(wd);
    if (it != m_watches.end()) {
        return it->second;
    }
    return {};
}

} // namespace galay::async

#endif // USE_IOURING || USE_EPOLL || USE_KQUEUE
