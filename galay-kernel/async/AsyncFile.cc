#include "galay-kernel/common/Defn.hpp"
#include "AsyncFile.h"

#if defined(USE_KQUEUE) || defined(USE_IOURING)

#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>

namespace galay::async
{

AsyncFile::AsyncFile()
    : m_controller(GHandle::invalid())
{
}

AsyncFile::~AsyncFile()
{
}

AsyncFile::AsyncFile(AsyncFile&& other) noexcept
    : m_controller(std::move(other.m_controller))
{
}

AsyncFile& AsyncFile::operator=(AsyncFile&& other) noexcept
{
    if (this != &other) {
        if (m_controller.m_handle != GHandle::invalid()) {
            ::close(m_controller.m_handle.fd);
        }
        m_controller = std::move(other.m_controller);
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
    m_controller.m_handle.fd = fd;
    return {};
}

FileReadAwaitable AsyncFile::read(char* buffer, size_t length, off_t offset)
{
    return FileReadAwaitable(&m_controller, buffer, length, offset);
}

FileWriteAwaitable AsyncFile::write(const char* buffer, size_t length, off_t offset)
{
    return FileWriteAwaitable(&m_controller, buffer, length, offset);
}

CloseAwaitable AsyncFile::close()
{
    return CloseAwaitable(&m_controller);
}

std::expected<size_t, IOError> AsyncFile::size() const
{
    struct stat st;
    if (fstat(m_controller.m_handle.fd, &st) < 0) {
        return std::unexpected(IOError(kStatFailed, errno));
    }
    return static_cast<size_t>(st.st_size);
}

std::expected<void, IOError> AsyncFile::sync()
{
    if (fsync(m_controller.m_handle.fd) < 0) {
        return std::unexpected(IOError(kSyncFailed, errno));
    }
    return {};
}

} // namespace galay::async

#endif // USE_KQUEUE || USE_IOURING
