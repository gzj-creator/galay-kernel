#include "galay-kernel/kernel/Runtime.h"
#include <cassert>
#include <iostream>

using namespace galay::kernel;

int main() {
    static_assert(GALAY_RUNTIME_SCHEDULER_COUNT_AUTO == static_cast<size_t>(-1),
                  "Auto scheduler count sentinel must map to size_t(-1)");

    {
        Runtime runtime = RuntimeBuilder()
            .ioSchedulerCount(4)
            .computeSchedulerCount(0)
            .build();

        runtime.start();
        assert(runtime.getIOSchedulerCount() == 4);
        assert(runtime.getComputeSchedulerCount() == 0);
        runtime.stop();
    }

    {
        Runtime runtime = RuntimeBuilder()
            .ioSchedulerCount(4)
            .computeSchedulerCount(GALAY_RUNTIME_SCHEDULER_COUNT_AUTO)
            .build();

        runtime.start();
        assert(runtime.getIOSchedulerCount() == 4);
        assert(runtime.getComputeSchedulerCount() >= 1);
        runtime.stop();
    }

    std::cout << "T42-RuntimeStrictSchedulerCounts PASS\n";
    return 0;
}
