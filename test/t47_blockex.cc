/**
 * @file t47_blockex.cc
 * @brief 用途：验证 `Runtime::blockOn` 会向调用方透传根任务抛出的异常。
 * 关键覆盖点：根任务异常抛出、调用侧捕获、异常信息或类型保持。
 * 通过条件：预期异常被成功捕获，测试返回 0。
 */

#include "galay-kernel/kernel/runtime.h"
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

    std::cout << "T47-RuntimeBlockOnException PASS\n";
    return 0;
}
