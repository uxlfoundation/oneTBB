#include "oneapi/tbb.h"


using namespace oneapi::tbb;


#pragma warning(disable: 588)


void ParallelApplyFoo(float a[], size_t n) {
    parallel_for(size_t(0), n, [=](size_t i) {Foo(a[i]);});
}
