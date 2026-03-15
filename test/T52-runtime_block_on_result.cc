#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/Runtime.h"
#include <cassert>
#include <iostream>

using namespace galay::kernel;

Task<int> answerTask()
{
    co_return 42;
}

int main()
{
    Runtime runtime = RuntimeBuilder()
        .ioSchedulerCount(0)
        .computeSchedulerCount(1)
        .build();

    int value = runtime.blockOn(answerTask());
    assert(value == 42);

    std::cout << "T52-RuntimeBlockOnResult PASS\n";
    return 0;
}
