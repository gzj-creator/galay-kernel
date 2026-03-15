#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/Waker.h"
#include "galay-kernel/kernel/Scheduler.hpp"

#include <deque>
#include <iostream>

using namespace galay::kernel;

namespace {

struct ChildSuspendState {
    Waker waker;
    bool armed = false;
    bool child_done = false;
};

struct ParentState {
    bool parent_done = false;
    int parent_resumes = 0;
};

class ManualScheduler final : public Scheduler {
public:
    void start() override {}
    void stop() override {}

    bool spawn(Coroutine co) override {
        auto* scheduler = detail::CoroutineAccess::belongScheduler(co);
        if (!scheduler) {
            detail::CoroutineAccess::setScheduler(co, this);
        } else if (scheduler != this) {
            return false;
        }
        m_ready.push_back(detail::CoroutineAccess::detachTask(std::move(co)));
        return true;
    }

    bool spawnImmidiately(Coroutine co) override {
        auto* scheduler = detail::CoroutineAccess::belongScheduler(co);
        if (scheduler) {
            return false;
        }
        detail::CoroutineAccess::setScheduler(co, this);
        TaskRef task = detail::CoroutineAccess::detachTask(std::move(co));
        resume(task);
        return true;
    }

    bool schedule(TaskRef task) override {
        if (!task.isValid()) {
            return false;
        }
        ++schedule_calls;
        m_ready.push_back(std::move(task));
        return true;
    }

    bool addTimer(Timer::ptr) override { return true; }

    SchedulerType type() override {
        return kComputeScheduler;
    }

    bool runOne() {
        if (m_ready.empty()) {
            return false;
        }
        TaskRef task = std::move(m_ready.front());
        m_ready.pop_front();
        resume(task);
        return true;
    }

    int schedule_calls = 0;

private:
    std::deque<TaskRef> m_ready;
};

struct ChildSuspendAwaitable {
    ChildSuspendState* state;

    bool await_ready() const noexcept { return false; }

    bool await_suspend(std::coroutine_handle<Coroutine::promise_type> handle) noexcept {
        state->waker = Waker(handle);
        state->armed = true;
        return true;
    }

    void await_resume() const noexcept {}
};

Coroutine childTask(ChildSuspendState* state) {
    co_await ChildSuspendAwaitable{state};
    state->child_done = true;
    co_return;
}

Coroutine parentTask(ChildSuspendState* child_state, ParentState* parent_state) {
    Coroutine child = childTask(child_state);
    co_await child.wait();
    ++parent_state->parent_resumes;
    parent_state->parent_done = true;
    co_return;
}

bool verifyWaitContinuationReturnsThroughScheduler() {
    ManualScheduler scheduler;
    ChildSuspendState child_state;
    ParentState parent_state;

    if (!scheduler.spawnImmidiately(parentTask(&child_state, &parent_state))) {
        std::cerr << "[T43] failed to start parent task\n";
        return false;
    }

    if (!child_state.armed) {
        std::cerr << "[T43] child did not suspend as expected\n";
        return false;
    }

    child_state.waker.wakeUp();
    if (scheduler.schedule_calls != 1) {
        std::cerr << "[T43] expected child wake to schedule once, got "
                  << scheduler.schedule_calls << "\n";
        return false;
    }

    if (!scheduler.runOne()) {
        std::cerr << "[T43] expected child task in ready queue\n";
        return false;
    }

    if (!child_state.child_done) {
        std::cerr << "[T43] child did not complete after resume\n";
        return false;
    }

    if (scheduler.schedule_calls != 2) {
        std::cerr << "[T43] expected waiter continuation to be re-scheduled, got "
                  << scheduler.schedule_calls << " schedule calls\n";
        return false;
    }

    if (parent_state.parent_done) {
        std::cerr << "[T43] waiter resumed inline instead of being queued\n";
        return false;
    }

    if (!scheduler.runOne()) {
        std::cerr << "[T43] expected waiter continuation in ready queue\n";
        return false;
    }

    if (!parent_state.parent_done || parent_state.parent_resumes != 1) {
        std::cerr << "[T43] waiter did not resume exactly once\n";
        return false;
    }

    return true;
}

}  // namespace

int main() {
    if (!verifyWaitContinuationReturnsThroughScheduler()) {
        return 1;
    }

    std::cout << "T43-WaitContinuationSchedulerPath PASS\n";
    return 0;
}
