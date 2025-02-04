#define TBB_PREVIEW_MEMORY_POOL 1
#include "oneapi/tbb/memory_pool.h"

int main() {
    char buf[1024*1024];
    oneapi::tbb::fixed_pool my_pool(buf, 1024*1024);

    void* my_ptr = my_pool.malloc(10);
    my_pool.free(my_ptr);
}
