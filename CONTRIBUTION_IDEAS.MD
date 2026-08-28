<!--
******************************************************************************
* 
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/-->

# oneTBB Contribution Ideas
This file acts as a repository for potential contributions to the project.
It provides a centralized location where contributors can find tasks and features to work on,
helping to enhance the project. Its main purpose is to guide contributors towards areas where
they can make a positive impact on the project's development.

## Complexity Brackets
Tasks in this repository are categorized into three complexity brackets: Beginner, Intermediate, and Expert.
Each task is labeled with the appropriate complexity bracket to indicate the level of difficulty.

- Beginner:These tasks are suitable for contributors who are new to the project or have limited experience.
They involve relatively simple concepts and can be a good starting point for learning and getting familiar with the codebase.

- Intermediate: These tasks require a moderate level of knowledge and experience.
They may involve more complex features or require understanding of specific areas of the project.
Contributors with some experience in the project can take on these tasks to further develop their skills.

- Expert: These tasks are challenging and require advanced knowledge and expertise.
They often involve complex algorithms, performance optimizations, or deep understanding of the project's internals.
Contributors with extensive experience in the project can tackle these tasks to push the boundaries of the project's capabilities.

When choosing a task to work on, consider your skill level and the complexity bracket that best matches your expertise.
This will help ensure that you can make meaningful contributions and continue to grow as a developer.

## Beginner

### Cleanup the code base from redundant macros.
After the revamp from TBB to oneTBB, many macros were removed. However, some of them are still being used in the codebase.
Retrieve a list of macros used in the library and clean up any redundant ones.

### Introduce new examples.
You have the option to contribute a new example that you deem significant.
Alternatively, you can check library APIs that have not yet been covered and provide an example that addresses them.
Any example you provide should adhere to the style of the existing benchmarks.

### Stabilize examples performance.
Several oneTBB examples serve as performance benchmarks for the library. It's recommended to check the relative error for these examples.
If the error exceeds 5%, consider stabilizing the example to improve the reproducibility of results, while still maintaining a reasonable execution time.

## Intermediate

### C++ 20 Modules
With the introduction of C++ 20 Modules, it is worth analyzing the applicability of this feature to the oneTBB library.
Modules provide a more efficient and scalable way of organizing and managing code dependencies.
To get started, you can refer to the C++ 20 Modules documentation and familiarize yourself with the syntax and concepts.
Then, analyze the oneTBB codebase and propose a modularization strategy that aligns with the library's architecture.
Please do not forget to document your finding into oneTBB discussion page.

### NUMA related improvements
The oneTBB scheduler, by default, does not incorporate platform characteristics into its task scheduling.
You might want to enhance the scheduler's logic to take into account these platform characteristics.
For instance, you could refer to Hierarchical task stealing (#523) as a starting point.

## Expert

### Graceful degradation on thread creation failure.
On many Unix-like systems, `pthread_create` can fail spuriously even if
the running machine has enough resources to spawn a new thread (#824).
Consider reworking the thread creation process and avoid calling `std::terminate` if
`pthread_create` has failed. One question to consider is how to handle `task_arena::enqueue` guarantees.

