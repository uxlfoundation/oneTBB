#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/global_control.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/tick_count.h>

#include <vector>
#include <memory>
#include <random>
#include <iostream>

void cpu_work(std::size_t grain) {
    for (volatile int i = 0; i < grain; ++i) ;
}

int main() {
    constexpr std::size_t num_arenas = 16;
    const std::size_t num_drivers = std::thread::hardware_concurrency();

    const std::size_t arena_concurrency = 4;
    constexpr std::size_t burst_count = 256;
    constexpr std::size_t grain = 64;
    constexpr std::size_t idle_pause_us = 50;

    constexpr std::size_t num_reps = 1000;

    tbb::global_control gc(tbb::global_control::max_allowed_parallelism,
                           num_arenas * arena_concurrency);

    std::vector<std::unique_ptr<tbb::task_arena>> arenas;
    arenas.reserve(num_arenas);

    for (std::size_t i = 0; i < num_arenas; ++i) {
        arenas.emplace_back(new tbb::task_arena(int(arena_concurrency)));
        // Should it be pre-initialize?
        // arenas.back()->initialize();
    }

    std::atomic<bool> stop_signal{false};

    auto driver_fn = [&](std::size_t id) {
        std::mt19937_64 rng(0x9E3779B97F4A7C15ull ^ (id * 2654435761u));
        std::uniform_int_distribution<std::size_t> pick(0, num_arenas - 1);

        for (std::size_t i = 0; i < num_reps; ++i) {
            tbb::task_arena& arena = *arenas[pick(rng)];

            arena.execute([&] {
                tbb::parallel_for(0ul, burst_count, [](std::size_t) {
                    cpu_work(grain);
                });
            });

            std::this_thread::sleep_for(std::chrono::microseconds(idle_pause_us));
        }
    };

    std::vector<std::thread> drivers;
    drivers.reserve(num_drivers);

    auto start = tbb::tick_count::now();

    for (std::size_t i = 0; i < num_drivers; ++i) {
        drivers.emplace_back(driver_fn, i);
    }

    for (auto& driver : drivers) {
        driver.join();
    }

    auto finish = tbb::tick_count::now();

    std::cout << "Elapsed time: " << (finish - start).seconds() << std::endl;
}