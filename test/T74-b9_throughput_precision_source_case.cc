/**
 * @file T74-b9_throughput_precision_source_case.cc
 * @brief 用途：验证 B9 吞吐量统计使用纳秒级时间精度计算结果。
 * 关键覆盖点：纳秒级耗时统计、吞吐公式精度、毫秒展示换算。
 * 通过条件：源码中高精度时间计算 token 全部存在，测试返回 0。
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
    const auto source_path = projectRoot() / "benchmark" / "B9-unsafe_channel.cc";
    const auto content = readText(source_path);
    if (content.empty()) {
        std::cerr << "[T74] unable to read " << source_path << '\n';
        return 1;
    }

    const char* required_tokens[] = {
        "duration_cast<std::chrono::nanoseconds>(elapsed).count()",
        "static_cast<double>(message_count) * 1'000'000'000.0 / elapsed_ns",
        "static_cast<double>(elapsed_ns) / 1'000'000.0",
    };
    for (const char* token : required_tokens) {
        if (!containsText(content, token)) {
            std::cerr << "[T74] B9 throughput paths should use high-precision elapsed_ns token: "
                      << token << '\n';
            return 1;
        }
    }

    std::cout << "T74-B9ThroughputPrecisionSourceCase PASS\n";
    return 0;
}
