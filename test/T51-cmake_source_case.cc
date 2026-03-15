/**
 * @file T51-cmake_source_case.cc
 * @brief 用途：验证 `benchmark/CMakeLists.txt` 中源文件大小写与真实文件一致。
 * 关键覆盖点：CMake `add_executable` 源文件名扫描、大小写敏感匹配、缺失项报告。
 * 通过条件：不存在大小写不一致的源文件引用，测试返回 0。
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

std::filesystem::path projectRoot() {
    auto path = std::filesystem::path(__FILE__).parent_path().parent_path();
    return path.lexically_normal();
}

std::vector<std::string> findMissingExactMatches(const std::filesystem::path& cmake_path) {
    std::ifstream input(cmake_path);
    if (!input.is_open()) {
        return {"cannot open " + cmake_path.string()};
    }

    const auto source_dir = cmake_path.parent_path();
    std::unordered_set<std::string> exact_names;
    for (const auto& entry : std::filesystem::directory_iterator(source_dir)) {
        exact_names.insert(entry.path().filename().string());
    }

    std::vector<std::string> missing;
    const std::regex add_executable_pattern(
        R"(add_executable\(\s*([^\s\)]+)\s+([^\s\)]+))");

    std::string line;
    while (std::getline(input, line)) {
        std::smatch match;
        if (!std::regex_search(line, match, add_executable_pattern)) {
            continue;
        }

        const auto source_name = match[2].str();
        if (!source_name.ends_with(".cc")) {
            continue;
        }

        if (!exact_names.contains(source_name)) {
            missing.push_back(source_name);
        }
    }

    return missing;
}

}  // namespace

int main() {
    const auto root = projectRoot();
    const auto cmake_path = root / "benchmark" / "CMakeLists.txt";

    const auto missing = findMissingExactMatches(cmake_path);
    if (!missing.empty()) {
        std::cerr << "benchmark/CMakeLists.txt has case-sensitive source mismatches:\n";
        for (const auto& source_name : missing) {
            std::cerr << "  - " << source_name << '\n';
        }
        return 1;
    }

    std::cout << "T51-cmake_source_case PASS\n";
    return 0;
}
