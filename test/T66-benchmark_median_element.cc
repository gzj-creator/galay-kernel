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
