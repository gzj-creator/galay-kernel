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
    const auto source_path = projectRoot() / "benchmark" / "B1-compute_scheduler.cc";
    const auto content = readText(source_path);
    if (content.empty()) {
        std::cerr << "[T70] unable to read " << source_path << '\n';
        return 1;
    }

    if (!containsText(content, "ThroughputSample measureThroughputSample(")) {
        std::cerr << "[T70] B1 should extract per-sample throughput measurement into a helper\n";
        return 1;
    }

    if (!containsText(content, "measureThroughputSample(")) {
        std::cerr << "[T70] benchThroughput should use measureThroughputSample\n";
        return 1;
    }

    std::cout << "T70-B1ThroughputSampleSourceCase PASS\n";
    return 0;
}
