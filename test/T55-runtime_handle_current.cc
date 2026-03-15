#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/Runtime.h"
#include <cassert>
#include <iostream>

using namespace galay::kernel;

Task<bool> checkCurrentHandle()
{
    auto current = RuntimeHandle::tryCurrent();
    co_return current.has_value();
}

int main()
{
    assert(!RuntimeHandle::tryCurrent().has_value());

    Runtime runtime = RuntimeBuilder()
        .ioSchedulerCount(0)
        .computeSchedulerCount(1)
        .build();

    bool inside_runtime = runtime.blockOn(checkCurrentHandle());
    assert(inside_runtime);

    std::cout << "T55-RuntimeHandleCurrent PASS\n";
    return 0;
}
