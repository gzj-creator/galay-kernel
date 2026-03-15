#ifndef GALAY_KERNEL_BLOCKING_EXECUTOR_H
#define GALAY_KERNEL_BLOCKING_EXECUTOR_H

#include <functional>

namespace galay::kernel
{

class BlockingExecutor
{
public:
    BlockingExecutor() = default;
    BlockingExecutor(const BlockingExecutor&) = delete;
    BlockingExecutor& operator=(const BlockingExecutor&) = delete;

    void submit(std::function<void()> task) const;
};

} // namespace galay::kernel

#endif // GALAY_KERNEL_BLOCKING_EXECUTOR_H
