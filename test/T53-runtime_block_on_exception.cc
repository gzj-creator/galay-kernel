#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/Runtime.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace galay::kernel;

Task<int> explodeTask()
{
    throw std::runtime_error("boom");
    co_return 0;
}

int main()
{
    Runtime runtime = RuntimeBuilder()
        .ioSchedulerCount(0)
        .computeSchedulerCount(1)
        .build();

    try {
        (void)runtime.blockOn(explodeTask());
        assert(false && "Runtime::blockOn should rethrow task exceptions");
    } catch (const std::runtime_error& ex) {
        assert(std::string(ex.what()) == "boom");
    }

    std::cout << "T53-RuntimeBlockOnException PASS\n";
    return 0;
}
