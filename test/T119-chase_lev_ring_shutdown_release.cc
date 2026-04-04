/**
 * @file T119-chase_lev_ring_shutdown_release.cc
 * @brief 验证 Chase-Lev ring 析构时会释放仍排队的任务引用。
 */

#include "galay-kernel/kernel/IOScheduler.hpp"
#include "galay-kernel/kernel/Task.h"

#include <iostream>

using namespace galay::kernel;

namespace {

Task<void> emptyTask() {
    co_return;
}

bool runScenario() {
    TaskRef queued = detail::TaskAccess::detachTask(emptyTask());
    TaskRef keeper = queued;
    auto* state = keeper.state();
    if (state == nullptr) {
        std::cerr << "[T119] keeper lost task state before enqueue\n";
        return false;
    }
    const uint32_t refs_before_destroy = state->m_refs.load(std::memory_order_acquire);

    {
        ChaseLevTaskRing ring;
        if (!ring.push_back(std::move(queued))) {
            std::cerr << "[T119] failed to enqueue task into ring\n";
            return false;
        }
    }

    const uint32_t refs_after_destroy = state->m_refs.load(std::memory_order_acquire);
    if (refs_after_destroy + 1 != refs_before_destroy) {
        std::cerr << "[T119] ring destruction should release exactly one queued task ref, before="
                  << refs_before_destroy << ", after=" << refs_after_destroy << "\n";
        return false;
    }

    return true;
}

}  // namespace

int main() {
    if (!runScenario()) {
        return 1;
    }

    std::cout << "T119-ChaseLevRingShutdownRelease PASS\n";
    return 0;
}
