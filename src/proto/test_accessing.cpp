/*
    Copyright (C) 2005-2011, Intel Corporation

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

#define TBB_USE_ASSERT 1
#define HARNESS_CUSTOM_MAIN 1

struct debug_point {
    const char *file, *func;
    int line;//TODO: add global point number
    debug_point(const char *f, const char *m, int l) : file(f), func(m), line(l) {}
    void test_race(const char *f = 0);
    template<typename T> void test_race(const char *f, T arg );
    template<typename T, typename U> void test_race(const char *f, T arg0, U arg1);
};
#define __TBB_ASSERT_RACEPOINT(args) {static ::debug_point point(__FILE__, __FUNCTION__, __LINE__); point.test_race args;}
//TODO:CHECKPOINT
//TODO:use __PRETTY_FUNCTION__ for GCC and __FUNCDNAME__ or __FUNCSIG__ for VC
#include "proto/synchronizer.h"

#include "tbb/task_scheduler_init.h"
#include "tbb/aligned_space.h"
#include "tbb/enumerable_thread_specific.h"
#include "harness.h"
#include "harness_barrier.h"
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <vector>
using namespace tbb;
//using namespace std;

static tbb::atomic<size_t> trace_counter;
static const size_t trace_buffer_size = 1<<16;
struct trace_record {
    int thread;
    const debug_point *point;
    const char* type;
    const char* msgf;
    long word[4];
} trace_buffer[trace_buffer_size];

#define __TBB_TRACE(tid, msg) {static ::debug_point point(__FILE__, __FUNCTION__, __LINE__); trace_event(tid, point, "trace", msg, 0, 0);}

template<typename T, typename U>
inline void trace_event( int thread, const debug_point &dp, const char* t, const char* f = 0, T arg0 = 0, U arg1 = 0 ) {
    trace_record &r = trace_buffer[trace_counter++ % trace_buffer_size];
    r.thread = thread;
    r.point = &dp;
    r.type = t;
    r.msgf = f;
    r.word[0] = arg0;
    r.word[1] = arg1;
}
void trace_dump(FILE *f, size_t from = 0, size_t stop = 0) {
    if(!stop) stop = (from-1)%trace_buffer_size;
    for(size_t i = from; i != stop; i = (i+1)%trace_buffer_size) {
        if(i == trace_counter) fputs("->\n", f);
        trace_record &r = trace_buffer[i];
        if(!r.point) continue;
        for(int n = r.thread; n > 0; n--) fprintf(f, "\t\t");
        fprintf(f, "#%d %s:%d ", r.thread, r.point->func, r.point->line);
        if(r.msgf) fprintf(f, r.msgf, r.word[0], r.word[1]);
        if(r.type) fprintf(f, " %s", r.type);
        fputs("\n", f);
    }
}

struct race_context {
    int thread, point;
    unsigned long path;
};
static enumerable_thread_specific<race_context> tls;
// random race scheduler
struct race_scheduler {
    static bool is_active;
    static Harness::SpinBarrier barrier;

    race_scheduler() {
    #if __APPLE__
        srandomdev();
    #elif __linux__
        if(Verbose) puts("Warning: using poor random number generator");
        //todo: read /dev/random
        srandom(clock());
    #else
    #   define random() rand()
        srand(clock());
        if(Verbose) puts("Warning: using poor random number generator");
    #endif
    }
    
    template<typename Test>
    static void execute(int nthreads, const Test &test) {
        race_context &rc = tls.local(); // reset main thread
        rc.thread = -1; rc.path = 0; rc.point = 0;
        barrier.initialize( nthreads );
        is_active = true;
        NativeParallelFor(nthreads, test);
        is_active = false;
    }
    static void open_race_hole() {
    #if __APPLE__ || __linux__
        usleep(1);
    #else
        __TBB_Yield();
    #endif
    }
    static bool next_iteration(int i, int t) {
        ASSERT(is_active, 0);
        if(i >= 10000) return false;
        race_context &rc = tls.local();
        rc.thread = t;
        rc.point = 0;
        rc.path = random();
        if(Verbose && !t && i % 1000 == 0) {
            printf("iteration %d\n", i);
        }
        __TBB_TRACE(t, "finished");
        barrier.wait();
        if(!t) trace_counter = 0;
        barrier.wait();
        return true;
    }
    static void test_race(const debug_point &dp, const char *f = NULL, long arg0 = 0, long arg1 = 0) {
        if(!is_active) return;
        race_context &rc = tls.local();
        int p = rc.point++%(sizeof(long)*8);
        bool halt = rc.path&(1<<p);
        if( halt )
            open_race_hole();
        trace_event(rc.thread, dp, halt?"- sleeped":0, f, arg0, arg1);
    }
    static void print_context() {
        if(!is_active) { puts("Serial context"); return; }
        __TBB_TRACE(tls.local().thread, "aborted");
        puts("Race context:");
        trace_dump(stdout, 0, trace_counter);
    }
} init_rs;
bool race_scheduler::is_active = false;
Harness::SpinBarrier race_scheduler::barrier(0);

void debug_point::test_race(const char *f) {
    race_scheduler::test_race(*this, f, 0, 0 );
}
template<typename T>
void debug_point::test_race(const char *f, T arg) {
    race_scheduler::test_race(*this, f, long(arg), 0 );
}
template<typename T, typename U>
void debug_point::test_race(const char *f, T arg0, U arg1) {
    race_scheduler::test_race(*this, f, long(arg0), long(arg1) );
}

/////////////////////////////////////////////////////////////////////

template<typename T>
bool is_const(T = T()) {
    return access_tag_traits<T>::is_const;
}

void test_access_tags() {
    // Using access levels here - due to <cstdio> included
    using tbb::access;
    ASSERT( is_const( access::const_shared() ), 0);
    ASSERT(!is_const( access::shared() ), 0);
    ASSERT( is_const<const access::serial>(), 0);
    ASSERT(!is_const<access::serial>(), 0);
    //...
}

template<typename Synchronizer>
struct test_synchronizer {
    // types assert
    typedef typename Synchronizer::version_type *ver_ptr;
    typedef typename Synchronizer::template memento<tbb::access::none>::state_type memento_none;
    typedef typename Synchronizer::template memento<tbb::access::serial>::state_type memento_serial;
    typedef typename Synchronizer::template memento<tbb::access::preserve>::state_type memento_preserve;
    typedef typename Synchronizer::template memento<tbb::access::concurrent>::state_type memento_concurrent;
    typedef typename Synchronizer::template memento<typename Synchronizer::access_traits::default_access_type>::state_type memento_default;
    typedef typename Synchronizer::template memento<typename Synchronizer::access_traits::const_access_type>::state_type memento_const;
    // declarations above are really MONSTROUS... :(

    void serial_compliance() {
        // Using access levels here - due to <cstdio> included
        using tbb::access;
        ASSERT(!is_const( Synchronizer::access_traits::default_access() ), 0);
        ASSERT( is_const( Synchronizer::access_traits::const_access() ), 0);
        memento_none mn = access::none();
        memento_serial ms = access::serial();
        memento_preserve mp = access::preserve();
        memento_concurrent me = access::exclusive();
        memento_concurrent mh = access::shared();
        memento_default md = Synchronizer::access_traits::default_access();
        memento_const mc = Synchronizer::access_traits::const_access();

        Synchronizer litex( mp ); // lifetime exclusive..?
        litex.init_lock( me ); // common initialization sequence

        // fake locking
        litex.init_lock( mn );
        ASSERT( litex.try_lock( mn ) == access::timeout, 0 );
        ASSERT( litex.blocking_try_lock( mn ) == access::timeout, 0 );
        ASSERT(!litex.unlock( mn ), 0 );
        litex.init_lock( ms );
        ASSERT( litex.try_lock( ms ) == access::acquired, 0 );
        ASSERT( litex.blocking_try_lock( ms ) == access::acquired, 0 );
        ASSERT(!litex.unlock( ms ), 0 );

        // real locking
        memento_preserve mp1 = access::preserve();
        if( Synchronizer::access_traits::has_multiple_owners_support ) {
            ASSERT( litex.try_lock( mp1 ) == access::acquired, 0 );
            ASSERT(!litex.unlock( mp1 ), 0);
        } else ASSERT( litex.try_lock( mp1 ) == access::timeout, 0 );

        if(!Synchronizer::access_traits::has_exclusive_access_support &&
            !Synchronizer::access_traits::has_shared_access_support ) {
            // check dooming by last owner
            ASSERT( litex.unlock( mp ), 0);
            return; // nothing to test any more
        }

        memento_concurrent me1 = access::exclusive();
        if(!Synchronizer::access_traits::has_exclusive_access_support ) {
            ASSERT( litex.try_lock( me1 ) == access::acquired, 0 );
            ASSERT(!litex.unlock( me1 ), 0);
        } else ASSERT( litex.try_lock( me1 ) == access::busy, 0 );
        ASSERT( litex.try_lock( mh ) == access::busy, 0 );

        if( Synchronizer::access_traits::has_shared_access_support ) {
            ASSERT(!litex.unlock( me ), 0);
            memento_concurrent mh1 = access::shared();
            ASSERT( litex.try_lock( mh ) == access::acquired, 0 );
            ASSERT( litex.try_lock( mh1 ) == access::acquired, 0 );
            ASSERT(!litex.unlock( mh ), 0);
            ASSERT( litex.try_lock( me ) == access::busy, 0 );
            ASSERT(!litex.unlock( mh1 ), 0);
            ASSERT( litex.try_lock( me ) == access::acquired, 0 );
        }
        ASSERT( litex.is_locked() && litex.is_locked(access::exclusive()) && litex.is_locked(access::preserve()), 0 );
        ASSERT(!litex.unlock( me ) && !litex.is_locked(access::exclusive()) && !litex.is_doomed(), 0);
        if( Synchronizer::access_traits::has_multiple_owners_support ) {
            int i; // assume, the lock can be acquired on the same memento instance
            for(i = 0; litex.try_lock( mp1 ) == access::acquired; i++ );
            for(int j = 0; j < 10000; j++) ASSERT( litex.try_lock( mp1 ) == access::busy, 0 ); // no exceptions
            for(int j = 0; j < i; j++) ASSERT( !litex.unlock( mp1 ), 0 );
        }
        // check dooming by last owner
        ASSERT( litex.unlock( mp ), 0);
        ASSERT(!litex.is_locked(access::preserve()) && litex.is_doomed(), 0 );
        //TODO: should access::serial return access::doomed on such instances?
        ASSERT( litex.try_lock( mp ) == access::doomed, 0 );
        ASSERT( litex.try_lock( md ) == access::doomed, 0 );
        ASSERT( litex.try_lock( mc ) == access::doomed, 0 );
        // resurect the instance
        ASSERT( litex.try_lock( litex.reset( me ) ) == access::busy, 0);
        litex.recycle();
        ASSERT( litex.try_lock( mp ) == access::doomed, 0 );
        ASSERT( litex.try_lock( me ) == access::doomed, 0 );
        ASSERT( litex.try_lock( litex.reset( me ) ) == access::acquired, 0);
        litex.init_lock( mp );
        ASSERT( litex.is_locked(access::exclusive()) && litex.is_locked(access::preserve()), 0 );
        ASSERT(!litex.unlock( mp ) && !litex.is_locked(access::preserve()) && !litex.is_doomed(), 0);
        // check dooming by last lock
        ASSERT( litex.unlock( me ), 0);
        ASSERT( litex.is_locked(access::exclusive()) && litex.is_doomed(), 0 );
        ASSERT( litex.try_lock( mp ) == access::doomed, 0 );
        litex.recycle();
        ASSERT(!litex.is_locked(access::exclusive()) && litex.is_doomed(), 0 );
        ASSERT( litex.try_lock( me ) == access::doomed, 0 );
    }

    struct TestActor: NoAssign {
        Synchronizer &sync;
        int &counter;
        tbb::atomic<int> &frags;
        TestActor( Synchronizer &sync_, int &c, tbb::atomic<int> &f)
            : sync(sync_), counter(c), frags(f) {}
        /** Increments counter once for each iteration in the iteration space. */
        void operator()( int thread ) const {
            using tbb::access;
            memento_concurrent mex = access::exclusive();
            memento_preserve mp = access::preserve();
            access::result_t r;
            for(int i = 0; race_scheduler::next_iteration(i, thread); i++) {
                __TBB_ASSERT_RACEPOINT(());
                while( (r = sync.blocking_try_lock( mex )) == access::doomed ) {
                    __TBB_ASSERT_RACEPOINT(("lock doomed"));
                    if( (r = sync.try_lock( sync.reset( mex ) )) == access::acquired ) {
                        frags--;
                        break;
                    }
                }
                ASSERT( r == access::acquired, 0 );
                int c = counter;
                race_scheduler::open_race_hole();
                counter = c + 1;
                __TBB_ASSERT_RACEPOINT(("lock acquired"));
                if( sync.unlock( mex ) ) {
                    __TBB_ASSERT_RACEPOINT(("unlock doomed"));
                    sync.recycle();
                    frags++;
                }
                if( Synchronizer::access_traits::has_multiple_owners_support ) {
                    __TBB_ASSERT_RACEPOINT(("unlocked"));
                    while( (r = sync.try_lock( mp )) == access::doomed ) {
                        __TBB_ASSERT_RACEPOINT(("attach doomed"));
                        if( (r = sync.try_lock( sync.reset( mp ) )) == access::acquired ) {
                            frags--;
                            break;
                        }
                    }
                    ASSERT( r == access::acquired, 0 ); // it's not amount stress, must pass without access::busy
                    __TBB_ASSERT_RACEPOINT(("attached"));
                    ASSERT( sync.move_to( mex, mp ), 0 );
                    int c = counter;
                    race_scheduler::open_race_hole();
                    counter = c - 1;
                    __TBB_ASSERT_RACEPOINT(("locked from owner"));
                    ASSERT( sync.move_to( mp, mex ), 0 );
                    __TBB_ASSERT_RACEPOINT(("attached from lock"));
                    if( sync.unlock( mp ) ) {
                        __TBB_ASSERT_RACEPOINT(("detach doomed"));
                        sync.recycle();
                        frags++;
                    }
                } else ASSERT( false, "test is not ready for such cases");
            }
        }
    };

    void concurrent_correctness() {
        aligned_space<Synchronizer, 1> sync;
        memset(sync.begin(), 0, sizeof(sync)); // check for zerro-initialization
        ASSERT( sync.begin()->is_doomed(), 0);
        int counter = 0;
        tbb::atomic<int> frags; frags = 0; // dead/live phases
        race_scheduler::execute( 4, TestActor  (*sync.begin(), counter, frags) );
        ASSERT( !counter, "Atomicity of accesses is broken");
        ASSERT( !frags, "Odd lifitime operation");
        ASSERT( sync.begin()->is_doomed(), 0);
    }
};

#if _POSIX_VERSION
#include <signal.h>
int main(int argc, char** argv) {
    signal(SIGTSTP, reinterpret_cast<void (*)(int)>(race_scheduler::print_context));
#else
int main(int argc, char** argv) {
#endif
    ParseCommandLine(argc, argv);
    SetHarnessErrorProcessing(race_scheduler::print_context);
    set_assertion_handler(ReportError);
#if __TBB_BTS
    printf("Using BTS\n");
#endif
    test_access_tags();
    test_synchronizer<versioned_synchronizer> versioned;
    versioned.serial_compliance();
    versioned.concurrent_correctness();
    puts("done");
}
