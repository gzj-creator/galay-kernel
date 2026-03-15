/**
 * @file T1-coroutine_chain.cc
 * @brief 用途：验证协程链式 `wait()` 调用在 `Runtime::blockOn` 根任务下的执行顺序。
 * 关键覆盖点：多层协程串联等待、嵌套恢复顺序、根任务阻塞等待到最终完成。
 * 通过条件：调用链按预期顺序执行，`blockOn` 正常返回且进程退出码为 0。
 */

#include "galay-kernel/kernel/Coroutine.h"
#include "galay-kernel/kernel/Runtime.h"
#include <iostream>

using namespace galay::kernel;


Coroutine test2()
{
    std::cout << "test2 wait" << std::endl;
    std::cout << "test2 end" <<std::endl;
    co_return;
}

Coroutine test1()
{
    std::cout << "test1 wait" << std::endl;
    co_await test2().wait();
    std::cout << "test1 end" <<std::endl;
    co_return;
}

Coroutine test()
{
    std::cout << "test wait" << std::endl;
    co_await test1().wait();
    std::cout << "test end" <<std::endl;
    co_return;
}

Task<void> rootTask()
{
    co_await test().wait();
}


int main()
{
    Runtime runtime;
    runtime.blockOn(rootTask());
    return 0;
}
