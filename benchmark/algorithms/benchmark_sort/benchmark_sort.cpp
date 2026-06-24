#include <oneapi/tbb/parallel_sort.h>

#if TEST_ONEDPL
#include <oneapi/dpl/algorithm>
#include <oneapi/dpl/execution>
#endif

#if TEST_TASKFLOW
#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/sort.hpp>
#endif

#include <benchmark/benchmark.h>

#include <algorithm>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cstdint>
#include <random>
#include <limits>

template <typename DataType>
struct number_traits {
    using data_type = DataType;
    using compare_type = std::less<data_type>;

    static data_type make_value(int raw_number) {
        return data_type(raw_number);
    }
};

struct uint32_t_traits : number_traits<std::uint32_t> {
    static const char* name() { return "uint32_t"; }
};

struct uint64_t_traits : number_traits<std::uint64_t> {
    static const char* name() { return "uint64_t"; }
};

struct double_traits : number_traits<double> {
    static const char* name() { return "double"; }
};

struct string_traits {
    using data_type = std::string;
    using compare_type = std::less<data_type>;

    static data_type make_value(int raw_number) {
        // strings with lower raw_number should be considered less then strings with higher raw_number
        // Adding 0 padding before each number to align the strings length and preserve the ordering:
        static constexpr std::size_t min_width = std::numeric_limits<int>::digits10 + 1;
        static constexpr std::size_t key_width = 100;
        static_assert(key_width >= min_width, "key_width must fit the widest raw_number to preserve ordering");
        std::string s = std::to_string(raw_number);
        return std::string(key_width - s.size(), '0') + s;
    }

    static const char* name() { return "string"; }
};

struct kv_traits {
    struct kv {
        std::uint32_t key;
        std::uint32_t payload;
    };

    struct key_compare {
        bool operator()(const kv& lhs, const kv& rhs) const {
            return lhs.key < rhs.key;
        }
    };

    using data_type = kv;
    using compare_type = key_compare;

    static data_type make_value(int raw_number) {
        return kv{std::uint32_t(raw_number), std::uint32_t(raw_number ^ 0x9e3779b9u)};
    }

    static const char* name() { return "kv"; }
};

struct uniform_distribution {
    template <typename TypeTraits, typename Iterator>
    static void fill(Iterator begin, Iterator end) {
        std::mt19937 rng(0);
        std::uniform_int_distribution<int> dist(0, std::numeric_limits<int>::max());

        std::generate(begin, end, [&] { return TypeTraits::make_value(dist(rng)); });
    }

    static const char* name() { return "uniform_distribution"; }
};

struct all_equal_distribution {
    template <typename TypeTraits, typename Iterator>
    static void fill(Iterator begin, Iterator end) {
        std::fill(begin, end, TypeTraits::make_value(0));
    }

    static const char* name() { return "all_equal_distribution"; }
};

struct uniq8_distribution {
    template <typename TypeTraits, typename Iterator>
    static void fill(Iterator begin, Iterator end) {
        std::mt19937 rng(0);
        std::uniform_int_distribution<int> dist(0, 7);

        std::generate(begin, end, [&] { return TypeTraits::make_value(dist(rng)); });
    }

    static const char* name() { return "uniq8_distribution"; }
};

struct sorted_distribution {
    template <typename TypeTraits, typename Iterator>
    static void fill(Iterator begin, Iterator end) {
        int value = 0;

        for (Iterator it = begin; it != end; ++it) {
            *it = TypeTraits::make_value(value);
            ++value;
        }
    }

    static const char* name() { return "sorted_distribution"; }
};

struct reverse_sorted_distribution {
    template <typename TypeTraits, typename Iterator>
    static void fill(Iterator begin, Iterator end) {
        int value = std::distance(begin, end);

        for (Iterator it = begin; it != end; ++it) {
            *it = TypeTraits::make_value(value);
            --value;
        }
    }

    static const char* name() { return "reverse_sorted_distribution"; }
};

struct near_sorted_1p_distribution {
    template <typename TypeTraits, typename Iterator>
    static void fill(Iterator begin, Iterator end) {
        // Generate fully sorted
        sorted_distribution::fill<TypeTraits>(begin, end);

        // Perform 1% random swaps
        std::size_t n = std::distance(begin, end);

        std::mt19937 rng(0);
        std::uniform_int_distribution<std::size_t> dist(0, n - 1);

        std::size_t swaps = n / 100;
        if (swaps == 0) swaps = 1;

        while (swaps != 0) {
            std::size_t lhs = dist(rng);
            std::size_t rhs = dist(rng);

            if (lhs != rhs) {
                std::iter_swap(begin + lhs, begin + rhs);
                --swaps;
            }
        }
    }

    static const char* name() { return "near_sorted_1p_distribution"; }
};

struct std_sorter {
    template <typename Iterator, typename Compare>
    static void sort(Iterator begin, Iterator end, Compare compare) {
        std::sort(begin, end, compare);
    }

    static const char* name() { return "std_sort"; }
};

struct tbb_sorter {
    template <typename Iterator, typename Compare>
    static void sort(Iterator begin, Iterator end, Compare compare) {
        oneapi::tbb::parallel_sort(begin, end, compare);   
    }

    static const char* name() { return "tbb_sort"; }
};

#if TEST_DPL
struct dpl_sorter {
    template <typename Iterator, typename Compare>
    static void sort(Iterator begin, Iterator end, Compare compare) {
        oneapi::dpl::sort(oneapi::dpl::execution::par, begin, end, compare);   
    }

    static const char* name() { return "dpl_sort"; }
};
#endif

#if TEST_TASKFLOW
struct taskflow_sorter {
    static tf::Executor executor;

    template <typename Iterator, typename Compare>
    static void sort(Iterator begin, Iterator end, Compare compare) {
        tf::Taskflow taskflow;
        taskflow.sort(begin, end, compare);
        executor.run(taskflow).get();   
    }

    static const char* name() { return "taskflow_sort"; }
}
#endif

template <typename TypeTraits, typename Distribution, typename Sorter>
void benchmark_parallel_sort(benchmark::State& state, std::size_t problem_size) {
    using data_type = typename TypeTraits::data_type;

    std::vector<data_type> base(problem_size);

    Distribution::template fill<TypeTraits>(base.begin(), base.end());

    typename TypeTraits::compare_type compare;

    for (auto _ : state) {
        state.PauseTiming();
        std::vector<data_type> data = base;
        state.ResumeTiming();

        Sorter::sort(data.begin(), data.end(), compare);

        benchmark::DoNotOptimize(data.data());
        benchmark::ClobberMemory();
    }
}

template <typename TypeTraits, typename Distribution, typename Sorter>
void register_parallel_sort_benchmark(std::size_t problem_size) {
    std::string benchmark_name = std::string(TypeTraits::name()) + ","
                                 + Distribution::name() + ","
                                 + Sorter::name() + ","
                                 + std::to_string(problem_size);
    benchmark::RegisterBenchmark(benchmark_name,
                                 benchmark_parallel_sort<TypeTraits, Distribution, Sorter>, problem_size);
}

template <typename TypeTraits, typename Distribution>
void register_parallel_sort_benchmark_with_distribution(std::size_t problem_size) {
    register_parallel_sort_benchmark<TypeTraits, Distribution, std_sorter>(problem_size);
    register_parallel_sort_benchmark<TypeTraits, Distribution, tbb_sorter>(problem_size);
#if TEST_ONEDPL
    register_parallel_sort_benchmark<TypeTraits, Distribution, std_sorter>(problem_size);
#endif
#if TEST_TASKFLOW
    register_parallel_sort_benchmark<TypeTraits, Distribution, std_sorter>(problem_size);
#endif
}

template <typename TypeTraits>
void register_parallel_sort_benchmark_with_type(std::size_t problem_size) {
    register_parallel_sort_benchmark_with_distribution<TypeTraits, uniform_distribution>(problem_size);
    register_parallel_sort_benchmark_with_distribution<TypeTraits, all_equal_distribution>(problem_size);
    register_parallel_sort_benchmark_with_distribution<TypeTraits, uniq8_distribution>(problem_size);
    register_parallel_sort_benchmark_with_distribution<TypeTraits, sorted_distribution>(problem_size);
    register_parallel_sort_benchmark_with_distribution<TypeTraits, reverse_sorted_distribution>(problem_size);
    register_parallel_sort_benchmark_with_distribution<TypeTraits, near_sorted_1p_distribution>(problem_size);
}

void register_parallel_sort_benchmark_with_problem_size(std::size_t problem_size) {
    register_parallel_sort_benchmark_with_type<uint32_t_traits>(problem_size);
    register_parallel_sort_benchmark_with_type<uint64_t_traits>(problem_size);
    register_parallel_sort_benchmark_with_type<double_traits>(problem_size);
    register_parallel_sort_benchmark_with_type<kv_traits>(problem_size);
    register_parallel_sort_benchmark_with_type<string_traits>(problem_size);
}

void register_all_parallel_sort_benchmarks() {
    register_parallel_sort_benchmark_with_problem_size(1e4);
    register_parallel_sort_benchmark_with_problem_size(1e5);
    register_parallel_sort_benchmark_with_problem_size(2e5);
    register_parallel_sort_benchmark_with_problem_size(1e6);
    register_parallel_sort_benchmark_with_problem_size(2e6);
    register_parallel_sort_benchmark_with_problem_size(1e7);
    register_parallel_sort_benchmark_with_problem_size(3e7);
    register_parallel_sort_benchmark_with_problem_size(5e7);
    register_parallel_sort_benchmark_with_problem_size(8e7);
    register_parallel_sort_benchmark_with_problem_size(1e8);
}

int main(int argc, char** argv) {
    benchmark::Initialize(&argc, argv);

    register_all_parallel_sort_benchmarks();

    int return_code = benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return return_code;
}
