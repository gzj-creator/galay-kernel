#include "Scheduler.hpp"

#include <cstddef>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace galay::kernel
{

bool Scheduler::setAffinity(std::optional<uint32_t> cpu_id)
{
    if (!cpu_id.has_value()) {
        m_affinity_cpu.store(kNoAffinity, std::memory_order_release);
        return true;
    }

#if !defined(__linux__)
    (void)cpu_id;
    return false;
#else
    const uint32_t cpu_count = std::thread::hardware_concurrency();
    if (cpu_count > 0 && *cpu_id >= cpu_count) {
        return false;
    }
    m_affinity_cpu.store(static_cast<int32_t>(*cpu_id), std::memory_order_release);
    return true;
#endif
}

bool Scheduler::applyConfiguredAffinity()
{
    const int32_t cpu_id = m_affinity_cpu.load(std::memory_order_acquire);
    if (cpu_id < 0) {
        return true; // 默认不绑核
    }

#if defined(__linux__)
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(static_cast<size_t>(cpu_id), &cpu_set);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set) == 0;
#else
    return false;
#endif
}

} // namespace galay::kernel
