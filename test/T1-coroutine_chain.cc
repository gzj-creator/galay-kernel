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
