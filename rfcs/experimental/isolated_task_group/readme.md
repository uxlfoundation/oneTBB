# Task group as an isolation scope

## Introduction

In-arena task isolation scopes were introduced into TBB to restrict task stealing within arena
in scenarios where stealing arbitrary tasks leads to correctness or performance issues.

On the implementation level, an isolation scope is represented by a unique tag (usually an address)
that is assigned to any task created within the dynamic extent of the scope and used by
task dispatchers called within that extent. Other task dispatchers, and specifically those of
oneTBB worker threads, are not restricted by isolation and can take any task, including those
with an isolation tag.

On the API level, the main way to create an isolation scope is to call `this_task_arena::isolate`.
Typically, it is used to wrap one or more parallel algorithm invocations, and the calling thread
is prohibited to take any task created outside of those algorithms. A typical usage pattern
for this is to wrap nested parallel calls within an outer-level parallel algorithm in which tasks
depend on a thread-specific state (e.g., use TLS or acquire a lock).

However, there are also patterns where the isolation scope needs to go beyond a single invocation
of `task_arena::isolate`. For example, the following pseudocode expresses a lazy initialization
pattern within an outer TBB parallel construct, with the initialization function also utilizing
some parallelism.

```
tbb::task_group tg;
mutex init_mtx;
parallel_XXXL { // pipeline, FG
  while (!initialized(some_obj)) {
    if (try_lock(init_mtx)) {
      tg.run_and_wait {
        initialize(some_obj); // uses parallel_for, etc.
      };
      unlock(init_mtx);
    } else { tg.wait(); }
  }
  ... // do the work that uses some_obj
}
```

Any thread that needs `some_obj` checks if it has been initialized, and if not, tries to obtain
the lock and perform initialization. At the same time, threads that have not succeeded to get
the lock can still help with inner parallelism of the initialization function. For that,
the function is called within `task_group::run_and_wait`, and other threads call `wait` on the
same task group.

In this example the use of `try_lock` eliminates the risk of deadlock via unrestricted stealing.
Still, isolation can be helpful for both the thread doing the initialization and those helping.
The main advantage is to prevent tasks not related to the initialization of `some_obj` to be taken
by threads that wait for that initialization to complete - so that it can be complete sooner
and the threads can make progress on their outer level tasks.

However, for that to happen each participating thread should use the same isolation tag, which is
not possible with `this_task_arena::isolate`. And since a task group is used for collaborative waiting
anyway, it is natural for it to also provide the isolation scope.

## Experimental feature: `isolated_task_group`

The `isolated_task_group` class is a task group which tasks belong to the same isolation scope,
unique for each instance of the class.

```c++
// Defined in tbb/task_group.h
// Enabled with #define TBB_PREVIEW_ISOLATED_TASK_GROUP 1

namespace tbb {
    class isolated_task_group : public task_group {
    public:
        isolated_task_group();
        isolated_task_group(task_group_context& context);
        ~isolated_task_group();

        /* Redefines the following member functions of class task_group */
        template<typename F> void run(F&& f);
        void run(d2::task_handle&& h);
        template<typename F> task_group_status run_and_wait( const F& f );
        task_group_status wait();
    };
}
```

The class inherits `task_group` and redefines member functions that run and wait for tasks.
Except for the class name, using `isolated_task_group` is no different from `task_group`.

This experimental feature was added in [TBB 2019 Update 9](https://github.com/uxlfoundation/oneTBB/releases/tag/2019_U9).
For unknown reasons (most likely just by oversight) the documentation for the class is missed
for oneTBB 2021 and beyond.

## Does oneTBB still need `isolated_task_group`?

### Use cases

Since 2019, the original motivating use case of lazy parallel initialization has been addressed
in oneTBB by the `collaborative_call_once` function, which also creates an isolation scope for each
instance of `collaborative_once_flag`.

However, `isolated_task_group` is more versatile and might have other use cases. Specifically,
with the addition of [waiting for work in a task arena](https://github.com/uxlfoundation/oneTBB/tree/master/rfcs/proposed/task_arena_waiting)
in-arena isolation of unrelated work portions represented by different task groups might become 
important to prevent unexpected latency increase for the wait calls.

### Shortcomings

In retrospective, the inheritance-based design of `isolated_task_group` seems a mistake.
It was likely motivated by the desire to share the core implementation; and the "is-a" relation
indeed exists. However, since the `task_group` member functions are not virtual, the new class
only hides but does not override these. It is therefore possible to call these methods and create
tasks that do not carry the group isolation tag.

It does not seem possible to redefine the functions of `task_group` as virtual. We could consider
private inheritance with selective injection of `task_group` member functions; but that would not
fully solve the problem because C-style casts ignore private inheritance.

Continuing with that design makes therefore no sense; a task group with isolation should be
implemented either as a completely separate class or as a property of class `task_group`
set at construction. The latter option, however, would likely need class layout changes and so
would break backward compatibility.

### Alternatives

Over time, alternative ways to provide "shareable" in-arena isolation scopes were also considered.

#### A `task_group_context` trait

One of early ideas for in-arena isolation was to add a special trait to the `task_group_context`
class that would communicate to the task scheduler that arbitrary stealing is not allowed
for a certain parallel construct. The benefits of this approach are: a) the possibility to use
it with all TBB parallel constructs (algorithms, task groups, flow graphs), and b) the dynamic tree
of bound contexts would enable propagation of isolation to nested constructs.

However, the main role of `task_group_context` and the dynamic tree is to support cancellation
and exceptions during parallel execution. Most `task_group_context` instances are implicit, but
some might be created explicitly and can even used by several parallel constructs; and contexts
explicitly created as `isolated` start a new context tree.

After analysis we can conclude that:

- Cancellation/exception handling and task isolation are not logically associated. In certain
  points in the context tree, a developer may want to start task isolation while keeping
  cancellation propagation, and vice versa.
- We cannot rely on context binding for isolation, because nested levels (which can reside in
  an independently developed library) might reuse outer level contexts or use isolated context.
- Unlike cancellation and exception handling which are rare events and might therefore absorb
  higher overheads, task isolation must be checked for each task selected for execution and must
  therefore have minimal overheads.
- At the task scheduler level, the high-level semantic information is mostly lost, and
  significantly different usage patterns are often indistinguishable for the scheduler.
  A task with an isolation property might spawn tasks which belong to the same parallel construct,
  to the outer level one, or to a new nested one. It essentially means that, in order to support
  isolation specified in `task_group_context` (and not by dedicated APIs), each task spawn should
  check/obtain an isolation tag, causing extra overhead and breaking the pay-as-you-go principle.

Seemingly, making task isolation a property of `task_group_context` (or something similar) would
require a significant overhaul of both the scheduler internals and likely the public oneTBB APIs.

#### User-specified isolation tags

Another idea is to add an isolation tag parameter to the `task_arena` APIs for work submission,
allowing users to explicitly manage isolation scopes as they need. At the time it was considered
as a possible future extension for `task_arena::isolate`; but it might as well be added to
`enqueue` and `wait_for` calls.

The main advantage of this approach is that it is a reasonably simple API extension on top of
the existing task isolation machinery. It is explicit, and so adheres to pay-as-you-go and is
very flexible. For example, a special "no isolation" tag value can be added for specific cases,
such as submitting work to an outer-level task group or a flow graph.

But the other side of this flexibility is higher risk of user errors due to omission or
mismatch of isolation tags, which might cause deadlocks. At a glance, it is likely not possible
for oneTBB to diagnose potential errors, as any tag value would technically be valid.

Also, a good unique isolation tag is usually an object address. In the case of `task_arena` calls
taking a task group instance it would likely be the address of that instance, resulting
in a strange "duplication" of arguments, like in `arena.wait_for(tgroup, intptr_t(&tgroup));`.

#### Implicit isolation for `task_arena` calls

We might as well consider a much narrower but implicit approach to task isolation specifically
for the new arena waiting API extensions. Each arena method that takes a task group would use
the address of that group as the isolation tag, automatically isolating these calls (and their
dynamic extent, i.e., the nested parallelism within) from any other work in that arena.

Compared to the user-specified isolation tags, this approach would not have most of the downsides
but also would lose the flexibility and broader usage potential. One issue would however remain
the same - the regular methods of `task_group` can be used to create tasks that do not carry
any isolation tag; and so user errors are still possible.

## Exit criteria

To decide on making `isolated_task_group` a fully supported feature of the library, we need to
answer at least the following questions:

- Is there sufficient motivation to add support for isolation scopes shared across multiple calls?
- Is the task group based design preferable to other alternatives?
- What is better, adding a special class or extending `task_group` with a new property?

For the first question, we should check if there are notable performance benefits with isolated
task groups compared to regular ones; that needs some POC implementation in `task_arena`. We can
also search for usage evidences and/or feedback on `isolated_task_group`.

In case we decide to proceed with the feature, a oneTBB specification update will be needed.
