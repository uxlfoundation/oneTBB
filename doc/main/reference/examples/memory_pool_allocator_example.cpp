#define TBB_PREVIEW_MEMORY_POOL 1
#include "oneapi/tbb/memory_pool.h"
#include <list>

int main() {
    oneapi::tbb::memory_pool<std::allocator<int>> my_pool;

    typedef oneapi::tbb::memory_pool_allocator<int> pool_allocator_t;
    std::list<int, pool_allocator_t> my_list(pool_allocator_t{my_pool});

    my_list.emplace_back(1);
}
