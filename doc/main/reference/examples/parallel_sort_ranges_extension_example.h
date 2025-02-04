#include <array>
#include <span> // requires C++20
#include <oneapi/tbb/parallel_sort.h>

std::span<int> get_span() {
    static std::array<int, 3> arr = {3, 2, 1};
    return std::span<int>(arr);
}

int main() {
    tbb::parallel_sort(get_span());
}