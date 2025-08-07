/*
    Copyright (c) 2005-2020 Intel Corporation

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "perf.h"

#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cassert>
#include <atomic>
#include <thread>
#include <new>

#include "tbb/tick_count.h"
#include "tbb/info.h"
#include "tbb/global_control.h"
#include "tbb/task_group.h"
#include "tbb/task_arena.h"

#if  __linux__ || __APPLE__ || __FreeBSD__ || __NetBSD__
    #include <sys/resource.h>
#endif

__TBB_PERF_API int NumCpus = tbb::info::default_concurrency(),
                   NumThreads,
                   MaxConcurrency;

namespace Perf {

SessionSettings theSettings;

namespace internal {

    typedef std::vector<duration_t> durations_t;

    static uintptr_t NumRuns = 7;
    static duration_t RunDuration = 0.01;

    static const int RateFieldLen = 10;
    static const int OvhdFieldLen = 12;

    const char* TestNameColumnTitle = "Test name";
    const char* WorkloadNameColumnTitle = "Workload";

    size_t TitleFieldLen = 0;
    size_t WorkloadFieldLen = 0;

    int TotalConfigs = 0;
    int MaxTbbMasters = 1;

    //! Defines the mapping between threads and cores in the undersubscription mode
    /** When adding new enumerator, insert it before amLast, and do not specify
        its value explicitly. **/
    enum AffinitizationMode {
        amFirst = 0,
        amDense = amFirst,
        amSparse,
        //! Used to track the number of supported affinitization modes
        amLast
    };

    static const int NumAffinitizationModes = amLast - amFirst; 

    const char* AffinitizationModeNames[] = { "dense", "sparse" };

    int NumActiveAffModes = 1;

    //! Settings of a test run configuration
    struct RunConfig {
        int my_maxConcurrency;
        int my_numThreads;      // For task scheduler tests this is number of workers + 1
        int my_numMasters;      // Used for task scheduler tests only
        int my_affinityMode;    // Used for task scheduler tests only
        int my_workloadID;

        int NumMasters () const {
            return theSettings.my_opts & UseTaskScheduler ? my_numMasters : my_numThreads;
        }
    };

    double StandardDeviation ( double avg, const durations_t& d ) {
        double  std_dev = 0;
        for ( uintptr_t i = 0; i < d.size(); ++i ) {
            double  dev = fabs(d[i] - avg);
            std_dev += dev * dev;
        }
        std_dev = sqrt(std_dev / d.size());
        return std_dev / avg * 100;
    }

    void Statistics ( const durations_t& d, 
                      duration_t& avgTime, double& stdDev, 
                      duration_t& minTime, duration_t& maxTime )
    {
        minTime = maxTime = avgTime = d[0];
        for ( size_t i = 1; i < d.size(); ++i ) {
            avgTime += d[i];
            if ( minTime > d[i] )
                minTime = d[i];
            else if ( maxTime < d[i] )
                maxTime = d[i];
        }
        avgTime = avgTime / d.size();
        stdDev = StandardDeviation( avgTime, d );
    }

    //! Timing data for the series of repeated runs and results of their statistical processing
    struct TimingSeries {
        //! Statistical timing series
        durations_t my_durations;
        
        //! Average time obtained from my_durations data
        duration_t  my_avgTime;

        //! Minimal time obtained from my_durations data
        duration_t  my_minTime;

        //! Minimal time obtained from my_durations data
        duration_t  my_maxTime;

        //! Standard deviation of my_avgTime value (per cent)
        double  my_stdDev;

        TimingSeries ( uintptr_t nruns = NumRuns )
            : my_durations(nruns), my_avgTime(0), my_minTime(0), my_maxTime(0)
        {}

        void CalculateStatistics () {
            Statistics( my_durations, my_avgTime, my_stdDev, my_minTime, my_maxTime );
        }
    }; // struct TimingSeries

    //! Settings and timing results for a test run configuration
    struct RunResults {
        //! Run configuration settings
        RunConfig   my_config;
        
        //! Timing results for this run configuration
        TimingSeries my_timing;
    };

    typedef std::vector<const char*>    names_t;
    typedef std::vector<TimingSeries>   timings_t;
    typedef std::vector<RunResults>     test_results_t;

    enum TestMethods {
        idRunSerial = 0x01,
        idOnStart = 0x02,
        idOnFinish = 0x04,
        idPrePostProcess = idOnStart | idOnFinish
    };

    //! Set of flags identifying methods not overridden by the currently active test
    /** Used as a scratch var. **/
    uintptr_t g_absentMethods;

    //! Test object and timing results for all of its configurations 
    struct TestResults {
        //! Pointer to the test object interface
        Test*           my_test;

        //! Set of flags identifying optional methods overridden by my_test
        /** A set of ORed TestMethods flags **/
        uintptr_t       my_availableMethods;
        
        //! Vector of serial times for each workload supported by this test
        /** Element index in the vector serves as a zero based workload ID. **/
        timings_t       my_serialBaselines;
        
        //! Common baselines for both parallel and serial variants
        /** Element index in the vector serves as a zero based workload ID. **/
        timings_t       my_baselines;

        //! Strings identifying workloads to be used in output
        names_t         my_workloadNames;

        //! Vector of timings for all run configurations of my_test
        test_results_t  my_results;

        const char*     my_testName;

        mutable bool    my_hasOwnership;

        TestResults ( Test* t, const char* className, bool takeOwnership )
            : my_test(t), my_availableMethods(0), my_testName(className), my_hasOwnership(takeOwnership)
        {}

        TestResults ( const TestResults& tr )
            : my_test(tr.my_test)
            , my_availableMethods(0)
            , my_testName(tr.my_testName)
            , my_hasOwnership(tr.my_hasOwnership)
        {
            tr.my_hasOwnership = false;
        }

        ~TestResults () {
            for ( size_t i = 0; i < my_workloadNames.size(); ++i )
                delete my_workloadNames[i];
            if ( my_hasOwnership )
                delete my_test;
        }
    }; // struct TestResults

    typedef std::vector<TestResults> session_t;

    session_t theSession;

    TimingSeries CalibrationTiming;

    const uintptr_t CacheSize = 8*1024*1024;
    volatile intptr_t W[CacheSize];

    struct WiperBody {
        void operator()( int ) const {
            volatile intptr_t sink = 0;
            for ( uintptr_t i = 0; i < CacheSize; ++i )
                sink += W[i];
        }
    };

    void TraceHistogram ( const durations_t& t, const char* histogramFileName ) {
        FILE* f = histogramFileName ? fopen(histogramFileName, "wt") : stdout;
        uintptr_t  n = t.size();
        const uintptr_t num_buckets = 100;
        double  min_val = *std::min_element(t.begin(), t.end()),
                max_val = *std::max_element(t.begin(), t.end()),
                bucket_size = (max_val - min_val) / num_buckets;
        std::vector<uintptr_t> hist(num_buckets + 1, 0);
        for ( uintptr_t i = 0; i < n; ++i )
            ++hist[uintptr_t((t[i]-min_val)/bucket_size)];
        assert(hist[num_buckets] == 1);
        ++hist[num_buckets - 1];
        hist.resize(num_buckets);
        fprintf (f, "Histogram: nvals = %u, min = %g, max = %g, nbuckets = %u\n", (unsigned)n, min_val, max_val, (unsigned)num_buckets);
        double bucket = min_val;
        for ( uintptr_t i = 0; i < num_buckets; ++i, bucket+=bucket_size )
            fprintf (f, "%12g\t%u\n", bucket, (unsigned)hist[i]);
        fclose(f);
    }

#if _MSC_VER
    typedef DWORD_PTR cpu_set_t;

    class AffinityHelper {
        static const unsigned MaxAffinitySetSize = sizeof(cpu_set_t) * 8;
        static unsigned AffinitySetSize;

        //! Mapping from a CPU index to a valid affinity cpu_mask
        /** The first element is not used. **/
        static cpu_set_t m_affinities[MaxAffinitySetSize + 1];

        static cpu_set_t m_processMask;

        class Initializer {
        public:
            Initializer () {
                SYSTEM_INFO si;
                GetNativeSystemInfo(&si);
                assert( (si.dwNumberOfProcessors <= MaxAffinitySetSize) && "Too many CPUs" );
                AffinitySetSize = min (si.dwNumberOfProcessors, MaxAffinitySetSize);
                cpu_set_t systemMask = 0;
                GetProcessAffinityMask( GetCurrentProcess(), &m_processMask, &systemMask );
                cpu_set_t cpu_mask = 1;
                for ( DWORD i = 0; i < AffinitySetSize; ++i ) {
                    while ( !(cpu_mask & m_processMask) && cpu_mask )
                        cpu_mask <<= 1;
                    assert( (cpu_mask != 0) && "Process affinity set is culled?" );
                    m_affinities[i] = cpu_mask;
                    cpu_mask <<= 1;
                }
            }
        }; // class AffinityHelper::Initializer

        static Initializer m_initializer;

    public:
        static cpu_set_t CpuAffinity ( int cpuIndex ) {
            return m_affinities[cpuIndex % AffinitySetSize];
        }

        static const cpu_set_t& ProcessMask () { return m_processMask; }
    }; // class AffinityHelper

    unsigned AffinityHelper::AffinitySetSize = 0;
    cpu_set_t AffinityHelper::m_affinities[AffinityHelper::MaxAffinitySetSize + 1] = {0};
    cpu_set_t AffinityHelper::m_processMask = 0;
    AffinityHelper::Initializer AffinityHelper::m_initializer;

    #define CPU_ZERO(cpu_mask)              (*cpu_mask = 0)
    #define CPU_SET(cpu_idx, cpu_mask)      (*cpu_mask |= AffinityHelper::CpuAffinity(cpu_idx))
    #define CPU_CLR(cpu_idx, cpu_mask)      (*cpu_mask &= ~AffinityHelper::CpuAffinity(cpu_idx))
    #define CPU_ISSET(cpu_idx, cpu_mask)    ((*cpu_mask & AffinityHelper::CpuAffinity(cpu_idx)) != 0)

#elif __linux__ /* end of _MSC_VER */

    #include <unistd.h>
    #include <sys/types.h>
    #include <linux/unistd.h>

    pid_t gettid() { return (pid_t)syscall(__NR_gettid); }

    #define GET_MASK(cpu_set) (*(unsigned*)(void*)&cpu_set)
    #define RES_STAT(res) (res != 0 ? "failed" : "ok")

    class AffinityHelper {
        static cpu_set_t m_processMask;

        class Initializer {
        public:
            Initializer () {
                CPU_ZERO (&m_processMask);
                int res = sched_getaffinity( getpid(), sizeof(cpu_set_t), &m_processMask );
                assert( (res == 0) && "sched_getaffinity failed" );
            }
        }; // class AffinityHelper::Initializer

        static Initializer m_initializer;

    public:
        static const cpu_set_t& ProcessMask () { return m_processMask; }
    }; // class AffinityHelper

    cpu_set_t AffinityHelper::m_processMask;
    AffinityHelper::Initializer AffinityHelper::m_initializer;
#endif /* __linux__ */

    //! Simple native parallel loop where each iteration is executed in a different thread
    template <typename Index, typename Body>
    void NativeParallelFor( Index Number, const Body& body ) {
        std::vector<std::thread> thread_pool;
        for (Index idx = 0; idx < Number; ++idx) {
            thread_pool.emplace_back([&body, idx] {
                body(idx);
            });
        }

        for (auto& thread : thread_pool) {
            thread.join();
        }
    }

    //! Native parallel loop with grainsize (like tbb::blocked_range)
    template <typename Index, typename Body>
    void NativeParallelFor( Index Number, Index block_size, const Body& body ) {
        NativeParallelFor(Number / block_size, [block_size, &body] (Index idx) {
            for (Index i = idx * block_size; i < (idx + 1) * block_size; ++i) {
                body(i);
            }
        });
    }

    bool PinTheThread ( int cpu_idx, std::atomic<int>& nThreads ) {
    #if _MSC_VER || __linux__
        cpu_set_t orig_mask, target_mask;
        CPU_ZERO( &target_mask );
        CPU_SET( cpu_idx, &target_mask );
        assert( (CPU_ISSET(cpu_idx, &target_mask)) && "CPU_SET failed" );
    #endif
    #if _MSC_VER
        orig_mask = SetThreadAffinityMask( GetCurrentThread(), target_mask );
        if ( !orig_mask )
            return false;
    #elif __linux__
        CPU_ZERO( &orig_mask );
        int res = sched_getaffinity( gettid(), sizeof(cpu_set_t), &orig_mask );
        assert( (res == 0) && "sched_getaffinity failed" );
        res = sched_setaffinity( gettid(), sizeof(cpu_set_t), &target_mask );
        assert( (res == 0) && "sched_setaffinity failed" );
    #endif /* _MSC_VER */
        --nThreads;
        while ( nThreads )
            std::this_thread::yield();
    #if _MSC_VER
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    #endif
        return true;
    }

    std::atomic<int> m_result;
    std::atomic<int> m_nThreads;

    bool AffinitizeTBB ( int p, int affMode ) {
    #if _MSC_VER
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        SetPriorityClass (GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    #endif
        m_result = 0;
        m_nThreads = p;
        tbb::task_group tg;
        for ( int i = 0; i < p; ++i ) {
            tg.run( [&](){
                int idx = tbb::this_task_arena::current_thread_index();
                if ( PinTheThread( affMode == amSparse ? idx * NumCpus / p : idx, m_nThreads ) )
                    ++m_result;
            });
        }
        tg.wait();
        return m_result == p;
    }

    inline 
    void Affinitize ( int p, int affMode ) {
        if ( !AffinitizeTBB (p, affMode) )
            Report("Warning: Failed to set affinity for %d TBB threads\n", p);
    }

    class TbbWorkersTrapper {
        tbb::task_group_context my_context;
        tbb::task_group spawn_group;
        tbb::task_group wait_group;
        tbb::task_handle trap;
        
        Perf::SpinBarrier my_barrier;

        friend class TrapperTask;

        class TrapperTask {
            TbbWorkersTrapper& my_owner;
        public:
            TrapperTask ( TbbWorkersTrapper& owner ) : my_owner(owner) {}
            void operator()() {
                my_owner.my_barrier.wait();
                my_owner.wait_group.wait();
            }
        };

    public:
        TbbWorkersTrapper ()
            : my_context(tbb::task_group_context::bound, 
                         tbb::task_group_context::default_traits | tbb::task_group_context::concurrent_wait),
              spawn_group(my_context),
              wait_group(my_context)
        {
            trap = wait_group.defer([](){});
            my_barrier.initialize(NumThreads);
            for ( int i = 1; i < NumThreads; ++i )
                spawn_group.run( TrapperTask(*this) );
            my_barrier.wait(); // Wait until all workers are ready
        }

        ~TbbWorkersTrapper () {
            wait_group.run_and_wait(std::move(trap));
            spawn_group.wait(); // Make sure no tasks are referencing us
        }
    }; // TbbWorkersTrapper


#if __TBB_STATISTICS
    static bool StatisticsMode = true;
#else
    static bool StatisticsMode = false;
#endif

//! Suppresses silly warning
inline bool __TBB_bool( bool b ) { return b; }

#define START_WORKERS(needScheduler, p, a, setWorkersAffinity, trapWorkers) \
    tbb::global_control *thread_ctl = nullptr;                              \
    TbbWorkersTrapper *trapper = nullptr;                                   \
    if ( theSettings.my_opts & UseTaskScheduler                             \
         && (needScheduler) && ((setWorkersAffinity) || (trapWorkers)) )    \
    {                                                                       \
        thread_ctl = new tbb::global_control                                \
            (tbb::global_control::max_allowed_parallelism, p );             \
        if ( __TBB_bool(setWorkersAffinity) )                               \
            Affinitize( p, a );                                             \
        if ( __TBB_bool(trapWorkers) )                                      \
            trapper = new TbbWorkersTrapper;                                \
    }

#define STOP_WORKERS()  \
    if ( trapper ) delete trapper;                                          \
    if ( thread_ctl ) delete thread_ctl;                                    \
    if ( theSettings.my_opts & UseTaskScheduler ) {                         \
        tbb::task_scheduler_handle tsh{tbb::attach{}};                      \
        tbb::finalize(tsh, std::nothrow);                                   \
    }

    typedef void (Test::*RunMemFnPtr)( Test::ThreadInfo& );

    TimingSeries *TlsTimings;
    Perf::SpinBarrier  multipleMastersBarrier;

    class TimingFunctor {
        Test* my_test;
        RunConfig *my_cfg;
        RunMemFnPtr my_fnRun;
        size_t my_numRuns;
        size_t my_numRepeats;
        uintptr_t my_availableMethods;

        duration_t TimeSingleRun ( Test::ThreadInfo& ti ) const {
            if ( my_availableMethods & idOnStart )
                my_test->OnStart(ti);
            // Warming run
            (my_test->*my_fnRun)(ti);
            multipleMastersBarrier.wait();
            tbb::tick_count t0 = tbb::tick_count::now();
            (my_test->*my_fnRun)(ti);
            duration_t t = (tbb::tick_count::now() - t0).seconds();
            if ( my_availableMethods & idOnFinish )
                my_test->OnFinish(ti);
            return t;
        }

    public:
        TimingFunctor ( Test* test, RunConfig *cfg, RunMemFnPtr fnRun, 
                        size_t numRuns, size_t nRepeats, uintptr_t availableMethods )
            : my_test(test), my_cfg(cfg), my_fnRun(fnRun)
            , my_numRuns(numRuns), my_numRepeats(nRepeats), my_availableMethods(availableMethods)
        {}

        void operator()( int tid ) const {
            Test::ThreadInfo ti = { tid, NULL };
            durations_t &d = TlsTimings[tid].my_durations;
            bool singleMaster = my_cfg->my_numMasters == 1;
            START_WORKERS( (!singleMaster || (singleMaster && StatisticsMode)) && my_fnRun != &Test::RunSerial, 
                            my_cfg->my_numThreads, my_cfg->my_affinityMode, singleMaster, singleMaster );
            for ( uintptr_t k = 0; k < my_numRuns; ++k )  {
                if ( my_numRepeats > 1 ) {
                    d[k] = 0;
                    if ( my_availableMethods & idPrePostProcess ) {
                        for ( uintptr_t i = 0; i < my_numRepeats; ++i )
                            d[k] += TimeSingleRun(ti);
                    }
                    else {
                        multipleMastersBarrier.wait();
                        tbb::tick_count t0 = tbb::tick_count::now();
                        for ( uintptr_t i = 0; i < my_numRepeats; ++i )
                            (my_test->*my_fnRun)(ti);
                        d[k] = (tbb::tick_count::now() - t0).seconds();
                    }
                    d[k] /= my_numRepeats;
                }
                else
                    d[k] = TimeSingleRun(ti);
            }
            STOP_WORKERS();
            TlsTimings[tid].CalculateStatistics();
        }
    }; // class TimingFunctor
    
    void DoTiming ( TestResults& tr, RunConfig &cfg, RunMemFnPtr fnRun, size_t nRepeats, TimingSeries& ts ) {
        int numThreads = cfg.NumMasters();
        size_t numRuns = ts.my_durations.size() / numThreads;
        TimingFunctor body( tr.my_test, &cfg, fnRun, numRuns, nRepeats, tr.my_availableMethods );
        multipleMastersBarrier.initialize(numThreads);
        tr.my_test->SetWorkload(cfg.my_workloadID);
        if ( numThreads == 1 ) {
            TimingSeries *t = TlsTimings;
            TlsTimings = &ts;
            body(0);
            TlsTimings = t;
        }
        else {
            ts.my_durations.resize(numThreads * numRuns);
            NativeParallelFor( numThreads, body );
            for ( int i = 0, j = 0; i < numThreads; ++i ) {
                durations_t &d = TlsTimings[i].my_durations;
                for ( size_t k = 0; k < numRuns; ++k, ++j )
                    ts.my_durations[j] = d[k];
            }
            ts.CalculateStatistics();
        }
    }

    //! Runs the test function, does statistical processing, and, if title is nonzero, prints results.
    /** If histogramFileName is a string, the histogram of individual runs is generated and stored
        in a file with the given name. If it is NULL then the histogram is printed on the console.
        By default no histogram is generated. 
        The histogram format is: "rate bucket start" "number of tests in this bucket". **/
    void RunTestImpl ( TestResults& tr, RunConfig &cfg, RunMemFnPtr pfnTest, TimingSeries& ts ) {
        // nRepeats is a number of repeated calls to the test function made as 
        // part of the same run. It is determined experimentally by the following 
        // calibration process so that the total run time was approx. RunDuration.
        // This is helpful to increase the measurement precision in case of very 
        // short tests.
        size_t nRepeats = 1;
        // A minimal stats is enough when doing calibration
        CalibrationTiming.my_durations.resize( (NumRuns < 4 ? NumRuns : 3) * cfg.NumMasters() );
        // There's no need to be too precise when calculating nRepeats. And reasonably 
        // far extrapolation can speed up the process significantly.
        for (;;) {
            DoTiming( tr, cfg, pfnTest, nRepeats, CalibrationTiming );
            if ( CalibrationTiming.my_avgTime * nRepeats > 1e-4 )
                break;
            nRepeats *= 2;
        }
        nRepeats *= (uintptr_t)ceil( RunDuration / (CalibrationTiming.my_avgTime * nRepeats) );

        DoTiming(tr, cfg, pfnTest, nRepeats, ts);

        // No histogram for baseline measurements
        if ( pfnTest != &Test::RunSerial && pfnTest != &Test::Baseline ) {
            const char* histogramName = theSettings.my_histogramName;
            if ( histogramName != NoHistogram && tr.my_test->HistogramName() != DefaultHistogram )
                histogramName = tr.my_test->HistogramName();
            if ( histogramName != NoHistogram )
                TraceHistogram( ts.my_durations, histogramName );
        }
    } // RunTestImpl

    typedef void (*TestActionFn) ( TestResults&, int mastersRange, int w, int p, int m, int a, int& numTests );

    int TestResultIndex ( int mastersRange, int w, int p, int m, int a ) {
        return ((w * (MaxThread - MinThread + 1) + (p - MinThread)) * mastersRange + m) * NumActiveAffModes + a;
    }

    void RunTest ( TestResults& tr, int mastersRange, int w, int p, int m, int a, int& numTests ) {
        size_t r = TestResultIndex(mastersRange, w, p, m, a);
        assert( r < tr.my_results.size());
        RunConfig &rc = tr.my_results[r].my_config;
        rc.my_maxConcurrency = MaxConcurrency;
        rc.my_numThreads = p;
        rc.my_numMasters = m + tr.my_test->MinNumMasters();
        rc.my_affinityMode = a;
        rc.my_workloadID = w;
        RunTestImpl( tr, rc, &Test::Run, tr.my_results[r].my_timing );
        printf( "Running tests: %04.1f%%\r",  ++numTests * 100. / TotalConfigs ); fflush(stdout);
    }

    void WalkTests ( TestActionFn fn, int& numTests, bool setAffinity, bool trapWorkers, bool multipleMasters ) {
        for ( int p = MinThread; p <= MaxThread; ++p ) {
            NumThreads = p;
            MaxConcurrency = p < NumCpus ? p : NumCpus;
            for ( int a = 0; a < NumActiveAffModes; ++a ) {
                START_WORKERS( multipleMasters || !StatisticsMode, p, a, setAffinity, trapWorkers );
                for ( size_t i = 0; i < theSession.size(); ++i ) {
                    TestResults &tr = theSession[i];
                    Test *t = tr.my_test;
                    int mastersRange = t->MaxNumMasters() - t->MinNumMasters() + 1;
                    int numWorkloads = theSettings.my_opts & UseSmallestWorkloadOnly ? 1 : t->NumWorkloads();
                    for ( int w = 0; w < numWorkloads; ++w ) {
                        if ( multipleMasters )
                            for ( int m = 1; m < mastersRange; ++m )
                                fn( tr, mastersRange, w, p, m, a, numTests );
                        else
                            fn( tr, mastersRange, w, p, 0, a, numTests );
                    }
                }
                STOP_WORKERS();
            }
        }
    }

    void RunTests () {
        int numTests = 0;
        WalkTests( &RunTest, numTests, !StatisticsMode, !StatisticsMode, false );
        if ( MaxTbbMasters > 1 )
            WalkTests( &RunTest, numTests, true, false, true );
    }

    void InitTestData ( TestResults& tr, int mastersRange, int w, int p, int m, int a, int& ) {
        size_t r = TestResultIndex(mastersRange, w, p, m, a);
        assert( r < tr.my_results.size() );
        tr.my_results[r].my_timing.my_durations.resize( 
            (theSettings.my_opts & UseTaskScheduler ? tr.my_test->MinNumMasters() + m : p) * NumRuns );
    }

    char WorkloadName[MaxWorkloadNameLen + 1];

    void PrepareTests () {
        printf( "Initializing...\r" );
        NumActiveAffModes = theSettings.my_opts & UseAffinityModes ? NumAffinitizationModes : 1;
        TotalConfigs = 0;
        TitleFieldLen = strlen( TestNameColumnTitle );
        WorkloadFieldLen = strlen( WorkloadNameColumnTitle );
        int numThreads = MaxThread - MinThread + 1;
        int numConfigsBase = numThreads * NumActiveAffModes;
        int totalWorkloads = 0;
        for ( size_t i = 0; i < theSession.size(); ++i ) {
            TestResults &tr = theSession[i];
            Test &t = *tr.my_test;
            int numWorkloads = theSettings.my_opts & UseSmallestWorkloadOnly ? 1 : t.NumWorkloads();
            int numConfigs = numConfigsBase * numWorkloads;
            if ( t.MaxNumMasters() > 1 ) {
                assert( (theSettings.my_opts & UseTaskScheduler) && "Multiple masters mode is only valid for task scheduler tests" );
                if ( MaxTbbMasters < t.MaxNumMasters() )
                    MaxTbbMasters = t.MaxNumMasters();
                numConfigs *= t.MaxNumMasters() - t.MinNumMasters() + 1;
            }
            totalWorkloads += numWorkloads;
            TotalConfigs += numConfigs;

            const char* testName = t.Name();
            if ( testName )
                tr.my_testName = testName;
            assert( tr.my_testName && "Neither Test::Name() is implemented, nor RTTI is enabled" );
            TitleFieldLen = max( TitleFieldLen, strlen(tr.my_testName) );

            tr.my_results.resize( numConfigs );
            tr.my_serialBaselines.resize( numWorkloads );
            tr.my_baselines.resize( numWorkloads );
            tr.my_workloadNames.resize( numWorkloads );
        }
        TimingSeries tmpTiming;
        TlsTimings = &tmpTiming; // All measurements are serial here
        int n = 0;
        for ( size_t i = 0; i < theSession.size(); ++i ) {
            TestResults &tr = theSession[i];
            Test &t = *tr.my_test;
            // Detect which methods are overridden by the test implementation
            g_absentMethods = 0;
            Test::ThreadInfo ti = { 0 };
            t.SetWorkload(0);
            t.OnStart(ti);
            t.RunSerial(ti);
            t.OnFinish(ti);
            if ( theSettings.my_opts & UseSerialBaseline && !(g_absentMethods & idRunSerial) )
                tr.my_availableMethods |= idRunSerial;
            if ( !(g_absentMethods & idOnStart) )
                tr.my_availableMethods |= idOnStart;

            RunConfig rc = { 1, 1, 1, 0, 0 };
            int numWorkloads = theSettings.my_opts & UseSmallestWorkloadOnly ? 1 : t.NumWorkloads();
            for ( int w = 0; w < numWorkloads; ++w ) {
                WorkloadName[0] = 0;
                t.SetWorkload(w);
                if ( !WorkloadName[0] )
                    sprintf( WorkloadName, "%d", w );
                size_t len = strlen(WorkloadName);
                tr.my_workloadNames[w] = new char[len + 1];
                strcpy ( (char*)tr.my_workloadNames[w], WorkloadName );
                WorkloadFieldLen = max( WorkloadFieldLen, len );

                rc.my_workloadID = w;
                if ( theSettings.my_opts & UseBaseline )
                    RunTestImpl( tr, rc, &Test::Baseline, tr.my_baselines[w] );
                if ( tr.my_availableMethods & idRunSerial )
                    RunTestImpl( tr, rc, &Test::RunSerial, tr.my_serialBaselines[w] );
                printf( "Measuring baselines: %04.1f%%\r",  ++n * 100. / totalWorkloads ); fflush(stdout);
            }
        }
        TlsTimings = new TimingSeries[MaxThread + MaxTbbMasters - 1];
        if ( theSettings.my_opts & UseTaskScheduler ? MaxTbbMasters : MaxThread )
            WalkTests( &InitTestData, n, false, false, theSettings.my_opts & UseTaskScheduler ? true : false );
        CalibrationTiming.my_durations.reserve( MaxTbbMasters * 3 );
        printf( "                                                          \r");
    }

    FILE* ResFile = NULL;

    __TBB_PERF_API void Report ( char const* fmt, ... ) {
        va_list args;
        if ( ResFile ) {
            va_start( args, fmt );
            vfprintf( ResFile, fmt, args );
            va_end( args );
        }
        va_start( args, fmt );
        vprintf( fmt, args );
        va_end( args );
    }

    void PrintResults () {
        if ( theSettings.my_resFile )
            ResFile = fopen( theSettings.my_resFile, "w" );
        Report( "%-*s %-*s %s", TitleFieldLen, "Test-name", WorkloadFieldLen, "Workload", 
                                MaxTbbMasters > 1 ? "W    M    " : "T    " );
        if ( theSettings.my_opts & UseAffinityModes )
            Report( "Aff  " );
        Report( "%-*s SD,%%  %-*s %-*s %-*s ",
                RateFieldLen, "Avg.time", OvhdFieldLen, "Par.ovhd,%",
                RateFieldLen, "Min.time", RateFieldLen, "Max.time" );
        Report( " | Repeats = %lu, CPUs %d\n", (unsigned long)NumRuns, NumCpus );
        for ( size_t i = 0; i < theSession.size(); ++i ) {
            TestResults &tr = theSession[i];
            for ( size_t j = 0; j < tr.my_results.size(); ++j ) {
                RunResults &rr = tr.my_results[j];
                RunConfig &rc = rr.my_config;
                int w = rc.my_workloadID;
                TimingSeries &ts = rr.my_timing;
                duration_t baselineTime = tr.my_baselines[w].my_avgTime,
                           cleanTime = ts.my_avgTime - baselineTime;
                Report( "%-*s %-*s ", TitleFieldLen, tr.my_testName, WorkloadFieldLen, tr.my_workloadNames[w] );
                if ( MaxTbbMasters > 1 )
                    Report( "%-4d %-4d ", rc.my_numThreads - 1, rc.my_numMasters );
                else
                    Report( "%-4d ", rc.my_numThreads );
                if ( theSettings.my_opts & UseAffinityModes )
                    Report( "%%-8s ", AffinitizationModeNames[rc.my_affinityMode] );
                Report( "%-*.2e %-6.1f ", RateFieldLen, cleanTime, ts.my_stdDev);
                if ( tr.my_availableMethods & idRunSerial  ) {
                    duration_t serialTime = (tr.my_serialBaselines[w].my_avgTime - baselineTime) / rc.my_maxConcurrency;
                    Report( "%-*.1f ", OvhdFieldLen, 100*(cleanTime - serialTime)/serialTime );
                }
                else
                    Report( "%*s%*s ", OvhdFieldLen/2, "-", OvhdFieldLen - OvhdFieldLen/2, "" );
                Report( "%-*.2e %-*.2e ", RateFieldLen, ts.my_minTime - baselineTime, RateFieldLen, ts.my_maxTime - baselineTime);
                Report( "\n" );
            }
        }
        delete [] TlsTimings;
        if ( ResFile )
            fclose(ResFile);
    }

    __TBB_PERF_API void RegisterTest ( Test* t, const char* className, bool takeOwnership ) {
        // Just collect test objects at this stage
        theSession.push_back( TestResults(t, className, takeOwnership) );
    }

} // namespace internal

__TBB_PERF_API void Test::Baseline ( ThreadInfo& ) {}

__TBB_PERF_API void Test::RunSerial ( ThreadInfo& ) { internal::g_absentMethods |= internal::idRunSerial; }

__TBB_PERF_API void Test::OnStart ( ThreadInfo& ) { internal::g_absentMethods |= internal::idOnStart; }

__TBB_PERF_API void Test::OnFinish ( ThreadInfo& ) { internal::g_absentMethods |= internal::idOnFinish; }

__TBB_PERF_API void WipeCaches () { internal::NativeParallelFor( NumCpus, internal::WiperBody() ); }

__TBB_PERF_API void EmptyFunc () {}
__TBB_PERF_API void AnchorFunc ( void* ) {}
__TBB_PERF_API void AnchorFunc2 ( void*, void* ) {}

__TBB_PERF_API void SetWorkloadName( const char* format, ... ) {
    internal::WorkloadName[MaxWorkloadNameLen] = 0;
    va_list args;
    va_start(args, format);
    vsnprintf( internal::WorkloadName, MaxWorkloadNameLen, format, args );
    va_end(args);
}


__TBB_PERF_API int TestMain( int argc, char* argv[], const SessionSettings* defaultSettings ) {
#if _MSC_VER
    HANDLE hMutex = CreateMutex( NULL, FALSE, "Global\\TBB_OMP_PerfSession" );
    WaitForSingleObject( hMutex, INFINITE );
#endif
    MinThread = MaxThread = NumCpus;
    if ( defaultSettings )
        theSettings = *defaultSettings;
    ParseCommandLine( argc, argv );  // May override data in theSettings

    internal::PrepareTests ();
    internal::RunTests ();
    internal::PrintResults();
    Report("\n");
#if _MSC_VER
    ReleaseMutex( hMutex );
    CloseHandle( hMutex );
#endif
    return 0;
}

} // namespace Perf
