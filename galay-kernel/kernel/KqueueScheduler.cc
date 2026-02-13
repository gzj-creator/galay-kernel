#include "KqueueScheduler.h"
#include "common/Defn.hpp"
#include "kernel/Awaitable.h"
#include "kernel/IOController.hpp"

#ifdef USE_KQUEUE

#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <stdexcept>

namespace galay::kernel
{

KqueueScheduler::KqueueScheduler(int max_events, int batch_size, int check_interval_ms)
    : m_kqueue_fd(kqueue())
    , m_running(false)
    , m_max_events(max_events)
    , m_batch_size(batch_size)
    , m_check_interval_ms(check_interval_ms)
{
    if (m_kqueue_fd == -1) {
        throw std::runtime_error("Failed to create kqueue");
    }

    // Create notification pipe
    if (pipe(m_notify_pipe) == -1) {
        close(m_kqueue_fd);
        throw std::runtime_error("Failed to create notification pipe");
    }

    // Set pipe non-blocking
    int flags = fcntl(m_notify_pipe[0], F_GETFL, 0);
    fcntl(m_notify_pipe[0], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(m_notify_pipe[1], F_GETFL, 0);
    fcntl(m_notify_pipe[1], F_SETFL, flags | O_NONBLOCK);

    // Add pipe read end to kqueue for notification
    struct kevent ev;
    EV_SET(&ev, m_notify_pipe[0], EVFILT_READ, EV_ADD, 0, 0, nullptr);
    kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);

    m_events.resize(m_max_events);
    m_coro_buffer.resize(m_batch_size);
}

KqueueScheduler::~KqueueScheduler()
{
    stop();
    if (m_kqueue_fd != -1) {
        close(m_kqueue_fd);
    }
    close(m_notify_pipe[0]);
    close(m_notify_pipe[1]);
}

void KqueueScheduler::start()
{
    if (m_running.exchange(true, std::memory_order_acq_rel)) {
        return; // Already running
    }

    m_thread = std::thread([this]() {
        m_threadId = std::this_thread::get_id();  // 设置调度器线程ID
        eventLoop();
    });
}

void KqueueScheduler::stop()
{
    if (!m_running.exchange(false, std::memory_order_acq_rel)) {
        return; // Already stopped
    }

    notify();

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void KqueueScheduler::notify()
{
    char buf = 1;
    write(m_notify_pipe[1], &buf, 1);
}

int KqueueScheduler::addAccept(IOController* controller)
{
    auto awaitable = controller->getAwaitable<AcceptAwaitable>();
    if(awaitable == nullptr) return -1;
    if(awaitable->handleComplete(controller->m_handle)) {
        return OK;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addConnect(IOController* controller)
{
    auto awaitable = controller->getAwaitable<ConnectAwaitable>();
    if(awaitable == nullptr) return -1;
    if(awaitable->handleComplete(controller->m_handle)) {
        return OK;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addRecv(IOController* controller)
{
    auto awaitable = controller->getAwaitable<RecvAwaitable>();
    if(awaitable == nullptr) return -1;
    if(awaitable->handleComplete(controller->m_handle)) {
        return OK;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addSend(IOController* controller)
{
    auto awaitable = controller->getAwaitable<SendAwaitable>();
    if(awaitable == nullptr) return -1;
    if(awaitable->handleComplete(controller->m_handle)) {
        return OK;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addReadv(IOController* controller)
{
    auto awaitable = controller->getAwaitable<ReadvAwaitable>();
    if(awaitable == nullptr) return -1;
    if(awaitable->handleComplete(controller->m_handle)) {
        return OK;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addWritev(IOController* controller)
{
    auto awaitable = controller->getAwaitable<WritevAwaitable>();
    if(awaitable == nullptr) return -1;
    if(awaitable->handleComplete(controller->m_handle)) {
        return OK;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addClose(IOController* contoller)
{
    if (contoller->m_handle == GHandle::invalid()) {
        return 0;
    }
    close(contoller->m_handle.fd);
    contoller->m_handle = GHandle::invalid();
    remove(contoller);
    return 0;
}

int KqueueScheduler::addFileRead(IOController* controller)
{
    auto awaitable = controller->getAwaitable<FileReadAwaitable>();
    if(awaitable == nullptr) return -1;
    if(awaitable->handleComplete(controller->m_handle)) {
        return OK;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addFileWrite(IOController* controller)
{
    auto awaitable = controller->getAwaitable<FileWriteAwaitable>();
    if(awaitable == nullptr) return -1;
    if(awaitable->handleComplete(controller->m_handle)) {
        return OK;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addSendFile(IOController* controller)
{
    auto awaitable = controller->getAwaitable<SendFileAwaitable>();
    if(awaitable == nullptr) return -1;
    if(awaitable->handleComplete(controller->m_handle)) {
        return OK;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addCustom(IOController* controller)
{
    auto* custom = controller->getAwaitable<CustomAwaitable>();
    if(custom == nullptr) return -1;
    while (auto* task = custom->front()) {
        bool done = task->context->handleComplete(controller->m_handle);
        if (done) { custom->popFront(); continue; }
        return processCustom(task->type, controller);
    }
    return OK;  // 队列空，由调用方决定是否唤醒
}

int KqueueScheduler::remove(IOController* controller)
{
    struct kevent evs[2];
    EV_SET(&evs[0], controller->m_handle.fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&evs[1], controller->m_handle.fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    return kevent(m_kqueue_fd, evs, 2, nullptr, 0, nullptr);
}

bool KqueueScheduler::spawn(Coroutine co)
{
    auto* scheduler = co.belongScheduler();
    // 如果协程未绑定 scheduler，绑定到当前 scheduler
    if (!scheduler) {
        co.belongScheduler(this);
    } else {
        if(scheduler != this) return false;
    }
    m_coro_queue.enqueue(std::move(co));
    return true;
}

bool KqueueScheduler::spawnImmidiately(Coroutine co)
{
    auto* scheduler = co.belongScheduler();
    if (scheduler) {
        return false;
    }
    co.belongScheduler(this);
    resume(co);
    return true;
}

void KqueueScheduler::processPendingCoroutines()
{
    // 循环处理直到队列为空，因为resume可能会spawn新协程
    while (true) {
        size_t count = m_coro_queue.try_dequeue_bulk(m_coro_buffer.data(), m_batch_size);
        if (count == 0) {
            break;
        }
        for (size_t i = 0; i < count; ++i) {
           Scheduler::resume( m_coro_buffer[i]);
        }
    }
}



void KqueueScheduler::eventLoop()
{
    // kevent 超时时间公式：timeout = tickDuration / 2
    // 使用时间轮精度的一半作为超时，确保定时器最大误差不超过半个 tick
    // 例如：tickDuration = 50ms 时，timeout = 25ms，最大误差 ≤ 25ms
    uint64_t tick_duration_ns = m_timer_manager.during();
    uint64_t timeout_ns = tick_duration_ns / 2;
    struct timespec timeout;
    timeout.tv_sec = timeout_ns / 1000000000ULL;
    timeout.tv_nsec = timeout_ns % 1000000000ULL;

    while (m_running.load(std::memory_order_relaxed)) {
        // Process pending coroutines
        processPendingCoroutines();
        m_timer_manager.tick();
        int nev = kevent(m_kqueue_fd, nullptr, 0, m_events.data(), m_max_events, &timeout);

        if (nev < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        for (int i = 0; i < nev; ++i) {
            struct kevent& ev = m_events[i];
            if (ev.ident == static_cast<uintptr_t>(m_notify_pipe[0])) {
                char buf[256];
                while (read(m_notify_pipe[0], buf, sizeof(buf)) > 0);
                continue;
            }
            if (ev.flags & EV_ERROR) {
                continue;
            }
            if (!ev.udata) {
                continue;
            }

            processEvent(ev);
        }
    }
}

void KqueueScheduler::processEvent(struct kevent& ev)
{
    IOController* controller = static_cast<IOController*>(ev.udata);
    if (!controller || controller->m_type == IOEventType::INVALID) {
        return;
    }

    // 检查错误标志
    if (ev.flags & EV_ERROR) {
        // 处理错误
        return;
    }

    uint32_t t = static_cast<uint32_t>(controller->m_type);

    // ===== 读方向事件 =====
    if (ev.filter == EVFILT_READ) {
        if (t & ACCEPT) {
            AcceptAwaitable* awaitable = controller->getAwaitable<AcceptAwaitable>();
            if (awaitable) {
                if(awaitable->handleComplete(controller->m_handle)) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & RECV) {
            RecvAwaitable* awaitable = controller->getAwaitable<RecvAwaitable>();
            if (awaitable) {
                if(awaitable->handleComplete(controller->m_handle)) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & READV) {
            ReadvAwaitable* awaitable = controller->getAwaitable<ReadvAwaitable>();
            if (awaitable) {
                if(awaitable->handleComplete(controller->m_handle)) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & RECVFROM) {
            RecvFromAwaitable* awaitable = controller->getAwaitable<RecvFromAwaitable>();
            if (awaitable) {
                if(awaitable->handleComplete(controller->m_handle)) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & FILEREAD) {
            FileReadAwaitable* awaitable = controller->getAwaitable<FileReadAwaitable>();
            if (awaitable) {
                if(awaitable->handleComplete(controller->m_handle)) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
    }
    // ===== 写方向事件 =====
    else if (ev.filter == EVFILT_WRITE) {
        if (t & CONNECT) {
            ConnectAwaitable* awaitable = controller->getAwaitable<ConnectAwaitable>();
            if (awaitable) {
                if(awaitable->handleComplete(controller->m_handle)) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & SEND) {
            SendAwaitable* awaitable = controller->getAwaitable<SendAwaitable>();
            if (awaitable) {
                if(awaitable->handleComplete(controller->m_handle)) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & WRITEV) {
            WritevAwaitable* awaitable = controller->getAwaitable<WritevAwaitable>();
            if (awaitable) {
                if(awaitable->handleComplete(controller->m_handle)) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & SENDTO) {
            SendToAwaitable* awaitable = controller->getAwaitable<SendToAwaitable>();
            if (awaitable) {
                if(awaitable->handleComplete(controller->m_handle)) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & FILEWRITE) {
            FileWriteAwaitable* awaitable = controller->getAwaitable<FileWriteAwaitable>();
            if (awaitable) {
                if(awaitable->handleComplete(controller->m_handle)) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
        else if (t & SENDFILE) {
            SendFileAwaitable* awaitable = controller->getAwaitable<SendFileAwaitable>();
            if (awaitable) {
                if(awaitable->handleComplete(controller->m_handle)) {
                    awaitable->m_waker.wakeUp();
                }
            }
        }
    }
    // ===== 文件监控事件 =====
    else if (ev.filter == EVFILT_VNODE) {
        if (t & FILEWATCH) {
            FileWatchAwaitable* awaitable = controller->getAwaitable<FileWatchAwaitable>();
            if (awaitable) {
                FileWatchResult result;
                result.isDir = false;  // kqueue 不直接提供此信息

                // 转换 kqueue fflags 到 FileWatchEvent
                uint32_t mask = 0;
                if (ev.fflags & NOTE_WRITE)   mask |= static_cast<uint32_t>(FileWatchEvent::Modify);
                if (ev.fflags & NOTE_DELETE)  mask |= static_cast<uint32_t>(FileWatchEvent::DeleteSelf);
                if (ev.fflags & NOTE_RENAME)  mask |= static_cast<uint32_t>(FileWatchEvent::MoveSelf);
                if (ev.fflags & NOTE_ATTRIB)  mask |= static_cast<uint32_t>(FileWatchEvent::Attrib);
                if (ev.fflags & NOTE_EXTEND)  mask |= static_cast<uint32_t>(FileWatchEvent::Modify);
                result.event = static_cast<FileWatchEvent>(mask);

                awaitable->m_result = std::move(result);
                awaitable->handleComplete(controller->m_handle);
                awaitable->m_waker.wakeUp();
            }
        }
    }
    // ===== 自定义事件 =====
    if (t & CUSTOM) {
        CustomAwaitable* custom = controller->getAwaitable<CustomAwaitable>();
        if (custom) {
            auto* task = custom->front();
            if (task) {
                bool done = task->context->handleComplete(controller->m_handle);
                if (done) {
                    custom->popFront();
                    if (custom->empty()) {
                        custom->m_waker.wakeUp();
                    } else {
                        if (addCustom(controller) == OK) {
                            custom->m_waker.wakeUp();
                        }
                    }
                } else {
                    processCustom(task->type, controller);
                }
            }
        }
    }
}

int KqueueScheduler::processCustom(IOEventType type, IOController* controller)
{
    struct kevent ev;
    switch (type) {
        case ACCEPT:
        case RECV:
        case READV:
        case RECVFROM:
        case FILEREAD:
            EV_SET(&ev, controller->m_handle.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, controller);
            return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
        case CONNECT:
        case SEND:
        case WRITEV:
        case SENDTO:
        case FILEWRITE:
        case SENDFILE:
            EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, controller);
            return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
        default:
            return -1;
    }
}

int KqueueScheduler::addRecvFrom(IOController* controller)
{
    auto awaitable = controller->getAwaitable<RecvFromAwaitable>();
    if(awaitable == nullptr) return -1;
    if(awaitable->handleComplete(controller->m_handle)) {
        return OK;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addSendTo(IOController* controller)
{
    auto awaitable = controller->getAwaitable<SendToAwaitable>();
    if(awaitable == nullptr) return -1;
    if(awaitable->handleComplete(controller->m_handle)) {
        return OK;
    }
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}

int KqueueScheduler::addFileWatch(IOController* controller)
{
    FileWatchAwaitable* awaitable = controller->getAwaitable<FileWatchAwaitable>();
    if(awaitable == nullptr) return -1;

    // 将 FileWatchEvent 转换为 kqueue fflags
    unsigned int fflags = 0;
    uint32_t events = static_cast<uint32_t>(awaitable->m_events);
    if (events & static_cast<uint32_t>(FileWatchEvent::Modify))     fflags |= NOTE_WRITE;
    if (events & static_cast<uint32_t>(FileWatchEvent::DeleteSelf)) fflags |= NOTE_DELETE;
    if (events & static_cast<uint32_t>(FileWatchEvent::MoveSelf))   fflags |= NOTE_RENAME;
    if (events & static_cast<uint32_t>(FileWatchEvent::Attrib))     fflags |= NOTE_ATTRIB;
    // NOTE_EXTEND 也映射到 Modify（文件扩展）
    if (events & static_cast<uint32_t>(FileWatchEvent::Modify))     fflags |= NOTE_EXTEND;

    // kqueue 使用 EVFILT_VNODE 监控文件变化
    struct kevent ev;
    EV_SET(&ev, controller->m_handle.fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, fflags, 0, controller);
    return kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
}


}

#endif // USE_KQUEUE
