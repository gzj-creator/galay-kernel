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
        std::cerr << "[T72] unable to read " << source_path << '\n';
        return 1;
    }

    const auto single_section = sliceSection(
        content,
        "void benchSingleProducerThroughput(int64_t message_count)",
        "// 2. 多生产者吞吐量测试");
    if (single_section.empty()) {
        std::cerr << "[T72] unable to isolate single producer section\n";
        return 1;
    }

    const char* required_tokens[] = {
        "CompletionLatch producer_ready_latch",
        "StartGate start_gate",
        "producer_ready_latch.waitFor",
        "start_gate.open()",
    };
    for (const char* token : required_tokens) {
        if (!containsText(single_section, token)) {
            std::cerr << "[T72] single producer section should contain token: "
                      << token << '\n';
            return 1;
        }
    }

    std::cout << "T72-B8SingleProducerGateSourceCase PASS\n";
    return 0;
}
