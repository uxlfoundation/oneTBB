#define TBB_PREVIEW_MEMORY_POOL 1
#include "oneapi/tbb/memory_pool.h"

int main() {
    oneapi::tbb::memory_pool<std::allocator<char>> my_pool;

    void* my_ptr = my_pool.malloc(10);
    my_pool.free(my_ptr);
}
