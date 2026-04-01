#include "KqueueReactor.h"

#ifdef USE_KQUEUE

#include "kernel/Awaitable.h"

#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

namespace galay::kernel {

namespace {

inline bool sequenceMaskUsesSlot(uint8_t mask, IOController::Index slot) {
    return (mask & detail::sequenceSlotMask(slot)) != 0;
}

}  // namespace

KqueueReactor::KqueueReactor(int max_events, std::atomic<uint64_t>& last_error_code)
    : m_kqueue_fd(kqueue())
    , m_max_events(max_events)
    , m_last_error_code(last_error_code) {
    if (m_kqueue_fd == -1) {
        throw std::runtime_error("Failed to create kqueue");
    }

    if (pipe(m_notify_pipe) == -1) {
        close(m_kqueue_fd);
        throw std::runtime_error("Failed to create notification pipe");
    }

    int flags = fcntl(m_notify_pipe[0], F_GETFL, 0);
    if (flags < 0 || fcntl(m_notify_pipe[0], F_SETFL, flags | O_NONBLOCK) < 0) {
        close(m_notify_pipe[0]);
        close(m_notify_pipe[1]);
        close(m_kqueue_fd);
        throw std::runtime_error("Failed to set notification pipe read end non-blocking");
    }

    flags = fcntl(m_notify_pipe[1], F_GETFL, 0);
    if (flags < 0 || fcntl(m_notify_pipe[1], F_SETFL, flags | O_NONBLOCK) < 0) {
        close(m_notify_pipe[0]);
        close(m_notify_pipe[1]);
        close(m_kqueue_fd);
        throw std::runtime_error("Failed to set notification pipe write end non-blocking");
    }

    struct kevent ev;
    EV_SET(&ev, m_notify_pipe[0], EVFILT_READ, EV_ADD, 0, 0, nullptr);
    if (kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr) < 0) {
        close(m_notify_pipe[0]);
        close(m_notify_pipe[1]);
        close(m_kqueue_fd);
        throw std::runtime_error("Failed to register notification pipe to kqueue");
    }

    m_events.resize(m_max_events);
}

KqueueReactor::~KqueueReactor() {
    if (m_kqueue_fd != -1) {
        close(m_kqueue_fd);
    }
    if (m_notify_pipe[0] != -1) {
        close(m_notify_pipe[0]);
    }
    if (m_notify_pipe[1] != -1) {
        close(m_notify_pipe[1]);
    }
}

void KqueueReactor::notify() {
    char buf = 1;
    if (write(m_notify_pipe[1], &buf, 1) < 0) {
        detail::storeBackendError(
            m_last_error_code, kNotReady, static_cast<uint32_t>(errno));
    }
}

int KqueueReactor::wakeReadFdForTest() const {
    return m_notify_pipe[0];
}

int KqueueReactor::addAccept(IOController* controller) {
    auto* awaitable = controller->getAwaitable<AcceptAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return 1;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueReactor::addConnect(IOController* controller) {
    auto* awaitable = controller->getAwaitable<ConnectAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return 1;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueReactor::addRecv(IOController* controller) {
    auto* awaitable = controller->getAwaitable<RecvAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return 1;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueReactor::addSend(IOController* controller) {
    auto* awaitable = controller->getAwaitable<SendAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return 1;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueReactor::addReadv(IOController* controller) {
    auto* awaitable = controller->getAwaitable<ReadvAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return 1;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueReactor::addWritev(IOController* controller) {
    auto* awaitable = controller->getAwaitable<WritevAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return 1;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueReactor::addClose(IOController* controller) {
    if (controller == nullptr || controller->m_handle == GHandle::invalid()) {
        return 0;
    }

    const int fd = controller->m_handle.fd;
    struct kevent evs[2];
    EV_SET(&evs[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&evs[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    (void)kevent(m_kqueue_fd, evs, 2, nullptr, 0, nullptr);

    controller->m_type = IOEventType::INVALID;
    controller->m_awaitable[IOController::READ] = nullptr;
    controller->m_awaitable[IOController::WRITE] = nullptr;
    controller->m_sequence_owner[IOController::READ] = nullptr;
    controller->m_sequence_owner[IOController::WRITE] = nullptr;
    detail::clearSequenceInterestMask(controller);

    close(fd);
    controller->m_handle = GHandle::invalid();
    return 0;
}

int KqueueReactor::addFileRead(IOController* controller) {
    auto* awaitable = controller->getAwaitable<FileReadAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return 1;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueReactor::addFileWrite(IOController* controller) {
    auto* awaitable = controller->getAwaitable<FileWriteAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return 1;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueReactor::addRecvFrom(IOController* controller) {
    auto* awaitable = controller->getAwaitable<RecvFromAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return 1;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueReactor::addSendTo(IOController* controller) {
    auto* awaitable = controller->getAwaitable<SendToAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return 1;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueReactor::addFileWatch(IOController* controller) {
    auto* awaitable = controller->getAwaitable<FileWatchAwaitable>();
    if (awaitable == nullptr) return -1;

    unsigned int fflags = 0;
    const uint32_t events = static_cast<uint32_t>(awaitable->m_events);
    if (events & static_cast<uint32_t>(FileWatchEvent::Modify)) fflags |= NOTE_WRITE;
    if (events & static_cast<uint32_t>(FileWatchEvent::DeleteSelf)) fflags |= NOTE_DELETE;
    if (events & static_cast<uint32_t>(FileWatchEvent::MoveSelf)) fflags |= NOTE_RENAME;
    if (events & static_cast<uint32_t>(FileWatchEvent::Attrib)) fflags |= NOTE_ATTRIB;
    if (events & static_cast<uint32_t>(FileWatchEvent::Modify)) fflags |= NOTE_EXTEND;

    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, fflags, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueReactor::addSendFile(IOController* controller) {
    auto* awaitable = controller->getAwaitable<SendFileAwaitable>();
    if (awaitable == nullptr) return -1;
    if (awaitable->handleComplete(controller->m_handle)) {
        return 1;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueReactor::addSequence(IOController* controller) {
    if (controller == nullptr) {
        return -1;
    }
    return syncSequenceRegistration(controller);
}

int KqueueReactor::remove(IOController* controller) {
    if (controller == nullptr || controller->m_handle == GHandle::invalid()) {
        return 0;
    }
    struct kevent evs[2];
    EV_SET(&evs[0], controller->m_handle.fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&evs[1], controller->m_handle.fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    controller->m_sequence_armed_mask = 0;
    return kevent(m_kqueue_fd, evs, 2, nullptr, 0, nullptr);
}

void KqueueReactor::poll(const struct timespec& timeout, WakeCoordinator& wake_coordinator) {
    const int nev = kevent(m_kqueue_fd, nullptr, 0, m_events.data(), m_max_events, &timeout);
    if (nev < 0) {
        if (errno == EINTR) {
            return;
        }
        detail::storeBackendError(
            m_last_error_code, kNotReady, static_cast<uint32_t>(errno));
        return;
    }

    for (int i = 0; i < nev; ++i) {
        struct kevent& ev = m_events[i];
        if (ev.ident == static_cast<uintptr_t>(m_notify_pipe[0])) {
            char buf[256];
            while (read(m_notify_pipe[0], buf, sizeof(buf)) > 0) {}
            wake_coordinator.cancelPendingWake();
            continue;
        }
        if (ev.flags & EV_ERROR) {
            if (ev.data > 0) {
                detail::storeBackendError(
                    m_last_error_code, kNotReady, static_cast<uint32_t>(ev.data));
            }
            continue;
        }
        if (!ev.udata) {
            continue;
        }
        processEvent(ev);
    }
}

void KqueueReactor::processEvent(struct kevent& ev) {
    auto* controller = static_cast<IOController*>(ev.udata);
    if (!controller || controller->m_type == IOEventType::INVALID) {
        return;
    }

    const uint32_t t = static_cast<uint32_t>(controller->m_type);

    if (ev.filter == EVFILT_READ) {
        if (t & ACCEPT) {
            completeAwaitableAndWake(controller, controller->getAwaitable<AcceptAwaitable>());
        } else if (t & RECV) {
            completeAwaitableAndWake(controller, controller->getAwaitable<RecvAwaitable>());
        } else if (t & READV) {
            completeAwaitableAndWake(controller, controller->getAwaitable<ReadvAwaitable>());
        } else if (t & RECVFROM) {
            completeAwaitableAndWake(controller, controller->getAwaitable<RecvFromAwaitable>());
        } else if (t & FILEREAD) {
            completeAwaitableAndWake(controller, controller->getAwaitable<FileReadAwaitable>());
        }
    } else if (ev.filter == EVFILT_WRITE) {
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
    } else if (ev.filter == EVFILT_VNODE) {
        if (t & FILEWATCH) {
            auto* awaitable = controller->getAwaitable<FileWatchAwaitable>();
            if (awaitable) {
                FileWatchResult result;
                result.isDir = false;

                uint32_t mask = 0;
                if (ev.fflags & NOTE_WRITE) mask |= static_cast<uint32_t>(FileWatchEvent::Modify);
                if (ev.fflags & NOTE_DELETE) mask |= static_cast<uint32_t>(FileWatchEvent::DeleteSelf);
                if (ev.fflags & NOTE_RENAME) mask |= static_cast<uint32_t>(FileWatchEvent::MoveSelf);
                if (ev.fflags & NOTE_ATTRIB) mask |= static_cast<uint32_t>(FileWatchEvent::Attrib);
                if (ev.fflags & NOTE_EXTEND) mask |= static_cast<uint32_t>(FileWatchEvent::Modify);
                result.event = static_cast<FileWatchEvent>(mask);

                awaitable->m_result = std::move(result);
                awaitable->handleComplete(controller->m_handle);
                awaitable->m_waker.wakeUp();
            }
        }
    }

    if (t & SEQUENCE) {
        SequenceAwaitableBase* owner = nullptr;
        if (ev.filter == EVFILT_READ) {
            owner = controller->m_sequence_owner[IOController::READ];
            if (owner != nullptr && !owner->waitsOn(IOController::READ)) {
                owner = nullptr;
            }
        } else if (ev.filter == EVFILT_WRITE) {
            owner = controller->m_sequence_owner[IOController::WRITE];
            if (owner != nullptr && !owner->waitsOn(IOController::WRITE)) {
                owner = nullptr;
            }
        }

        if (owner == nullptr) {
            return;
        }

        const auto progress = owner->onActiveEvent(controller->m_handle);
        if (progress == SequenceProgress::kCompleted) {
            (void)detail::syncSequenceInterestMask(controller);
            owner->m_waker.wakeUp();
            return;
        }

        const int ret = addSequence(controller);
        if (ret == 1) {
            owner->m_waker.wakeUp();
        } else if (ret < 0) {
            const uint32_t sys = (ret != -1)
                ? static_cast<uint32_t>(-ret)
                : static_cast<uint32_t>(errno);
            detail::storeBackendError(m_last_error_code, kNotReady, sys);
            owner->m_waker.wakeUp();
        }
    }
}

int KqueueReactor::syncSequenceRegistration(IOController* controller) {
    if (controller == nullptr || controller->m_handle == GHandle::invalid()) {
        return -1;
    }
    const auto desired_mask = detail::syncSequenceInterestMask(controller);
    return applySequenceInterest(controller, desired_mask);
}

int KqueueReactor::applySequenceInterest(IOController* controller, uint8_t desired_mask) {
    if (controller == nullptr || controller->m_handle == GHandle::invalid()) {
        return -1;
    }

    const uint8_t armed_mask = controller->m_sequence_armed_mask;
    const uint8_t to_delete = static_cast<uint8_t>(armed_mask & ~desired_mask);
    const uint8_t to_add = static_cast<uint8_t>(desired_mask & ~armed_mask);

    if ((to_delete | to_add) == 0) {
        return 0;
    }

    struct kevent evs[4];
    int ev_count = 0;
    const int fd = controller->m_handle.fd;

    const auto append_change = [&](IOController::Index slot, uint16_t flags) {
        const int16_t filter = slot == IOController::READ ? EVFILT_READ : EVFILT_WRITE;
        const uintptr_t fflags =
            (slot == IOController::WRITE && (flags & EV_ADD) != 0) ? NOTE_LOWAT : 0;
        const intptr_t data =
            (slot == IOController::WRITE && (flags & EV_ADD) != 0) ? 1 : 0;
        EV_SET(&evs[ev_count++], fd, filter, flags, fflags, data, controller);
    };

    if (sequenceMaskUsesSlot(to_delete, IOController::READ)) {
        append_change(IOController::READ, EV_DELETE);
    }
    if (sequenceMaskUsesSlot(to_delete, IOController::WRITE)) {
        append_change(IOController::WRITE, EV_DELETE);
    }
    if (sequenceMaskUsesSlot(to_add, IOController::READ)) {
        append_change(IOController::READ, EV_ADD | EV_CLEAR);
    }
    if (sequenceMaskUsesSlot(to_add, IOController::WRITE)) {
        append_change(IOController::WRITE, EV_ADD | EV_CLEAR);
    }

    const int ret = kevent(m_kqueue_fd, evs, ev_count, nullptr, 0, nullptr);
    if (ret == 0) {
        controller->m_sequence_armed_mask = desired_mask;
    }
    return ret;
}

}  // namespace galay::kernel

#endif  // USE_KQUEUE
