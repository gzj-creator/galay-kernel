/**
 * @file T80-scheduler_taskref_surface.cc
 * @brief 用途：锁定 `Scheduler` 公开提交面只暴露 `TaskRef` 风格接口。
 * 关键覆盖点：`schedule(TaskRef)`、`scheduleDeferred(TaskRef)`、`scheduleImmediately(TaskRef)` 存在，旧 `spawn(Coroutine)` 风格接口被移除。
 * 通过条件：编译期静态断言全部通过，测试返回 0。
 */

#include "galay-kernel/kernel/Scheduler.hpp"
#include "galay-kernel/kernel/Task.h"

#include <concepts>
#include <type_traits>
#include <utility>

using namespace galay::kernel;

template <typename T>
concept HasSchedule = requires(T scheduler, TaskRef task) {
    { scheduler.schedule(std::move(task)) } -> std::same_as<bool>;
};

template <typename T>
concept HasScheduleDeferred = requires(T scheduler, TaskRef task) {
    { scheduler.scheduleDeferred(std::move(task)) } -> std::same_as<bool>;
};

template <typename T>
concept HasScheduleImmediately = requires(T scheduler, TaskRef task) {
    { scheduler.scheduleImmediately(std::move(task)) } -> std::same_as<bool>;
};

template <typename T>
concept HasSpawn = requires(T scheduler, Coroutine co) {
    { scheduler.spawn(std::move(co)) } -> std::same_as<bool>;
};

template <typename T>
concept HasSpawnImmediately = requires(T scheduler, Coroutine co) {
    { scheduler.spawnImmidiately(std::move(co)) } -> std::same_as<bool>;
};

static_assert(HasSchedule<Scheduler>);
static_assert(HasScheduleDeferred<Scheduler>);
static_assert(HasScheduleImmediately<Scheduler>);
static_assert(!HasSpawn<Scheduler>);
static_assert(!HasSpawnImmediately<Scheduler>);

int main()
{
    return 0;
}
