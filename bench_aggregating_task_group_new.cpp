#include <chrono>
#include <iostream>
#include <cmath>

// #include <oneapi/tbb/aggregating_task_group_new.h>
#include <oneapi/tbb/exp_task_group.h>
#include <tbb/parallel_for.h>
#include <tbb/task_group.h>
#include <tbb/task_arena.h>
#include <tbb/global_control.h>


using namespace std::chrono;


int main(int argc, char* argv[])
{
	int mode = -1;
    // mode 0 executes parallel_for version only
    // mode 1 executes task_group version only
    // mode 2 executes exp_task_group version only
	if (argc > 1) {
		mode = atoi(argv[1]);
    }

    if (mode == -1) {
        std::cout << "ERROR: mode not specified" << std::endl;
        return 1;
    }

    int NumThreads = tbb::this_task_arena::max_concurrency();

    if (argc > 2) {
        NumThreads = atoi(argv[2]);
    }

    int NumTasks = 100'000'000;

    if (argc > 3) {
        NumTasks = atoi(argv[3]);
    }

    int* buf1 = new int[NumTasks];
    int* buf2 = new int[NumTasks];
    int* buf3 = new int[NumTasks];


	for(int i=0; i < NumTasks; ++i) {
		buf1[i] = i;
		buf2[i] = i;
		buf3[i] = -i;
	}

	auto doCalc = [&](int i) {
		int v1 = buf1[i];
		int v2 = buf2[i];
		int repeat = std::max(0, v2%10);
		int cal = v1;
		for(int l=0; l < repeat; ++l)
		{
			cal = static_cast<int>(std::sqrt(float(cal)) + v1);
		}
		buf3[i] = cal;
	};

	std::cout << "NumThreads = " << NumThreads << std::endl;
	std::cout << "NumTasks = " << NumTasks << std::endl;

    tbb::global_control gc(tbb::global_control::max_allowed_parallelism, NumThreads);

    constexpr std::size_t NumRuns = 6;
	std::vector<int> times(NumRuns, 0);

    for (std::size_t NumRun = 0; NumRun < NumRuns; ++NumRun) {
        auto start_time = high_resolution_clock::now();
        if (mode == 0) {
            tbb::blocked_range<size_t> range(0, NumTasks, 1);

            tbb::parallel_for(range, [=](const tbb::blocked_range<size_t>& subRange) {
                for (size_t i = subRange.begin(); i < subRange.end(); ++i) {
                    doCalc(i);
                }
            }, tbb::simple_partitioner{});
        }
        if (mode == 1) {
            tbb::task_group tg;

            for (int i = 0; i < NumTasks; ++i) {
                tg.run([=] {
                    doCalc(i);
                });
#ifndef AGGRESSIVE_SUBMIT
                std::this_thread::sleep_for(milliseconds(100));
#endif
            }
            tg.wait();
        }
        if (mode == 2) {
            tbb::exp_task_group tg;

            for (int i = 0; i < NumTasks; ++i) {
                tg.run([=] {
                    doCalc(i);
                });
#ifndef AGGRESSIVE_SUBMIT
                std::this_thread::sleep_for(milliseconds(100));
#endif
            }
            tg.wait();
        }
        auto end_time = high_resolution_clock::now();
        auto duration = end_time - start_time;
        auto elapsed = duration_cast<milliseconds>(duration).count();

        times[NumRun] = elapsed;

        // Utilize some of the results buffer to prevent optimizer from skipping all the work
        size_t accum = 0;
        
        // constexpr int ResultCount = Count;
        constexpr int ResultCount = 2; // For expediency, just touch a couple elements
        for(int i=0; i < ResultCount; ++i) {
            accum += buf3[i];
        }

        std::cout << "result = " << accum << std::endl;

        // Reset
        for(int i=0; i < NumTasks; ++i) {
            buf1[i] = i;
            buf2[i] = i;
            buf3[i] = -i;
        }
    }

    std::sort(times.begin(), times.end());
    std::cout << "Median Elapsed Time: " << times[times.size() / 2] << std::endl;

	delete [] buf1;
	delete [] buf2;
	delete [] buf3;

	return 0;
}