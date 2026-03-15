#include "galay-kernel/kernel/SchedulerCore.h"

#include <iostream>
#include <vector>

using namespace galay::kernel;

namespace {

const char* stageName(SchedulerCoreStage stage) {
    switch (stage) {
    case SchedulerCoreStage::CollectRemote:
        return "CollectRemote";
    case SchedulerCoreStage::CollectCompletions:
        return "CollectCompletions";
    case SchedulerCoreStage::RunReady:
        return "RunReady";
    case SchedulerCoreStage::Poll:
        return "Poll";
    }
    return "Unknown";
}

}  // namespace

int main() {
    IOSchedulerWorkerState worker(8);
    SchedulerCore core(worker, 8);

    std::vector<SchedulerCoreStage> stages;
    size_t resumed = 0;
    bool polled = false;
    bool completions_collected = false;

    core.runLoopIteration(
        [&]() { completions_collected = true; },
        [&]() {
            polled = true;
        },
        [&](TaskRef&) { ++resumed; },
        [&](SchedulerCoreStage stage) { stages.push_back(stage); });

    const std::vector<SchedulerCoreStage> expected = {
        SchedulerCoreStage::CollectRemote,
        SchedulerCoreStage::CollectCompletions,
        SchedulerCoreStage::RunReady,
        SchedulerCoreStage::Poll,
    };

    if (stages != expected) {
        std::cerr << "[T48] unexpected stage order:";
        for (SchedulerCoreStage stage : stages) {
            std::cerr << ' ' << stageName(stage);
        }
        std::cerr << "\n";
        return 1;
    }

    if (!polled) {
        std::cerr << "[T48] expected poll stage to execute\n";
        return 1;
    }

    if (!completions_collected) {
        std::cerr << "[T48] expected completion collection stage to execute\n";
        return 1;
    }

    if (resumed != 0) {
        std::cerr << "[T48] expected no ready task to resume, got " << resumed << "\n";
        return 1;
    }

    std::cout << "T48-SchedulerCoreLoopStages PASS\n";
    return 0;
}
