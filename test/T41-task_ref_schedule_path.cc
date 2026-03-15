/**
 * @file T41-task_ref_schedule_path.cc
 * @brief 用途：验证 `TaskRef` 走调度路径时会进入预期的 scheduler 恢复流程。
 * 关键覆盖点：TaskRef 调度入口、本地或远端派发、owner scheduler 恢复。
 * 通过条件：TaskRef 调度路径命中预期断言，测试返回 0。
 */

#include "galay-kernel/kernel/Scheduler.hpp"
#include "galay-kernel/kernel/Waker.h"
#include <iostream>

using namespace galay::kernel;

namespace {

Coroutine pendingTask() {
    co_return;
}

class CaptureScheduler final : public Scheduler {
public:
    void start() override {}
    void stop() override {}

    bool spawn(Coroutine) override {
        ++spawn_calls;
        return true;
    }

    bool spawnImmidiately(Coroutine) override {
        ++spawn_immediately_calls;
        return true;
    }

    bool schedule(TaskRef task) override {
        if (task.isValid()) {
            ++schedule_calls;
        }
        return true;
    }

    bool addTimer(Timer::ptr) override { return true; }

    SchedulerType type() override {
        return kIOScheduler;
    }

    int spawn_calls = 0;
    int spawn_immediately_calls = 0;
    int schedule_calls = 0;
};

bool verifyWakerUsesTaskRefSchedule() {
    CaptureScheduler scheduler;
    Coroutine co = pendingTask();
    detail::CoroutineAccess::setScheduler(co, &scheduler);

    Waker waker(detail::CoroutineAccess::taskRef(co));
    waker.wakeUp();

    if (scheduler.schedule_calls != 1) {
        std::cerr << "[T41] expected Waker::wakeUp to call schedule once, got "
                  << scheduler.schedule_calls << "\n";
        return false;
    }
    if (scheduler.spawn_calls != 0) {
        std::cerr << "[T41] expected Waker::wakeUp not to call spawn, got "
                  << scheduler.spawn_calls << "\n";
        return false;
    }
    return true;
}

bool verifyCoroutineResumeUsesTaskRefSchedule() {
    CaptureScheduler scheduler;
    Coroutine co = pendingTask();
    detail::CoroutineAccess::setScheduler(co, &scheduler);

    detail::CoroutineAccess::resume(co);

    if (scheduler.schedule_calls != 1) {
        std::cerr << "[T41] expected Coroutine::resume to call schedule once, got "
                  << scheduler.schedule_calls << "\n";
        return false;
    }
    if (scheduler.spawn_calls != 0) {
        std::cerr << "[T41] expected Coroutine::resume not to call spawn, got "
                  << scheduler.spawn_calls << "\n";
        return false;
    }
    return true;
}

}  // namespace

int main() {
    if (!verifyWakerUsesTaskRefSchedule()) {
        return 1;
    }
    if (!verifyCoroutineResumeUsesTaskRefSchedule()) {
        return 1;
    }

    std::cout << "T41-TaskRefSchedulePath PASS\n";
    return 0;
}
