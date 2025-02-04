#include "oneapi/tbb.h"


using namespace oneapi::tbb;


void ParallelApplyFoo( float* a, size_t n ) {
    parallel_for( blocked_range<size_t>(0,n),
        [=](const blocked_range<size_t>& r) {
                        for(size_t i=r.begin(); i!=r.end(); ++i)
                            Foo(a[i]);
                    }
    );
}
