#include <chrono>
#include <iostream>
#include <cmath>

// #include <oneapi/tbb/aggregating_task_group_new.h>
#include <oneapi/tbb/exp_task_group.h>
#include <tbb/parallel_for.h>
#include <tbb/task_group.h>
#include <tbb/task_arena.h>


using namespace std::chrono;


int main(int argc, char* argv[])
{
	int mode=0;
	// mode 0 executes both parallel_for and task_group based versions
    // mode 1 executes parallel_for version only
    // mode 2 executes task_group version only
	if (argc > 1) {
		mode = atoi(argv[1]);
	}
	constexpr size_t Count = 1000000000;
	size_t NumThreads = tbb::this_task_arena::max_concurrency();

	int * buf1 = new int[Count];
	int * buf2 = new int[Count];
	int * buf3 = new int[Count];

	for(int i=0; i < Count; ++i) {
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

	constexpr int MinBlockSize = 1024;	

	std::cout << "NumThreads = " << NumThreads << std::endl;
	std::cout << "Count = " << Count << std::endl;

	int RunCount = std::log2(Count/NumThreads) - log2(MinBlockSize) + 1;
	std::cout << "RunCount = " << RunCount << std::endl;

	// DoCalc over Count elements using BlockSize that evenly distributes 
	// work among threads and then with reduced sizes until MinBlockSize is reached
	
	for (int runIndex=0; runIndex <= RunCount; ++runIndex)
	{
		int BlockSize = std::max(static_cast<int>((Count/NumThreads)/(1<<runIndex)), MinBlockSize);
		std::cout << "BlockSize = " << BlockSize << std::endl;
		std::cout << "Estimated demand = " << Count/BlockSize << std::endl;
		if (mode == 0 || mode == 1)
		{
			// Use parallel_for to distribute to worker threads
			std::cout << "start parallel_for    :  " << std::flush;
			auto start_time = high_resolution_clock::now();
			tbb::blocked_range<size_t> range(0, Count, BlockSize);


		
			tbb::parallel_for(range, [=](const tbb::blocked_range<size_t> subRange){
			    for(int i=subRange.begin(); i < subRange.end(); ++i) {
					doCalc(i);
				}	
			}, tbb::simple_partitioner{});

			auto end_time = high_resolution_clock::now();
			auto duration = end_time - start_time;
			auto elapsed = duration_cast<milliseconds>(duration).count();
			std::cout << "Elapsed time = " << elapsed << " ms" << std::endl;
		}

		if (mode == 0 || mode == 2)
		{
			// Use task_group to distribute to worker threads
			std::cout << "start task_group::run :  " << std::flush;
			auto start_time = high_resolution_clock::now();
		
			tbb::task_group tg;

			for(int k=0; k < Count; k += BlockSize) {
				int subStart = k;
				int subEnd = k+BlockSize;
				if (subEnd > Count) subEnd = Count;
				tg.run([=]() {			
					for(int i=subStart; i < subEnd; ++i) {
						doCalc(i);
					}	
				});
			}
			tg.wait();

			auto end_time = high_resolution_clock::now();
			auto duration = end_time - start_time;
			auto elapsed = duration_cast<milliseconds>(duration).count();
			std::cout << "Elapsed time = " << elapsed << " ms" << std::endl;
		}

        if (mode == 0 || mode == 3)
        {
            std::cout << "start aggregating task_group::run::  " << std::flush;
            auto start_time = high_resolution_clock::now();

            tbb::exp_task_group tg;

            for(int k=0; k < Count; k += BlockSize) {
				int subStart = k;
				int subEnd = k+BlockSize;
				if (subEnd > Count) subEnd = Count;
				tg.run([=]() {			
					for(int i=subStart; i < subEnd; ++i) {
						doCalc(i);
					}	
				});
			}
			tg.wait();

            auto end_time = high_resolution_clock::now();
			auto duration = end_time - start_time;
			auto elapsed = duration_cast<milliseconds>(duration).count();
			std::cout << "Elapsed time = " << elapsed << " ms" << std::endl;
        }
        std::cout << std::endl << std::endl;
	}

	// Utilize some of the results buffer to prevent optimizer from skipping all the work
	size_t accum = 0;
	
		// constexpr int ResultCount = Count;
		constexpr int ResultCount = 2; // For expediency, just touch a couple elements
		for(int i=0; i < ResultCount; ++i) {
			accum += buf3[i];
		}

	
	std::cout << "result = " << accum << std::endl;

	delete [] buf1;
	delete [] buf2;
	delete [] buf3;

	return 0;
}