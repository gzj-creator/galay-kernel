#include "galay-kernel/common/Defn.hpp"
#include "AsyncFile.h"

#if defined(USE_KQUEUE) || defined(USE_IOURING)

#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>

namespace galay::async
{

AsyncFile::AsyncFile(IOScheduler* scheduler)
    : m_handle(GHandle::invalid())
    , m_scheduler(scheduler)
    , m_controller()
{
}

AsyncFile::~AsyncFile()
{
}

AsyncFile::AsyncFile(AsyncFile&& other) noexcept
    : m_handle(other.m_handle)
    , m_scheduler(other.m_scheduler)
    , m_controller(std::move(other.m_controller))
{
    other.m_handle = GHandle::invalid();
    other.m_scheduler = nullptr;
}

AsyncFile& AsyncFile::operator=(AsyncFile&& other) noexcept
{
    if (this != &other) {
        m_handle = other.m_handle;
        m_scheduler = other.m_scheduler;
        m_controller = std::move(other.m_controller);

        other.m_handle = GHandle::invalid();
        other.m_scheduler = nullptr;
    }
    return *this;
}

std::expected<void, IOError> AsyncFile::open(const std::string& path, FileOpenMode mode, int permissions)
{
    int flags = static_cast<int>(mode);
    int fd = ::open(path.c_str(), flags, permissions);
    if (fd < 0) {
        return std::unexpected(IOError(kOpenFailed, errno));
    }
    m_handle.fd = fd;
    return {};
}

FileReadAwaitable AsyncFile::read(char* buffer, size_t length, off_t offset)
{
    return FileReadAwaitable(m_scheduler, &m_controller, m_handle, buffer, length, offset);
}

FileWriteAwaitable AsyncFile::write(const char* buffer, size_t length, off_t offset)
{
    return FileWriteAwaitable(m_scheduler, &m_controller, m_handle, buffer, length, offset);
}

CloseAwaitable AsyncFile::close()
{
    return CloseAwaitable(m_scheduler, m_handle);
}

std::expected<size_t, IOError> AsyncFile::size() const
{
    struct stat st;
    if (fstat(m_handle.fd, &st) < 0) {
        return std::unexpected(IOError(kStatFailed, errno));
    }
    return static_cast<size_t>(st.st_size);
}

std::expected<void, IOError> AsyncFile::sync()
{
    if (fsync(m_handle.fd) < 0) {
        return std::unexpected(IOError(kSyncFailed, errno));
    }
    return {};
}

} // namespace galay::async

#endif // USE_KQUEUE || USE_IOURING
