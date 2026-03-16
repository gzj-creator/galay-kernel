/**
 * @file T78-task_await_surface.cc
 * @brief 用途：锁定 `Task<T>` 作为公开协程返回类型时可被直接 `co_await` 的接口形态。
 * 关键覆盖点：公开 `Task.h` 入口、`Task<int>` 直接 await、`Runtime::blockOn` 返回 await 后结果。
 * 通过条件：测试可编译运行，`co_await childTask()` 返回值正确且进程退出码为 0。
 */

#include "galay-kernel/kernel/Runtime.h"
#include "galay-kernel/kernel/Task.h"

#include <cassert>

using namespace galay::kernel;

Task<int> childTask()
{
    co_return 7;
}

Task<int> parentTask()
{
    int value = co_await childTask();
    co_return value + 1;
}

int main()
{
    Runtime runtime;
    assert(runtime.blockOn(parentTask()) == 8);
    return 0;
}
