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

#include "oneapi/tbb/global_control.h"
#include "oneapi/tbb/task_arena.h"
#include "oneapi/tbb/parallel_for.h"

void other_runtime_stage();

/*begin_arena_leave_policy_example*/
void tbb_stage() {
    oneapi::tbb::parallel_for(0, 1000, [](int) { /* computation */ });
}

void per_arena_leave_policy() {
    oneapi::tbb::task_arena ta(oneapi::tbb::task_arena::automatic, 1,
                               oneapi::tbb::task_arena::priority::normal,
                               oneapi::tbb::task_arena::leave_policy::fast);
    ta.execute([] { tbb_stage(); });
    // Worker threads have left the arena and do not compete with the other runtime
    other_runtime_stage();
}
/*end_arena_leave_policy_example*/

/*begin_global_leave_policy_example*/
void global_leave_policy() {
    oneapi::tbb::global_control gc(oneapi::tbb::global_control::leave_policy,
                                   oneapi::tbb::task_arena::leave_policy::fast);
    for (int i = 0; i < 10; ++i) {
        tbb_stage();    // uses the implicit arena, which now has fast leave
        other_runtime_stage();
    }
}
/*end_global_leave_policy_example*/

void other_runtime_stage() { /* computation with another threading runtime */ }

int main() {
    per_arena_leave_policy();
    global_leave_policy();
}
