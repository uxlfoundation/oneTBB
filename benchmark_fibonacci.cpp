#define TBB_PREVIEW_TASK_GROUP_EXTENSIONS 1
#include <oneapi/tbb/task_group.h>
#include <oneapi/tbb/tick_count.h>
#include <iostream>

using fibonacci_int_type = std::uint64_t;

fibonacci_int_type serial_fibonacci(std::size_t n) {
    if (n < 2) return fibonacci_int_type(n);
    return serial_fibonacci(n - 1) + serial_fibonacci(n - 2);
}

tbb::task_handle parallel_fibonacci(tbb::task_group& tg, std::size_t n, fibonacci_int_type* placeholder) {
    static constexpr std::size_t serial_cutoff = 10;
    tbb::task_handle bypass_task;

    if (n <= serial_cutoff) {
        *placeholder = serial_fibonacci(n);
    } else {
        fibonacci_int_type* prev_placeholder = new fibonacci_int_type(0);
        fibonacci_int_type* prev_prev_placeholder = new fibonacci_int_type(0);

        tbb::task_handle prev_task = tg.defer([&tg, n, prev_placeholder] {
            return parallel_fibonacci(tg, n - 1, prev_placeholder);
        });

        tbb::task_handle prev_prev_task = tg.defer([&tg, n, prev_prev_placeholder] {
            return parallel_fibonacci(tg, n - 2, prev_prev_placeholder);
        });

        tbb::task_handle merge_task = tg.defer([=] {
            *placeholder = *prev_placeholder + *prev_prev_placeholder;
            delete prev_placeholder;
            delete prev_prev_placeholder;
        });

        tbb::task_group::set_task_order(prev_task, merge_task);
        tbb::task_group::set_task_order(prev_prev_task, merge_task);
        tbb::task_group::transfer_this_task_completion_to(merge_task);

        tg.run(std::move(prev_prev_task));
        tg.run(std::move(merge_task));
        bypass_task = std::move(prev_task);
    }

    return bypass_task;
}

int main() {
    std::size_t n = 40;
    fibonacci_int_type result = 0;

    tbb::tick_count start = tbb::tick_count::now();

    tbb::task_group tg;
    tg.run_and_wait([&tg, n, &result] {
        return parallel_fibonacci(tg, n, &result);
    });

    tbb::tick_count finish = tbb::tick_count::now();

    std::cout << "Nth Fibonacci Number: " << result << std::endl;
    std::cout << "Elapsed time: " << (finish - start).seconds() << std::endl;
}
