#include "AioFile.h"

#ifdef USE_EPOLL

#include "galay-kernel/kernel/EpollScheduler.h"
#include <unistd.h>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace galay::async
{

// AioCommitAwaitable 实现

AioCommitAwaitable::AioCommitAwaitable(EpollScheduler* scheduler, io_context_t aio_ctx, int event_fd, size_t pending_count)
    : m_scheduler(scheduler)
    , m_aio_ctx(aio_ctx)
    , m_event_fd(event_fd)
    , m_pending_count(pending_count)
{
}

bool AioCommitAwaitable::await_suspend(std::coroutine_handle<> handle)
{
    m_waker = Waker(handle);

    if (m_pending_count == 0) {
        m_result = std::vector<ssize_t>{};
        return false;
    }

    // 将 eventfd 注册到 epoll
    int ret = m_scheduler->addAioCommit(this);
    if (ret < 0) {
        m_result = std::unexpected(IOError(kReadFailed, static_cast<uint32_t>(-ret)));
        return false;
    }

    return true;  // 挂起，等待 epoll 事件
}

std::expected<std::vector<ssize_t>, IOError> AioCommitAwaitable::await_resume()
{
    return std::move(m_result);
}

// AioFile 实现

AioFile::AioFile(EpollScheduler* scheduler, int max_events)
    : m_handle(GHandle::invalid())
    , m_scheduler(scheduler)
    , m_aio_ctx(0)
    , m_event_fd(-1)
    , m_max_events(max_events)
{
    // 创建 AIO context
    if (io_setup(max_events, &m_aio_ctx) < 0) {
        m_aio_ctx = 0;
    }

    // 创建 eventfd 用于通知
    m_event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
}

AioFile::~AioFile()
{
    close();
    if (m_aio_ctx) {
        io_destroy(m_aio_ctx);
    }
    if (m_event_fd >= 0) {
        ::close(m_event_fd);
    }
}

AioFile::AioFile(AioFile&& other) noexcept
    : m_handle(other.m_handle)
    , m_scheduler(other.m_scheduler)
    , m_aio_ctx(other.m_aio_ctx)
    , m_event_fd(other.m_event_fd)
    , m_max_events(other.m_max_events)
    , m_pending_cbs(std::move(other.m_pending_cbs))
    , m_pending_ptrs(std::move(other.m_pending_ptrs))
{
    other.m_handle = GHandle::invalid();
    other.m_scheduler = nullptr;
    other.m_aio_ctx = 0;
    other.m_event_fd = -1;
}

AioFile& AioFile::operator=(AioFile&& other) noexcept
{
    if (this != &other) {
        close();
        if (m_aio_ctx) {
            io_destroy(m_aio_ctx);
        }
        if (m_event_fd >= 0) {
            ::close(m_event_fd);
        }

        m_handle = other.m_handle;
        m_scheduler = other.m_scheduler;
        m_aio_ctx = other.m_aio_ctx;
        m_event_fd = other.m_event_fd;
        m_max_events = other.m_max_events;
        m_pending_cbs = std::move(other.m_pending_cbs);
        m_pending_ptrs = std::move(other.m_pending_ptrs);

        other.m_handle = GHandle::invalid();
        other.m_scheduler = nullptr;
        other.m_aio_ctx = 0;
        other.m_event_fd = -1;
    }
    return *this;
}

std::expected<void, IOError> AioFile::open(const std::string& path, AioOpenMode mode, int permissions)
{
    int flags = static_cast<int>(mode);
    int fd = ::open(path.c_str(), flags, permissions);
    if (fd < 0) {
        return std::unexpected(IOError(kOpenFailed, errno));
    }
    m_handle.fd = fd;
    return {};
}

void AioFile::preRead(char* buffer, size_t length, off_t offset)
{
    struct iocb cb;
    std::memset(&cb, 0, sizeof(cb));
    io_prep_pread(&cb, m_handle.fd, buffer, length, offset);
    io_set_eventfd(&cb, m_event_fd);

    m_pending_cbs.push_back(cb);
}

void AioFile::preWrite(const char* buffer, size_t length, off_t offset)
{
    struct iocb cb;
    std::memset(&cb, 0, sizeof(cb));
    io_prep_pwrite(&cb, m_handle.fd, const_cast<char*>(buffer), length, offset);
    io_set_eventfd(&cb, m_event_fd);

    m_pending_cbs.push_back(cb);
}

void AioFile::preReadBatch(const std::vector<std::tuple<char*, size_t, off_t>>& reads)
{
    for (const auto& [buffer, length, offset] : reads) {
        preRead(buffer, length, offset);
    }
}

void AioFile::preWriteBatch(const std::vector<std::tuple<const char*, size_t, off_t>>& writes)
{
    for (const auto& [buffer, length, offset] : writes) {
        preWrite(buffer, length, offset);
    }
}

AioCommitAwaitable AioFile::commit()
{
    // 更新指针数组
    m_pending_ptrs.clear();
    m_pending_ptrs.reserve(m_pending_cbs.size());
    for (auto& cb : m_pending_cbs) {
        m_pending_ptrs.push_back(&cb);
    }

    size_t pending_count = m_pending_ptrs.size();

    // 提交所有 AIO 请求
    if (pending_count > 0) {
        int ret = io_submit(m_aio_ctx, m_pending_ptrs.size(), m_pending_ptrs.data());
        if (ret < 0) {
            // 提交失败，清空并返回错误
            clear();
            AioCommitAwaitable awaitable(m_scheduler, m_aio_ctx, m_event_fd, 0);
            awaitable.m_result = std::unexpected(IOError(kWriteFailed, static_cast<uint32_t>(-ret)));
            return awaitable;
        }
    }

    // 清空待提交列表
    clear();

    return AioCommitAwaitable(m_scheduler, m_aio_ctx, m_event_fd, pending_count);
}

void AioFile::clear()
{
    m_pending_cbs.clear();
    m_pending_ptrs.clear();
}

void AioFile::close()
{
    if (m_handle.fd >= 0) {
        ::close(m_handle.fd);
        m_handle = GHandle::invalid();
    }
}

std::expected<size_t, IOError> AioFile::size() const
{
    struct stat st;
    if (fstat(m_handle.fd, &st) < 0) {
        return std::unexpected(IOError(kStatFailed, errno));
    }
    return static_cast<size_t>(st.st_size);
}

std::expected<void, IOError> AioFile::sync()
{
    if (fsync(m_handle.fd) < 0) {
        return std::unexpected(IOError(kSyncFailed, errno));
    }
    return {};
}

char* AioFile::allocAlignedBuffer(size_t size, size_t alignment)
{
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return nullptr;
    }
    return static_cast<char*>(ptr);
}

void AioFile::freeAlignedBuffer(char* buffer)
{
    free(buffer);
}

} // namespace galay::async

#endif // USE_EPOLL
