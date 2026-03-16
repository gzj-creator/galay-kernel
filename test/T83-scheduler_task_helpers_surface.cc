#include "galay-kernel/kernel/ComputeScheduler.h"
#include "galay-kernel/kernel/Task.h"

#include <concepts>
#include <type_traits>

#if defined(USE_KQUEUE)
#include "galay-kernel/kernel/KqueueScheduler.h"
using IOSchedulerType = galay::kernel::KqueueScheduler;
#elif defined(USE_EPOLL)
#include "galay-kernel/kernel/EpollScheduler.h"
using IOSchedulerType = galay::kernel::EpollScheduler;
#elif defined(USE_IOURING)
#include "galay-kernel/kernel/IOUringScheduler.h"
using IOSchedulerType = galay::kernel::IOUringScheduler;
#endif

using namespace galay::kernel;

namespace {

Task<void> noopTask() {
    co_return;
}

template <typename SchedulerT>
concept HasTaskScheduleHelpers = requires(SchedulerT& scheduler) {
    { scheduleTask(scheduler, noopTask()) } -> std::same_as<bool>;
    { scheduleTaskDeferred(scheduler, noopTask()) } -> std::same_as<bool>;
    { scheduleTaskImmediately(scheduler, noopTask()) } -> std::same_as<bool>;
};

static_assert(HasTaskScheduleHelpers<ComputeScheduler>,
              "ComputeScheduler should accept Task helpers without exposing detail::TaskAccess");

#if defined(USE_KQUEUE) || defined(USE_EPOLL) || defined(USE_IOURING)
static_assert(HasTaskScheduleHelpers<IOSchedulerType>,
              "IOScheduler should accept Task helpers without exposing detail::TaskAccess");
#endif

}  // namespace

int main() {
    return 0;
}
