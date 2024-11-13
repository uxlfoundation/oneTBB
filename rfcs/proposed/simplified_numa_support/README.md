# Simplified NUMA support in oneTBB

## Introduction

In Non-Uniform Memory Access (NUMA) systems, the cost of memory accesses depends on the
*nearness* of the processor to the memory resource on which the accessed data resides. 
While oneTBB has core support that enables developers to tune for Non-Uniform Memory 
Access (NUMA) systems, we believe this support can be simplified and improved to provide 
an improved user experience.  

This early proposal recommends addressing four areas for improvement:

1. improved reliability of HWLOC-dependent topology and pinning support in,
2. addition of a NUMA-aware allocation,
3. simplified approaches to associate task distribution with data placement and 
4. where possible, improved out-of-the-box performance for high-level oneTBB features.

We expect that this draft proposal may be broken into smaller proposals based on feedback 
and prioritization of the suggested features.

The features for NUMA tuning already available in the oneTBB 1.3 specification include:

- Functions in the `tbb::info` namespace **[info_namespace]** 
  - `std::vector<numa_node_id> numa_nodes()`
  - `int default_concurrency(numa_node_id id = oneapi::tbb::task_arena::automatic)`
- `tbb::task_arena::constraints` in **[scheduler.task_arena]**

Below is the example that demonstrates the use of these APIs to pin threads to different 
arenas to each of the NUMA nodes available on a system, submit work across those `task_arena` 
objects and into associated `task_group`` objects, and then wait for work again using both 
the `task_arena` and `task_group` objects.

    #include "oneapi/tbb/task_group.h"
    #include "oneapi/tbb/task_arena.h"

    #include <vector>

    int main() {
        std::vector<oneapi::tbb::numa_node_id> numa_nodes = oneapi::tbb::info::numa_nodes();
        std::vector<oneapi::tbb::task_arena> arenas(numa_nodes.size());
        std::vector<oneapi::tbb::task_group> task_groups(numa_nodes.size());

        // Initialize the arenas and place memory
        for (int i = 0; i < numa_nodes.size(); i++) {
            arenas[i].initialize(oneapi::tbb::task_arena::constraints(numa_nodes[i]));
            arenas[i].execute([i] {
              // allocate/place memory on NUMA node i
            });
        }
        
        for (int j 0; j < NUM_STEPS; ++i) {

          // Distribute work across the arenas / NUMA nodes
          for (int i = 0; i < numa_nodes.size(); i++) {
            arenas[i].execute([&task_groups, i] {
              task_groups[i].run([] {
                /* executed by the thread pinned to specified NUMA node */
              });
            });
          }

          // Wait for the work in each arena / NUMA node to complete
          for (int i = 0; i < numa_nodes.size(); i++) {
            arenas[i].execute([&task_groups, i] {
                task_groups[i].wait();
            });
          }
        }

        return 0;
    }

### The need for application-specific knowledge

In general when tuning a parallel application for NUMA systems, the goal is to expose sufficient
parallelism while minimizing (or at least controlling) data access and communication costs. The 
tradeoffs involved in this tuning often rely on application-specific knowledge. 

In particular, NUMA tuning typically involves:

1. Understanding the overall application problem and its use of algorithms and data containers
2. Placement of data container objects onto memory resources
3. Distribution of tasks to hardware resources that optimize for data placement

As shown in the previous example, the oneTBB 1.3 specification only provides low-level
support for NUMA optimization. The `tbb::info` namespace provides topology discovery. And the
combination of `task_arena`, `task_arena::constraints` and `task_group` provide a mechanism for
placing tasks onto specific processors. There is no high-level support for memory allocation
or placement, or for guiding the task distribution of algorithms.

### Issues that should be resolved in the oneTBB library

**The behavior of existing features is not always predictable.** There is a note in 
section **[info_namespace]** of the oneTBB specification that describes
the function `std::vector<numa_node_id> numa_nodes()`, "If error occurs during system topology 
parsing, returns vector containing single element that equals to `task_arena::automatic`."  

In practice, the error often occurs because HWLOC is not detected on the system. While the 
oneTBB documentation states in several places that HWLOC is required for NUMA support and 
even provides guidance on 
[how to check for HWLOC](https://www.intel.com/content/www/us/en/docs/onetbb/get-started-guide/2021-12/next-steps.html), 
the failure to resolve HWLOC at runtime silently returns a default of `task_arena::automatic`. This
default does not pin threads to NUMA nodes. It is too easy to write code similar to the preceding 
example and be unaware that a HWLOC installation error (or lack of HWLOC) has undone all your effort.

**Getting good performance using these tools requires notable manual coding effort by users.** As we 
can see in the preceding example, if we want to spread work across the NUMA nodes in 
a system we need to query the topology using functions in the `tbb::info` namespace, create
one `task_arena` per NUMA node, along with one `task_group` per NUMA node, and then add an
extra loop that iterates over these `task_arena` and `task_group` objects to execute the
work on the desired NUMA nodes. We also need to handle all container allocations using OS-specific
APIs (or behaviors, such as first-touch) to allocator or place them on the appropriate NUMA nodes.

**The out-of-the-box performance of the generic TBB APIs on NUMA systems is not good enough.**
Should the oneTBB library do anything special by default if the system is a NUMA system?  Or should 
regular random stealing distribute the work across all of the cores, regardless of which NUMA first 
touched the data?

Is it reasonable for a developer to expect that a series of loops, such as the ones that follow, will
try to create a NUMA-friendly distribution of tasks so that accesses to the same elements of `b` and `c`
in the two loops are from the same NUMA nodes? Or is this too much to expect without providing hints? 

    tbb::parallel_for(0, N, 
      [](int i) { 
        b[i] = f(i);
        c[i] = g(i); 
      });

    tbb::parallel_for(0, N, 
      [](int i) { 
        a[i] = b[i] + c[i]; 
      });

## Proposal

### Increased availability of NUMA support

The oneTBB 1.3 specification states for `tbb::info::numa_nodes`, "If error occurs during system 
topology parsing, returns vector containing single element that equals to task_arena::automatic."

Since the oneTBB library dynamically loads the HWLOC library, a misconfiguration can cause the HWLOC
to fail to be found. In that case, a call like:

    std::vector<oneapi::tbb::numa_node_id> numa_nodes = oneapi::tbb::info::numa_nodes();

will return a vector with a single element of `task_arena::automatic`. This behavior, as we have noticed
through user questions, can lead to unexpected performance from NUMA optimizations. When running
on a NUMA system, a developer that has not fully read the documentation may expect that `numa_nodes()`
will give a proper accounting of the NUMA nodes. When the code, without raising any alarm, returns only 
a single, valid element due to the environmental configuation (such as lack of HWLOCK), it is too easy 
for developers to not notice that the code is acting in a valid, but unexpected way.

We propose that the oneTBB library implementation include, wherever possibly, a statically-linked fallback 
to decrease that likelihood of such failures. The oneTBB specification will remain unchanged.

### NUMA-aware allocation

We will define allocators of other features that simplify the process of allocating or places data onto
specific NUMA nodes.

### Simplified approaches to associate task distribution with data placement

As discussed earlier, NUMA-aware allocation is just the first step in optimizing for NUMA architectures.
We also need to deliver mechanisms to guide task distribution so that tasks are executed on execution
resources that are near to the data they access. oneTBB already provides low-level support through
`tbb::info` and `tbb::task_arena`, but we should up-level this support into the high-level algorithms,
flow graph and containers where appropriate.

### Improved out-of-the-box performance for high-level oneTBB features.

For high-level oneTBB features that are modified to provide improved NUMA support, we should try to 
align default behaviors for those features with user-expectations when used on NUMA systems.

## Open Questions

1. Do we need simplified support, or are users that want NUMA support in oneTBB
willing to, or perhaps even prefer, to manage the details manually?
2. Is it reasonable to expect good out-of-the-box performance on NUMA systems 
without user hints or guidance.
