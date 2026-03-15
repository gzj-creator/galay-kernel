#include "BlockingExecutor.h"
#include <thread>
#include <utility>

namespace galay::kernel
{

void BlockingExecutor::submit(std::function<void()> task) const
{
    std::thread([task = std::move(task)]() mutable {
        task();
    }).detach();
}

} // namespace galay::kernel
