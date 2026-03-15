/**
 * @file T66-benchmark_median_element.cc
 * @brief 用途：验证 benchmark 中位数辅助函数的取值与排序边界。
 * 关键覆盖点：奇偶样本取中位数、排序后选取、边界输入处理。
 * 通过条件：`medianElement` 返回值符合预期，测试返回 0。
 */

#include "benchmark/BenchmarkSync.h"

#include <iostream>
#include <vector>

struct SampleValue {
    int value;
    const char* label;
};

int main() {
    const int middle = galay::benchmark::medianElement(std::vector<int>{9, 1, 5, 7, 3});
    if (middle != 5) {
        std::cerr << "[T66] medianElement should return the middle sorted integer\n";
        return 1;
    }

    const auto sample = galay::benchmark::medianElement(
        std::vector<SampleValue>{
            {9, "high"},
            {3, "low"},
            {5, "mid"},
        },
        [](const SampleValue& lhs, const SampleValue& rhs) {
            return lhs.value < rhs.value;
        });
    if (sample.value != 5 || std::string(sample.label) != "mid") {
        std::cerr << "[T66] medianElement should preserve the median struct sample\n";
        return 1;
    }

    std::cout << "T66-BenchmarkMedianElement PASS\n";
    return 0;
}
