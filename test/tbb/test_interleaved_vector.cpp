#include "common/test.h"

#include "tbb/interleaved_vector.h"


TEST_CASE("test basics") {
    size_t ELEMS = 1000*1000;

    tbb::interleaved_vector<unsigned, 4*1024> iv(2, ELEMS);

    for (size_t i = 0; i < ELEMS; ++i)
        iv.get()[i] = i;
    for (size_t i = 0; i < ELEMS; ++i)
        REQUIRE(iv.get()[i] == i);
}
