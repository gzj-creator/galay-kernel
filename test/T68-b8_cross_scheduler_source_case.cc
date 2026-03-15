/**
 * @file T68-b8_cross_scheduler_source_case.cc
 * @brief 用途：验证 B8 跨运行时场景采用新的同步采样实现而非忙等逻辑。
 * 关键覆盖点：源码中存在 `CompletionLatch`、`StartGate`、中位数采样且无 busy-wait token。
 * 通过条件：源码扫描命中预期标记且未发现禁用模式，测试返回 0。
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::filesystem::path projectRoot() {
    auto path = std::filesystem::path(__FILE__).parent_path().parent_path();
    return path.lexically_normal();
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

bool containsText(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

int main() {
    const auto source_path = projectRoot() / "benchmark" / "B8-mpsc_channel.cc";
    const auto content = readText(source_path);
    if (content.empty()) {
        std::cerr << "[T68] unable to read " << source_path << '\n';
        return 1;
    }

    const std::string begin_marker = "void benchCrossScheduler(int64_t message_count)";
    const std::string end_marker = "// 7. 持续压力测试";
    const auto begin = content.find(begin_marker);
    const auto end = content.find(end_marker, begin);
    if (begin == std::string::npos || end == std::string::npos || end <= begin) {
        std::cerr << "[T68] unable to isolate benchCrossScheduler source section\n";
        return 1;
    }

    const auto cross_section = content.substr(begin, end - begin);

    const char* required_tokens[] = {
        "CROSS_SCHEDULER_SAMPLE_COUNT",
        "CompletionLatch producer_ready_latch",
        "CompletionLatch producer_done_latch",
        "StartGate start_gate",
        "medianElement(",
    };
    for (const char* token : required_tokens) {
        if (!containsText(cross_section, token)) {
            std::cerr << "[T68] benchCrossScheduler should contain token: " << token << '\n';
            return 1;
        }
    }

    const char* forbidden_tokens[] = {
        "while (!g_consumer_done)",
        "while (!g_producer_done)",
        "sleep_for(1ms)",
    };
    for (const char* token : forbidden_tokens) {
        if (containsText(cross_section, token)) {
            std::cerr << "[T68] benchCrossScheduler should not busy-wait with token: "
                      << token << '\n';
            return 1;
        }
    }

    std::cout << "T68-B8CrossSchedulerSourceCase PASS\n";
    return 0;
}
