void Foo(float) {}

#include "parallel_for_lambda_example_2.h"

int main() {
    constexpr std::size_t size = 10;
    float array[size] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    ParallelApplyFoo(array, size);
}
