#include "EpollReactor.h"

#ifdef USE_EPOLL

#include "kernel/Awaitable.h"
#include "galay-kernel/async/AioFile.h"

#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <cerrno>
#include <expected>
#include <stdexcept>
#include <string>
#include <vector>

namespace galay::kernel {

namespace {

constexpr int kImmediateReady = 1;

}  // namespace

EpollReactor::EpollReactor(int max_events, std::atomic<uint64_t>& last_error_code)
    : m_epoll_fd(epoll_create1(EPOLL_CLOEXEC))
    , m_event_fd(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC))
    , m_max_events(max_events)
    , m_last_error_code(last_error_code) {
    if (m_epoll_fd == -1) {
        throw std::runtime_error("Failed to create epoll");
    }

    if (m_event_fd == -1) {
        close(m_epoll_fd);
        throw std::runtime_error("Failed to create eventfd");
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = nullptr;
    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_event_fd, &ev) == -1) {
        close(m_epoll_fd);
        close(m_event_fd);
        throw std::runtime_error("Failed to add eventfd to epoll");
    }

    m_events.resize(m_max_events);
}

EpollReactor::~EpollReactor() {
    if (m_epoll_fd != -1) {
        close(m_epoll_fd);
    }
    if (m_event_fd != -1) {
        close(m_event_fd);
    }
}

void EpollReactor::notify() {
    uint64_t val = 1;
    if (write(m_event_fd, &val, sizeof(val)) < 0) {
        detail::storeBackendError(
            m_last_error_code, kNotReady, static_cast<uint32_t>(errno));
    }
}

int EpollReactor::wakeReadFdForTest() const {
    return m_event_fd;
}

uint32_t EpollReactor::buildEvents(IOController* controller) const {
    uint32_t events = EPOLLET;
    const uint32_t t = static_cast<uint32_t>(controller->m_type);
    if (t & (ACCEPT | RECV | READV | RECVFROM | FILEREAD | FILEWATCH)) {
        events |= EPOLLIN;
    }
    if (t & (CONNECT | SEND | WRITEV | SENDTO | SENDFILE | FILEWRITE)) {
        events |= EPOLLOUT;
    }
    return events;
}

int EpollReactor::applyEvents(IOController* controller, uint32_t events) {
    if (controller == nullptr || controller->m_handle == GHandle::invalid()) {
        return -1;
    }

    if (events == controller->m_registered_events) {
        return 0;
    }

    const int fd = controller->m_handle.fd;
    if (events == EPOLLET) {
        const int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
        if (ret == 0 || errno == ENOENT) {
            controller->m_registered_events = 0;
            return 0;
        }
        return ret;
    }

    struct epoll_event ev;
    ev.events = events;
    ev.data.ptr = controller;

    int ret = 0;
    if (controller->m_registered_events == 0) {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    } else {
        ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, fd, &ev);
        if (ret == -1 && errno == ENOENT) {
            ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
        }
    }

    if (ret == 0) {
        controller->m_registered_events = events;
    }
    return ret;
}

int EpollReactor::addAccept(IOController* controller) {
    auto* awaitable = controller->getAwaitable<AcceptAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return kImmediateReady;
    }
    return applyEvents(controller, buildEvents(controller));
}

int EpollReactor::addConnect(IOController* controller) {
    auto* awaitable = controller->getAwaitable<ConnectAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return kImmediateReady;
    }
    return applyEvents(controller, buildEvents(controller));
}

int EpollReactor::addRecv(IOController* controller) {
    auto* awaitable = controller->getAwaitable<RecvAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return kImmediateReady;
    }
    return applyEvents(controller, buildEvents(controller));
}

int EpollReactor::addSend(IOController* controller) {
    auto* awaitable = controller->getAwaitable<SendAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return kImmediateReady;
    }
    return applyEvents(controller, buildEvents(controller));
}

int EpollReactor::addReadv(IOController* controller) {
    auto* awaitable = controller->getAwaitable<ReadvAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return kImmediateReady;
    }
    return applyEvents(controller, buildEvents(controller));
}

int EpollReactor::addWritev(IOController* controller) {
    auto* awaitable = controller->getAwaitable<WritevAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return kImmediateReady;
    }
    return applyEvents(controller, buildEvents(controller));
}

int EpollReactor::addSendFile(IOController* controller) {
    auto* awaitable = controller->getAwaitable<SendFileAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return kImmediateReady;
    }
    return applyEvents(controller, buildEvents(controller));
}

int EpollReactor::addClose(IOController* controller) {
    if (controller == nullptr || controller->m_handle == GHandle::invalid()) {
        return 0;
    }

    const int fd = controller->m_handle.fd;
    (void)applyEvents(controller, EPOLLET);

    controller->m_type = IOEventType::INVALID;
    controller->m_awaitable[IOController::READ] = nullptr;
    controller->m_awaitable[IOController::WRITE] = nullptr;
    controller->m_registered_events = 0;

    close(fd);
    controller->m_handle = GHandle::invalid();
    return 0;
}

int EpollReactor::addFileRead(IOController* controller) {
    return applyEvents(controller, EPOLLIN | EPOLLET);
}

int EpollReactor::addFileWrite(IOController* controller) {
    return applyEvents(controller, EPOLLOUT | EPOLLET);
}

int EpollReactor::addRecvFrom(IOController* controller) {
    auto* awaitable = controller->getAwaitable<RecvFromAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return kImmediateReady;
    }
    return applyEvents(controller, buildEvents(controller));
}

int EpollReactor::addSendTo(IOController* controller) {
    auto* awaitable = controller->getAwaitable<SendToAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return kImmediateReady;
    }
    return applyEvents(controller, buildEvents(controller));
}

int EpollReactor::addFileWatch(IOController* controller) {
    auto* awaitable = controller->getAwaitable<FileWatchAwaitable>();
    if (awaitable == nullptr) return -1;
    return applyEvents(controller, buildEvents(controller));
}

int EpollReactor::addSequence(IOController* controller) {
    auto* sequence = controller->getAwaitable<SequenceAwaitableBase>();
    if (sequence == nullptr) return -1;

    if (sequence->prepareForSubmit(controller->m_handle) == SequenceProgress::kCompleted) {
        return kImmediateReady;
    }

    auto* task = sequence->front();
    if (task == nullptr) {
        return kImmediateReady;
    }
    return processSequence(sequence->resolveTaskEventType(*task), controller);
}

int EpollReactor::remove(IOController* controller) {
    if (controller == nullptr || controller->m_handle == GHandle::invalid()) {
        return 0;
    }
    return applyEvents(controller, EPOLLET);
}

int EpollReactor::processSequence(IOEventType type, IOController* controller) {
    const uint32_t t = static_cast<uint32_t>(type);
    uint32_t events = EPOLLET;
    if (t & (ACCEPT | RECV | READV | RECVFROM | FILEREAD)) {
        events |= EPOLLIN;
    }
    if (t & (CONNECT | SEND | WRITEV | SENDTO | SENDFILE | FILEWRITE)) {
        events |= EPOLLOUT;
    }
    if (events == EPOLLET) {
        return -1;
    }
    return applyEvents(controller, events);
}

void EpollReactor::syncEvents(IOController* controller) {
    const uint32_t events = buildEvents(controller);
    if (applyEvents(controller, events) < 0) {
        detail::storeBackendError(
            m_last_error_code, kNotReady, static_cast<uint32_t>(errno));
    }
}

void EpollReactor::poll(int timeout_ms, WakeCoordinator& wake_coordinator) {
    const int nev = epoll_wait(m_epoll_fd, m_events.data(), m_max_events, timeout_ms);
    if (nev < 0) {
        if (errno == EINTR) {
            return;
        }
        detail::storeBackendError(
            m_last_error_code, kNotReady, static_cast<uint32_t>(errno));
        return;
    }

    for (int i = 0; i < nev; ++i) {
        struct epoll_event& ev = m_events[i];
        if (ev.data.ptr == nullptr) {
            uint64_t val = 0;
            while (read(m_event_fd, &val, sizeof(val)) > 0) {}
            wake_coordinator.cancelPendingWake();
            continue;
        }

        if (ev.events & EPOLLERR) {
            ev.events |= (EPOLLIN | EPOLLOUT);
        }

        processEvent(ev);
    }
}

void EpollReactor::processEvent(struct epoll_event& ev) {
    auto* controller = static_cast<IOController*>(ev.data.ptr);
    if (!controller || controller->m_type == IOEventType::INVALID) {
        return;
    }

    const uint32_t t = static_cast<uint32_t>(controller->m_type);

    if (ev.events & EPOLLIN) {
        if (t & ACCEPT) {
            completeAwaitableAndWake(controller, controller->getAwaitable<AcceptAwaitable>());
        } else if (t & RECV) {
            completeAwaitableAndWake(controller, controller->getAwaitable<RecvAwaitable>());
        } else if (t & READV) {
            completeAwaitableAndWake(controller, controller->getAwaitable<ReadvAwaitable>());
        } else if (t & RECVFROM) {
            completeAwaitableAndWake(controller, controller->getAwaitable<RecvFromAwaitable>());
        } else if (t & FILEREAD) {
            auto* aio_awaitable =
                static_cast<galay::async::AioCommitAwaitable*>(controller->m_awaitable[IOController::READ]);
            if (aio_awaitable) {
                uint64_t completed = 0;
                const ssize_t n = read(controller->m_handle.fd, &completed, sizeof(completed));
                if (n == static_cast<ssize_t>(sizeof(completed)) && completed > 0) {
                    std::vector<struct io_event> events(aio_awaitable->m_pending_count);
                    const int num_events = io_getevents(aio_awaitable->m_aio_ctx,
                                                        1,
                                                        aio_awaitable->m_pending_count,
                                                        events.data(),
                                                        nullptr);
                    if (num_events > 0) {
                        std::vector<ssize_t> results;
                        results.reserve(static_cast<size_t>(num_events));
                        for (int i = 0; i < num_events; ++i) {
                            results.push_back(events[static_cast<size_t>(i)].res);
                        }
                        aio_awaitable->m_result = std::move(results);
                    } else {
                        aio_awaitable->m_result = std::unexpected(IOError(kReadFailed, errno));
                    }
                } else {
                    aio_awaitable->m_result = std::unexpected(IOError(kReadFailed, errno));
                }

                (void)epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, controller->m_handle.fd, nullptr);
                aio_awaitable->m_waker.wakeUp();
            }
        } else if (t & FILEWATCH) {
            auto* awaitable = controller->getAwaitable<FileWatchAwaitable>();
            if (awaitable) {
                const ssize_t len =
                    read(controller->m_handle.fd, awaitable->m_buffer, awaitable->m_buffer_size);
                if (len > 0) {
                    auto* event = reinterpret_cast<struct inotify_event*>(awaitable->m_buffer);
                    FileWatchResult result;
                    result.isDir = (event->mask & IN_ISDIR) != 0;
                    if (event->len > 0) {
                        result.name = std::string(event->name);
                    }

                    uint32_t mask = 0;
                    if (event->mask & IN_ACCESS) mask |= static_cast<uint32_t>(FileWatchEvent::Access);
                    if (event->mask & IN_MODIFY) mask |= static_cast<uint32_t>(FileWatchEvent::Modify);
                    if (event->mask & IN_ATTRIB) mask |= static_cast<uint32_t>(FileWatchEvent::Attrib);
                    if (event->mask & IN_CLOSE_WRITE) mask |= static_cast<uint32_t>(FileWatchEvent::CloseWrite);
                    if (event->mask & IN_CLOSE_NOWRITE) mask |= static_cast<uint32_t>(FileWatchEvent::CloseNoWrite);
                    if (event->mask & IN_OPEN) mask |= static_cast<uint32_t>(FileWatchEvent::Open);
                    if (event->mask & IN_MOVED_FROM) mask |= static_cast<uint32_t>(FileWatchEvent::MovedFrom);
                    if (event->mask & IN_MOVED_TO) mask |= static_cast<uint32_t>(FileWatchEvent::MovedTo);
                    if (event->mask & IN_CREATE) mask |= static_cast<uint32_t>(FileWatchEvent::Create);
                    if (event->mask & IN_DELETE) mask |= static_cast<uint32_t>(FileWatchEvent::Delete);
                    if (event->mask & IN_DELETE_SELF) mask |= static_cast<uint32_t>(FileWatchEvent::DeleteSelf);
                    if (event->mask & IN_MOVE_SELF) mask |= static_cast<uint32_t>(FileWatchEvent::MoveSelf);
                    result.event = static_cast<FileWatchEvent>(mask);
                    awaitable->m_result = std::move(result);
                } else if (len == 0) {
                    awaitable->m_result = std::unexpected(IOError(kReadFailed, 0));
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    awaitable->m_result =
                        std::unexpected(IOError(kReadFailed, static_cast<uint32_t>(errno)));
                }

                awaitable->handleComplete(controller->m_handle);
                awaitable->m_waker.wakeUp();
            }
        }
    }

    if (ev.events & EPOLLOUT) {
        if (t & CONNECT) {
            completeAwaitableAndWake(controller, controller->getAwaitable<ConnectAwaitable>());
        } else if (t & SEND) {
            completeAwaitableAndWake(controller, controller->getAwaitable<SendAwaitable>());
        } else if (t & WRITEV) {
            completeAwaitableAndWake(controller, controller->getAwaitable<WritevAwaitable>());
        } else if (t & SENDTO) {
            completeAwaitableAndWake(controller, controller->getAwaitable<SendToAwaitable>());
        } else if (t & FILEWRITE) {
            completeAwaitableAndWake(controller, controller->getAwaitable<FileWriteAwaitable>());
        } else if (t & SENDFILE) {
            completeAwaitableAndWake(controller, controller->getAwaitable<SendFileAwaitable>());
        }
    }

    if (t & SEQUENCE) {
        auto* sequence = controller->getAwaitable<SequenceAwaitableBase>();
        if (sequence) {
            const auto progress = sequence->onActiveEvent(controller->m_handle);
            if (progress == SequenceProgress::kCompleted) {
                sequence->m_waker.wakeUp();
            } else {
                const int ret = addSequence(controller);
                if (ret == kImmediateReady) {
                    sequence->m_waker.wakeUp();
                } else if (ret < 0) {
                    const uint32_t sys = (ret != -1)
                        ? static_cast<uint32_t>(-ret)
                        : static_cast<uint32_t>(errno);
                    detail::storeBackendError(m_last_error_code, kNotReady, sys);
                    sequence->m_waker.wakeUp();
                }
            }
        }
    }

    syncEvents(controller);
}

}  // namespace galay::kernel

#endif  // USE_EPOLL
