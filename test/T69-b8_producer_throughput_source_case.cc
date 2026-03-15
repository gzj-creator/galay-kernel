/**
 * @file T69-b8_producer_throughput_source_case.cc
 * @brief 用途：验证 B8 生产者吞吐量场景保留采样与中位数统计实现。
 * 关键覆盖点：单生产者与多生产者分段、样本收集、`medianElement` 汇总。
 * 通过条件：源码中采样相关 token 全部存在，测试返回 0。
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

std::string sliceSection(const std::string& content,
                         const std::string& begin_marker,
                         const std::string& end_marker) {
    const auto begin = content.find(begin_marker);
    const auto end = content.find(end_marker, begin);
    if (begin == std::string::npos || end == std::string::npos || end <= begin) {
        return {};
    }
    return content.substr(begin, end - begin);
}

bool containsText(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

int main() {
    const auto source_path = projectRoot() / "benchmark" / "B8-mpsc_channel.cc";
    const auto content = readText(source_path);
    if (content.empty()) {
        std::cerr << "[T69] unable to read " << source_path << '\n';
        return 1;
    }

    const auto single_section = sliceSection(
        content,
        "void benchSingleProducerThroughput(int64_t message_count)",
        "// 2. 多生产者吞吐量测试");
    if (single_section.empty()) {
        std::cerr << "[T69] unable to isolate benchSingleProducerThroughput section\n";
        return 1;
    }

    const auto multi_section = sliceSection(
        content,
        "void benchMultiProducerThroughput(int producer_count, int64_t total_messages)",
        "// 3. 批量接收吞吐量测试");
    if (multi_section.empty()) {
        std::cerr << "[T69] unable to isolate benchMultiProducerThroughput section\n";
        return 1;
    }

    const char* required_tokens[] = {
        "PRODUCER_THROUGHPUT_SAMPLE_COUNT",
        "medianElement(",
        "std::vector<ThroughputSample> samples",
    };

    for (const char* token : required_tokens) {
        if (!containsText(single_section, token)) {
            std::cerr << "[T69] single producer section should contain token: "
                      << token << '\n';
            return 1;
        }
        if (!containsText(multi_section, token)) {
            std::cerr << "[T69] multi producer section should contain token: "
                      << token << '\n';
            return 1;
        }
    }

    std::cout << "T69-B8ProducerThroughputSourceCase PASS\n";
    return 0;
}
