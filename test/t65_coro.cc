#include <concepts>
#include <utility>

#include "galay-kernel/kernel/compute_scheduler.h"
#include "galay-kernel/kernel/runtime.h"
#include "galay-kernel/kernel/scheduler.hpp"

#ifdef USE_EPOLL
#include "galay-kernel/kernel/epoll_scheduler.h"
#elif defined(USE_KQUEUE)
#include "galay-kernel/kernel/kqueue_scheduler.h"
#elif defined(USE_IOURING)
#include "galay-kernel/kernel/uring_scheduler.h"
#endif

using galay::kernel::ComputeScheduler;
using galay::kernel::Runtime;
using galay::kernel::Scheduler;
using galay::kernel::Task;
using galay::kernel::TaskRef;

template <typename S>
concept HasTaskRefScheduleSurface = std::derived_from<S, Scheduler> &&
    requires(S& scheduler, TaskRef task) {
        { scheduler.schedule(task) } -> std::same_as<bool>;
        { scheduler.scheduleDeferred(task) } -> std::same_as<bool>;
        { scheduler.scheduleImmediately(task) } -> std::same_as<bool>;
    };

static_assert(HasTaskRefScheduleSurface<ComputeScheduler>,
              "Public scheduler headers should expose TaskRef-native scheduling");
static_assert(requires(Runtime runtime, Task<int> task) {
    { runtime.blockOn(std::move(task)) } -> std::same_as<int>;
}, "Runtime should expose Task-native blockOn");

int main()
{
    return 0;
}
