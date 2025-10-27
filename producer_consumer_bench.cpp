#include <oneapi/tbb/task_group.h>
#include <oneapi/tbb/aggregating_task_group.h>
#include <oneapi/tbb/parallel_while.h>
#include <oneapi/tbb/tick_count.h>
#include <oneapi/tbb/global_control.h>
#include <vector>
#include <algorithm>
#include <cmath>

template <typename F, typename Reset>
double measure(F f, Reset reset) {
    std::size_t num_measurements = 11;

    std::vector<double> times(num_measurements);

    for (std::size_t i = 0; i < num_measurements; ++i) {
        tbb::tick_count start = tbb::tick_count::now();
        f();
        tbb::tick_count finish = tbb::tick_count::now();
        reset();

        times[i] = (finish - start).seconds();
    }

    std::sort(times.begin(), times.end());
    return times[num_measurements / 2];
}

constexpr std::size_t work_count = 100000000;

struct benchmark_buffers {
    benchmark_buffers()
        : buf1(new int[work_count])
        , buf2(new int[work_count])
        , buf3(new int[work_count])
    {
        for (std::size_t i = 0; i < work_count; ++i) {
            buf1[i] = i;
            buf2[i] = i;
            buf3[i] = -i;
        }
    }

    ~benchmark_buffers() {
        delete buf1;
        delete buf2;
        delete buf3;
    }

    int* buf1;
    int* buf2;
    int* buf3;
};

struct reset_buffers {
    void operator()() {
        for (std::size_t i = 0; i < work_count; ++i) {
            buffers->buf1[i] = i;
            buffers->buf2[i] = i;
            buffers->buf3[i] = -i;
        }
    }

    benchmark_buffers* buffers;
};

struct do_calc {
    do_calc(benchmark_buffers* b) : buffers(b) {}

    void operator()(int i) const {
        int v1 = buffers->buf1[i];
        int v2 = buffers->buf2[i];
        
        int repeat = std::max(0, v2 % 10);
        int cal = v1;

        for (int l = 0; l < repeat; ++l) {
            cal = static_cast<int>(std::sqrt(float(cal)) + v1);
        }
        buffers->buf3[i] = cal;
    } 

    benchmark_buffers* buffers;
};

template <typename TaskGroupType>
struct benchmark_nonstop_produce {
    benchmark_nonstop_produce(std::size_t sz, benchmark_buffers& benchmark_buffers)
        : block_size(sz)
        , body(&benchmark_buffers)
    {}

    void operator()() const {
        TaskGroupType task_group;

        for (int k = 0; k < work_count; k += block_size) {
            int sub_begin = k;
            int sub_end = k + block_size;

            if (sub_end > work_count) sub_end = work_count;

            task_group.run([=] {
                for (int i = sub_begin; i < sub_end; ++i) {
                    body(i);
                }
            });
        }

        task_group.wait();
    }

    std::size_t block_size;
    do_calc     body;
};

struct benchmark_nonstop_produce_parallel_while {
    benchmark_nonstop_produce_parallel_while(std::size_t sz, benchmark_buffers& benchmark_buffers)
        : block_size(sz)
        , do_calc_body(&benchmark_buffers)
    {}

    void operator()() const {
        int k = 0;
        auto generator = [&] {
            return k++;
        };
        auto predicate = [](int i) {
            return i != work_count;
        };
        auto body = [&](int i) {
            int sub_begin = i;
            int sub_end = i + block_size;

            if (sub_end > work_count) sub_end = work_count;

            for (int j = sub_begin; j < sub_end; ++j) {
                do_calc_body(j);
            }
        };

        oneapi::tbb::parallel_while(generator, predicate, body);
    }

    std::size_t block_size;
    do_calc do_calc_body;
};

int main(int argc, char* argv[]) {
    std::size_t num_threads = argc > 1 ? strtol(argv[1], nullptr, 0) : tbb::this_task_arena::max_concurrency();
    std::size_t block_size  = argc > 2 ? strtol(argv[2], nullptr, 0) : 100; 

    std::cout << "Num Threads: " << num_threads << std::endl;
    std::cout << "Block Size: " << block_size << std::endl;

    tbb::global_control ctl(tbb::global_control::max_allowed_parallelism, num_threads);

    benchmark_buffers buffers;

    benchmark_nonstop_produce<tbb::task_group> tg_body(block_size, buffers);
    double tg_median_time = measure(tg_body, reset_buffers{&buffers});
    std::cout << "Elapsed time (tbb::task_group): " << tg_median_time << std::endl;

    benchmark_nonstop_produce<tbb::aggregating_task_group> atg_body(block_size, buffers);
    double atg_median_time = measure(atg_body, reset_buffers{&buffers});
    std::cout << "Elapsed time (tbb::aggregating_task_group): " << atg_median_time << std::endl;

    benchmark_nonstop_produce_parallel_while pwhile_body(block_size, buffers);
    double pwhile_median_time = measure(pwhile_body, reset_buffers{&buffers});
    std::cout << "Elapsed time (tbb::parallel_while): " << pwhile_median_time << std::endl;
}
