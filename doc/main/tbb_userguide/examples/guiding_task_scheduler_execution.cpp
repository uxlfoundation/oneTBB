/*
    Copyright (c) 2025 Intel Corporation
    Copyright (c) 2025 UXL Foundation Contributors

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

#include <vector>
#include <oneapi/tbb/info.h>
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/task_group.h>

void set_numa_node_example() {
/*begin_set_numa_node_example*/
    std::vector<tbb::numa_node_id> numa_nodes = tbb::info::numa_nodes();
    std::vector<tbb::task_arena> arenas(numa_nodes.size());
    std::vector<tbb::task_group> task_groups(numa_nodes.size());

    // Since the library creates one less worker thread than the number of total cores,
    // all but one of the arenas are created without reserving a slot for an external thread,
    // allowing them to be fully populated.
    for(unsigned j = 1; j < numa_nodes.size(); j++) {
        arenas[j].initialize(tbb::task_arena::constraints(numa_nodes[j]), /*reserved_slots*/ 0);
        arenas[j].enqueue([](){/*some parallel work*/}, task_groups[j]);
    }

    // The main thread executes in the arena with the reserved slot.
    arenas[0].initialize(tbb::task_arena::constraints(numa_nodes[0]));
    arenas[0].execute([&task_groups](){
        task_groups[0].run([](){/*some parallel work*/});
    });

    for(unsigned j = 0; j < numa_nodes.size(); j++) {
        arenas[j].wait_for(task_groups[j]);
    }
/*end_set_numa_node_example*/
}

int main() {
    set_numa_node_example();
    return 0;
}
