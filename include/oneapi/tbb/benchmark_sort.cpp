#include "parallel_sort.h"
#include "parallel_partition.h"
#include "tick_count.h"
#include <oneapi/dpl/algorithm>
#include <oneapi/dpl/execution>
#include <algorithm>
#include <iostream>
#include <thread>
#include <vector>
#if TEST_TASKFLOW
#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/sort.hpp>
#endif

struct std_sorter {
    template <typename Iterator, typename Compare>
    static void sort(Iterator begin, Iterator end, Compare comp) {
        std::sort(begin, end, comp);   
    }

    static const char* name() {
        return "std_sort";
    }
};

struct tbb_parallel_sorter {
    template <typename Iterator, typename Compare>
    static void sort(Iterator begin, Iterator end, Compare comp) {
        oneapi::tbb::parallel_sort(begin, end, comp);
    }

    static const char* name() {
        return "tbb_old_parallel_sort";
    }
};

struct tbb_parallel_quick_sorter {
    template <typename Iterator, typename Compare>
    static void sort(Iterator begin, Iterator end, Compare comp) {
        tbb::detail::d1::parallel_qsort(begin, end, comp);
    }

    static const char* name() {
        return "tbb_new_parallel_sort";
    }
};

struct dpl_parallel_sorter {
    template <typename Iterator, typename Compare>
    static void sort(Iterator begin, Iterator end, Compare comp) {
        oneapi::dpl::sort(oneapi::dpl::execution::par, begin, end, comp);
    }

    static const char* name() {
        return "dpl_sort";
    }
};

#if TEST_TASKFLOW
struct taskflow_sorter {
    static tf::Executor executor;

    template <typename Iterator, typename Compare>
    static void sort(Iterator begin, Iterator end, Compare comp) {
        tf::Taskflow taskflow;
        taskflow.sort(begin, end, comp);
        executor.run(taskflow).get();
    }

    static const char* name() {
        return "taskflow_sort";
    }
};

tf::Executor taskflow_sorter::executor{std::thread::hardware_concurrency()};
#endif

struct uint32_traits {
    using data_type = std::uint32_t;
    using compare = std::less<data_type>;

    static const char* name() {
        return "uint32_t";
    }
};

// TODO: why kv16?
struct kv16 {
    std::uint64_t key;
    std::uint64_t payload;

    kv16(int x) : key(static_cast<std::uint64_t>(x)), payload(0) {}
    kv16() : kv16(0) {}

    struct compare {
        bool operator()(const kv16& lhs, const kv16& rhs) const {
            return lhs.key < rhs.key;
        }
    };
};

struct kv16_traits {
    using data_type = kv16;
    using compare = kv16::compare;

    static const char* name() {
        return "kv16";
    };
};

struct uniform_distribution {
    template <typename Iterator>
    static void generate(Iterator begin, Iterator end) {
        using value_type = typename std::iterator_traits<Iterator>::value_type;

        std::mt19937 rng(0);
        std::uniform_int_distribution<int> dist(0, std::numeric_limits<int>::max());
        std::generate(begin, end, [&] { return value_type(dist(rng)); });
    }

    static const char* name() {
        return "uniform_distribution";
    }
};

struct all_equal_distribution {
    template <typename Iterator>
    static void generate(Iterator begin, Iterator end) {
        using value_type = typename std::iterator_traits<Iterator>::value_type;

        std::fill(begin, end, value_type(0));
    }

    static const char* name() {
        return "all_equal_distribution";
    }
};

struct uniq8_distribution {
    template <typename Iterator>
    static void generate(Iterator begin, Iterator end) {
        using value_type = typename std::iterator_traits<Iterator>::value_type;

        std::mt19937 rng(0);
        std::uniform_int_distribution<int> dist(0, 7);

        std::generate(begin, end, [&] { return value_type(dist(rng)); });
    };

    static const char* name() {
        return "uniq8_distribution";
    }
};

struct sorted_distribution {
    template <typename Iterator>
    static void generate(Iterator begin, Iterator end) {
        using value_type = typename std::iterator_traits<Iterator>::value_type;
        int value = 0;

        for (Iterator it = begin; it != end; ++it) {
            *it = value_type(value);
            ++value;
        }
    }

    static const char* name() {
        return "sorted_distribution";   
    }
};

struct reverse_sorted_distribution {
    template <typename Iterator>
    static void generate(Iterator begin, Iterator end) {
        using value_type = typename std::iterator_traits<Iterator>::value_type;

        int value = std::distance(begin, end);

        for (Iterator it = begin; it != end; ++it) {
            *it = value_type(value);
            --value;
        }
    }

    static const char* name() {
        return "reverse_sorted_distribution";   
    }
};

struct near_sorted_1pct_distribution {
template <typename Iterator>
    static void generate(Iterator begin, Iterator end) {
        using value_type = typename std::iterator_traits<Iterator>::value_type;

        std::size_t n = std::distance(begin, end);

        // Generate fully sorted
        int value = 0;
        for (Iterator it = begin; it != end; ++it) {
            *it = value_type(value);
            ++value;
        }

        // Perform 1% random swaps
        std::mt19937 rng(0);
        std::uniform_int_distribution<std::size_t> dist(0, n - 1);

        std::size_t swaps = n / 100;
        if (swaps == 0) swaps = 1;

        while (swaps != 0) {
            std::size_t a = dist(rng);
            std::size_t b = dist(rng);
            if (a != b) {
                std::iter_swap(begin + a, begin + b);
                --swaps;
            }
        }
    }

    static const char* name() {
        return "near_sorted_1pct_distribution";
    }
};

constexpr std::size_t num_samples = 10;

template <typename Iterator>
typename std::iterator_traits<Iterator>::value_type median(Iterator begin, Iterator end) {
    std::sort(begin, end);

    std::size_t n = std::distance(begin, end);
    std::size_t mid = n / 2;

    if (n % 2 == 1) {
        return *(begin + mid);
    } else {
        return (*(begin + mid) + *(begin + mid -1)) / 2.0;
    }
}

template <typename Sorter, typename Generator, typename TypeTraits>
void report(std::size_t problem_size, double elapsed_time) {
    std::cout << Sorter::name() << ","
              << TypeTraits::name() << ","
              << problem_size << ","
              << Generator::name() << ","
              << elapsed_time
              << std::endl;
}

template <typename Sorter, typename TypeTraits, typename Generator>
void benchmark_psort_with_distribution(std::size_t problem_size) {
    using data_type = typename TypeTraits::data_type;
    std::vector<data_type> base(problem_size);

    Generator::generate(base.begin(), base.end());

    typename TypeTraits::compare comp;

    // Warmup
    for (std::size_t i = 0; i < 2; ++i) {
        std::vector<data_type> data = base;
        Sorter::sort(data.begin(), data.end(), comp);
    }

    std::vector<double> times(num_samples);
    for (std::size_t i = 0; i < num_samples; ++i) {
        std::vector<data_type> data = base;

        oneapi::tbb::tick_count start = oneapi::tbb::tick_count::now();
        Sorter::sort(data.begin(), data.end(), comp);
        oneapi::tbb::tick_count finish = oneapi::tbb::tick_count::now();

        // Validate
        if (!std::is_sorted(data.begin(), data.end(), comp)) {
            std::cout << "Sorting error at: " << Sorter::name() << " at distribution "
                      << Generator::name() << " problem_size " << problem_size << std::endl;
            std::terminate();
        }

        times[i] = (finish - start).seconds();
    }

    double elapsed_time_median = median(times.begin(), times.end());

    report<Sorter, Generator, TypeTraits>(problem_size, elapsed_time_median);
}

template <typename Sorter, typename TypeTraits>
void benchmark_psort_with_size(std::size_t problem_size) {
    benchmark_psort_with_distribution<Sorter, TypeTraits, uniform_distribution>(problem_size);
    benchmark_psort_with_distribution<Sorter, TypeTraits, all_equal_distribution>(problem_size);
    benchmark_psort_with_distribution<Sorter, TypeTraits, uniq8_distribution>(problem_size);
    benchmark_psort_with_distribution<Sorter, TypeTraits, sorted_distribution>(problem_size);
    benchmark_psort_with_distribution<Sorter, TypeTraits, reverse_sorted_distribution>(problem_size);
    benchmark_psort_with_distribution<Sorter, TypeTraits, near_sorted_1pct_distribution>(problem_size);
}

template <typename Sorter, typename TypeTraits>
void benchmark_psort_with_type() {
    benchmark_psort_with_size<Sorter, TypeTraits>(1e4);
    benchmark_psort_with_size<Sorter, TypeTraits>(1e5);
    benchmark_psort_with_size<Sorter, TypeTraits>(2e5);
    benchmark_psort_with_size<Sorter, TypeTraits>(5e5);
    benchmark_psort_with_size<Sorter, TypeTraits>(7e5);
    benchmark_psort_with_size<Sorter, TypeTraits>(1e6);
    benchmark_psort_with_size<Sorter, TypeTraits>(5e6);
    benchmark_psort_with_size<Sorter, TypeTraits>(1e7);
    benchmark_psort_with_size<Sorter, TypeTraits>(1e8);
};

template <typename Sorter>
void benchmark_psort() {
    benchmark_psort_with_type<Sorter, uint32_traits>();
    // benchmark_psort_with_type<Sorter, kv16_traits>();
    // TODO: add strings sorting benchmark
}

int main() {
    benchmark_psort<std_sorter>();
    benchmark_psort<tbb_parallel_sorter>();
    benchmark_psort<tbb_parallel_quick_sorter>();
    benchmark_psort<dpl_parallel_sorter>();
#if TEST_TASKFLOW
    benchmark_psort<taskflow_sorter>();
#endif
}
