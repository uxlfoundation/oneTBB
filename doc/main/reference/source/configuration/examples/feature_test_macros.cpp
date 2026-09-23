/*
    Copyright (c) 2026 UXL Foundation Contributors

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

/*begin_feature_test_macros_example*/

#define TBB_PREVIEW_TASK_ARENA_CORE_TYPE_SELECTOR 1

#include <oneapi/tbb/version.h>
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/parallel_for.h>

#include <tuple>

int main() {
#if TBB_HAS_TASK_ARENA_CORE_TYPE_SELECTOR
    // Prefer the most performant core types when the feature is available
    tbb::task_arena::constraints c;
    c.set_core_type(tbb::task_arena::selectable);
    auto selector = [](std::tuple<tbb::core_type_id, std::size_t, std::size_t> core_type) -> int {
        auto index = std::get<1>(core_type);
        auto total = std::get<2>(core_type);
        // Exclude the least performant type when there is more than one;
        // rank the rest by index (higher index = higher score).
        return (total > 1 && index == 0) ? -1 : static_cast<int>(index);
    };
    tbb::task_arena arena(c, selector);
#else
    tbb::task_arena arena;
#endif

    arena.execute([] {
        tbb::parallel_for(0, 1000, [](int) { /* computation */ });
    });
}

/*end_feature_test_macros_example*/
