#ifndef GALAY_KERNEL_SCHEDULER_CORE_H
#define GALAY_KERNEL_SCHEDULER_CORE_H

#include "IOScheduler.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace galay::kernel {

enum class SchedulerCoreStage {
    CollectRemote,
    CollectCompletions,
    RunReady,
    Poll,
};

struct SchedulerReadyPassSummary {
    size_t ran = 0;
    size_t drainedRemote = 0;
    size_t passes = 0;
};

class SchedulerCore
{
public:
    explicit SchedulerCore(IOSchedulerWorkerState& worker, size_t ready_budget) noexcept
        : m_worker(worker)
        , m_ready_budget(std::max<size_t>(1, ready_budget))
    {
    }

    void setReadyBudget(size_t ready_budget) noexcept {
        m_ready_budget = std::max<size_t>(1, ready_budget);
    }

    size_t readyBudget() const noexcept {
        return m_ready_budget;
    }

    bool hasPendingWork() const noexcept {
        return m_worker.hasLocalWork() || m_worker.hasPendingInjected();
    }

    template <typename OnRemoteCollectedFn>
    size_t collectRemote(OnRemoteCollectedFn&& on_remote_collected_fn) {
        if (!m_worker.hasLocalWork() ||
            m_worker.shouldCheckInjected() ||
            m_worker.hasPendingInjected()) {
            const size_t drained = m_worker.drainInjected();
            on_remote_collected_fn(drained);
            return drained;
        }
        return 0;
    }

    size_t collectRemote() {
        return collectRemote([](size_t) {});
    }

    template <typename ResumeFn, typename OnRemoteCollectedFn>
    SchedulerReadyPassSummary runReadyPassDetailed(ResumeFn&& resume_fn,
                                                   OnRemoteCollectedFn&& on_remote_collected_fn) {
        const bool allow_injected_burst = !m_worker.hasLocalWork();
        size_t burst_credit = 0;
        SchedulerReadyPassSummary summary;
        TaskRef next;
        auto on_remote_collected = [&](size_t drained) {
            summary.drainedRemote += drained;
            on_remote_collected_fn(drained);
        };

        while (true) {
            size_t drained = collectRemote(on_remote_collected);
            if (allow_injected_burst) {
                burst_credit += drained;
            }

            if (!m_worker.popNext(next)) {
                if (m_worker.hasPendingInjected()) {
                    continue;
                }

                drained = m_worker.drainInjected();
                on_remote_collected(drained);
                if (allow_injected_burst) {
                    burst_credit += drained;
                }

                if (drained == 0 || !m_worker.popNext(next)) {
                    break;
                }
            }

            resume_fn(next);
            ++summary.ran;
            if (allow_injected_burst && burst_credit > 0) {
                --burst_credit;
            }
            if (summary.ran >= m_ready_budget && (!allow_injected_burst || burst_credit == 0)) {
                break;
            }
        }

        summary.passes = 1;
        return summary;
    }

    template <typename ResumeFn, typename OnRemoteCollectedFn>
    SchedulerReadyPassSummary runLocalFollowupPasses(size_t max_passes,
                                                     ResumeFn&& resume_fn,
                                                     OnRemoteCollectedFn&& on_remote_collected_fn) {
        SchedulerReadyPassSummary aggregate;
        if (max_passes == 0) {
            return aggregate;
        }

        auto&& resume = resume_fn;
        auto&& on_remote_collected = on_remote_collected_fn;

        for (size_t pass = 0; pass < max_passes; ++pass) {
            auto summary = runReadyPassDetailed(resume, on_remote_collected);
            aggregate.ran += summary.ran;
            aggregate.drainedRemote += summary.drainedRemote;
            aggregate.passes += summary.passes;

            if (summary.ran < m_ready_budget ||
                summary.drainedRemote != 0 ||
                !m_worker.hasLocalWork()) {
                break;
            }
        }

        return aggregate;
    }

    template <typename ResumeFn, typename OnRemoteCollectedFn>
    size_t runReadyPass(ResumeFn&& resume_fn, OnRemoteCollectedFn&& on_remote_collected_fn) {
        return runReadyPassDetailed(
            std::forward<ResumeFn>(resume_fn),
            std::forward<OnRemoteCollectedFn>(on_remote_collected_fn)).ran;
    }

    template <typename ResumeFn>
    size_t runReadyPass(ResumeFn&& resume_fn) {
        return runReadyPass(std::forward<ResumeFn>(resume_fn), [](size_t) {});
    }

    template <typename CollectCompletionsFn,
              typename PollFn,
              typename ResumeFn,
              typename StageObserverFn>
    void runLoopIteration(CollectCompletionsFn&& collect_completions_fn,
                          PollFn&& poll_fn,
                          ResumeFn&& resume_fn,
        StageObserverFn&& stage_observer_fn) {
        stage_observer_fn(SchedulerCoreStage::CollectRemote);
        collectRemote();

        stage_observer_fn(SchedulerCoreStage::CollectCompletions);
        collect_completions_fn();

        stage_observer_fn(SchedulerCoreStage::RunReady);
        runReadyPass(std::forward<ResumeFn>(resume_fn));

        if (!hasPendingWork()) {
            stage_observer_fn(SchedulerCoreStage::Poll);
            poll_fn();
        }
    }

private:
    IOSchedulerWorkerState& m_worker;
    size_t m_ready_budget;
};

}  // namespace galay::kernel

#endif  // GALAY_KERNEL_SCHEDULER_CORE_H
