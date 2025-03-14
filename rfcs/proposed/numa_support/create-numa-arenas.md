# API to simplify creation of task arenas constrained to NUMA nodes

This sub-RFC proposes an API to ease creation of a one-per-NUMA-node set of task arenas.

## Introduction

The code example in the [overarching RFC for NUMA support](README.md) shows the likely
pattern of using task arenas to distribute computation across all NUMA domains on a system.
Let's take a closer look at the part where arenas are created and initialized.

```c++
  std::vector<tbb::numa_node_id> numa_nodes = tbb::info::numa_nodes();
  std::vector<tbb::task_arena> arenas(numa_nodes.size());
  std::vector<tbb::task_group> task_groups(numa_nodes.size());

  // initialize each arena, each constrained to a different NUMA node
  for (int i = 0; i < numa_nodes.size(); i++)
    arenas[i].initialize(tbb::task_arena::constraints(numa_nodes[i]), 0);
```

The first line obtains a vector of NUMA node IDs for the system. Then, a vector of the same size
is created to store `tbb::task_arena` objects, each constrained to one of the NUMA nodes.
Another vector holds `task_group` instances used later to submit and wait for completion
of the work in each of the arenas - it is necessary because `task_arena` does not provide
any work synchronization API. Finally, the loop over all NUMA nodes initializes associated
task arenas with proper constraints.

While not incomprehensible, the code is quite verbose and arguably too explicit for the typical scenario
of creating a set of arenas across all available NUMA domains. There is also risk of subtle issues.
The default constructor of `task_arena` reserves a slot for an application thread. The arena initialization
at the last line explicitly overwrites it to 0 to allow TBB worker threads taking all the slots, however
this nuance might be unknown and easy to miss, potentially resulting in underutilization of CPU resources.

## Proposal

We propose to introduce a special function to create the set of task arenas, one per NUMA node on the system.
The initialization code equivalent to the example above would be:

```c++
  std::vector<tbb::task_arena> arenas = tbb::create_numa_task_arenas();
  std::vector<tbb::task_group> task_groups(arenas.size());
```

### Public API

The function has the following signature:

```c++
// Defined in tbb/task_arena.h

namespace tbb {
    std::vector<tbb::task_arena> create_numa_task_arenas(
        task_arena::constraints other_constraints = {},
        unsigned reserved_slots = 0
    };
}
```

It optionally takes a `constraints` argument to change default arena settings such as maximal concurrency
(the upper limit on the number of threads), core type etc.; the `numa_id` value in `other_constraints`
is ignored. The second optional argument allows to override the number of reserved slots, which by default
is 0 (unlike the `task_arena` construction default of 1) for the reasons described in the introduction.

The function returns a `std::vector` of created arenas. The arenas should not be initialized,
in order to allow changing certain arena settings before the use.

### Possible implementation

```c++
std::vector<tbb::task_arena> create_numa_task_arenas(
    tbb::task_arena::constraints other_constraints,
    unsigned reserved_slots)
{
    std::vector<tbb::numa_node_id> numa_nodes = tbb::info::numa_nodes();
    std::vector<tbb::task_arena> arenas;
    arenas.reserve(numa_nodes.size());
    for (tbb::numa_node_id nid : numa_nodes) {
        other_constraints.numa_id = nid;
        arenas.emplace_back(other_constraints, reserved_slots);
    }
    return arenas;
}
```

## Considered alternatives

The earlier proposal [PR #1559](https://github.com/uxlfoundation/oneTBB/pull/1559) suggested to add
a new class derived from `task_arena` which would also provide a method to wait for work completion.
It aims to simplify the whole usage example as much as possible. That proposal also includes
a factory function to create a set of such arenas constrained by NUMA domains.

While suggesting very concise and "bullet-proof" API for the problem at question, that approach has
its downsides:
- Adding a special "flavour" of `task_arena` potentially increases the library learning curve and
  might create confusion about which class to use in which conditions, and how these interoperate.
- It seems very specialized, capable to only address specific and quite narrow set of use cases.

Our proposal, instead, aims at a single usability aspect with incremental improvements/extensions
to the existing oneTBB classes and usage patterns, leaving other aspects to complementary proposals.
Specifically, the [Waiting in a task arena](../task_arena_waiting/readme.md) RFC improves the joint
use of a task arena and a task group to submit and wait for work. Combined, these extensions will
provide only a slightly more verbose solution for the NUMA use case, while being more flexible
and having greater potential for other useful extensions and applications.

## Open questions
- Instead of a free-standing function in namespace `tbb`, should we consider
  a static member function in class `task_arena`?
- The proposal does not consider arena priority, simply keeping the default `priority::normal`.
  Should a possibility to change priorities be considered?
- Are there any reasons for the API to first go out as an experimental feature?
