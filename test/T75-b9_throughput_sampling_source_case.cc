/**
 * @file T75-b9_throughput_sampling_source_case.cc
 * @brief 用途：验证 B9 吞吐量测试保留多样本采样与中位数汇总逻辑。
 * 关键覆盖点：样本数量控制、最小时长门限、辅助函数与中位数统计。
 * 通过条件：源码中采样与汇总 token 全部存在，测试返回 0。
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
        std::cerr << "[T75] unable to read " << source_path << '\n';
        return 1;
    }

    const char* required_tokens[] = {
        "THROUGHPUT_SAMPLE_COUNT",
        "THROUGHPUT_MIN_SAMPLE_DURATION",
        "measureThroughputSample(",
        "sample_message_count *= 2",
        "medianElement(",
    };
    for (const char* token : required_tokens) {
        if (!containsText(content, token)) {
            std::cerr << "[T75] B9 throughput benchmarking should contain token: "
                      << token << '\n';
            return 1;
        }
    }

    std::cout << "T75-B9ThroughputSamplingSourceCase PASS\n";
    return 0;
}
