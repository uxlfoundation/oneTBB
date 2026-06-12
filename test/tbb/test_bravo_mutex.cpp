#include "test_mutex.h"
#include <oneapi/tbb/rw_mutex.h>
#include <oneapi/tbb/bravo_rw_mutex.h>

using BRAVO_rw_mutex = tbb::detail::d1::BRAVO_rw_mutex<tbb::rw_mutex>;

TEST_CASE("BRAVO_rw_mutex: writers-only counter") {
    for (std::size_t p : utils::concurrency_range()) {
        test_with_native_threads::test_basic<BRAVO_rw_mutex>(p);
    }
}

template <typename M, long N>
struct ReadWriteInvariant : utils::NoAssign {
    using mutex_type = M;
    M mutex;
    long value[N];

    ReadWriteInvariant() {
        for (long k = 0; k < N; ++k) {
            value[k] = 0;
        }
    }

    void update() {
        for (long k = 0; k < N; ++k) {
            ++value[k];
        }
    }

    bool value_is(long expected) const {
        for (long k = 0; k < N; ++k) {
            if (value[k] != expected) return false;
        }
        return true;
    }

    void flog_once(std::size_t mode) {
        bool write = (mode % 8) == 7;
        typename mutex_type::scoped_lock lock(mutex, write);
        if (write) {
            update();
        } else {
            REQUIRE(value_is(value[0]));
        }
    }
};

TEST_CASE("BRAVO_rw_mutex: reader/writer invariant") {
    for (std::size_t p : utils::concurrency_range()) {
        ReadWriteInvariant<BRAVO_rw_mutex, 8> inv;
        test_with_native_threads::Order = 0;
        
        utils::NativeParallelFor(p, test_with_native_threads::Work<decltype(inv), test_with_native_threads::TEST_SIZE>(inv));
        REQUIRE(inv.value_is(test_with_native_threads::TEST_SIZE / 8));
    }
}