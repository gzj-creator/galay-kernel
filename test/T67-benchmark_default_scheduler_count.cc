#include "benchmark/BenchmarkSync.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::filesystem::path projectRoot() {
    auto path = std::filesystem::path(__FILE__).parent_path().parent_path();
    return path.lexically_normal();
}

bool containsText(const std::filesystem::path& path, const std::string& needle) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    return content.find(needle) != std::string::npos;
}

}  // namespace

int main() {
    if (galay::benchmark::defaultBenchmarkSchedulerCount(0) != 1) {
        std::cerr << "[T67] hardware_threads=0 should clamp to 1\n";
        return 1;
    }

    if (galay::benchmark::defaultBenchmarkSchedulerCount(1) != 1) {
        std::cerr << "[T67] hardware_threads=1 should stay at 1\n";
        return 1;
    }

    if (galay::benchmark::defaultBenchmarkSchedulerCount(2) != 2) {
        std::cerr << "[T67] hardware_threads=2 should allow 2 schedulers\n";
        return 1;
    }

    if (galay::benchmark::defaultBenchmarkSchedulerCount(8) != 2) {
        std::cerr << "[T67] default max parallelism should cap larger hosts at 2\n";
        return 1;
    }

    if (galay::benchmark::defaultBenchmarkSchedulerCount(8, 4) != 4) {
        std::cerr << "[T67] explicit max parallelism override should be honored\n";
        return 1;
    }

    const auto b1_path = projectRoot() / "benchmark" / "B1-compute_scheduler.cc";
    if (!containsText(b1_path, "defaultBenchmarkSchedulerCount")) {
        std::cerr << "[T67] B1 benchmark should use defaultBenchmarkSchedulerCount for stable defaults\n";
        return 1;
    }

    std::cout << "T67-BenchmarkDefaultSchedulerCount PASS\n";
    return 0;
}
