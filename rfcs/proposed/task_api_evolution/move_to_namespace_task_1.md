# Decoupling Task Dependency APIs from `task_group`

This RFC proposes moving the task dependency and completion-transfer APIs introduced by the
"Task Group Dynamic Dependencies" preview feature out of `task_group` and into the `tbb::task`
namespace as free functions. These functions have potential future use beyond `task_group`,
such as for non-blocking parallel algorithms or integration with C++ coroutines and
senders/receivers. Doing so before the feature is promoted to production creates a foundation
for future oneTBB extensions without added risk of API breaking changes.

## Motivation

The recently added "Task Group Dynamic Dependencies" preview feature introduced two fundamental
building blocks for dynamic task graph construction within a `task_group`:

- **Predecessor-successor dependencies** between tasks: a successor task may only begin once all
  its designated predecessors have completed.
- **Completion transfer**: the currently executing task may only be considered complete once a
  designated continuation task completes. The continuation task becomes responsible for signalling
  successor tasks, effectively replacing the current task in the task graph.

Together these primitives make a non-blocking style of parallel decomposition expressible in
oneTBB. Rather than blocking the calling thread on a nested `task_group` or `parallel_invoke`
until its tasks finish, a task can create subtasks and a continuation, transfer its completion
to the continuation, and return immediately. A single `wait()` at the outermost call site is then
sufficient to ensure all work completes, regardless of decomposition depth, and no nested
task dispatch loops accumulate on the call stack.

The non-blocking Fibonacci example illustrates this with the current preview API:

```cpp
void fibonacci(tbb::task_group& tg, int n, int* result) {
    constexpr int serial_cutoff = 10;
    if (n <= serial_cutoff) {
        *result = serial_fibonacci(n);
        return;
    }

    int* a = new int(0);
    int* b = new int(0);

    tbb::task_handle t1 = tg.defer([&tg, n, a]{ fibonacci(tg, n - 1, a); });
    tbb::task_handle t2 = tg.defer([&tg, n, b]{ fibonacci(tg, n - 2, b); });
    tbb::task_handle merge = tg.defer([=]{
        *result = *a + *b;
        delete a;
        delete b;
    });

    tbb::task_group::set_task_order(t1, merge);
    tbb::task_group::set_task_order(t2, merge);
    tbb::task_group::transfer_this_task_completion_to(merge);

    tg.run(std::move(t1));
    tg.run(std::move(t2));
    tg.run(std::move(merge));
}

int main() {
    tbb::task_group tg;
    int result = 0;
    tg.run_and_wait([&]{ fibonacci(tg, 30, &result); });
}
```

These two operations are defined as static members of `task_group` - `set_task_order` and
`transfer_this_task_completion_to` - primarily because `task_group` is the only public API
to create and run individual tasks in oneTBB. However, they differ from the core methods
of a task group, as these new functions manipulate tasks which are already a part of some
execution context. And now we see potential use cases for them in contexts where tasks
are not created through a `task_group`.

**Non-blocking parallel algorithms.** TBB's parallel algorithms are currently blocking:
they do not return until all their tasks complete. A non-blocking variant would instead
return a `task_completion_handle` representing the completion of all its work.
The calling task could then chain a continuation and transfer its own completion to it,
effectively embedding the algorithm into the current task tree and achieving the benefits
of flat-stack, single-wait-point execution while also reducing explicit task management.
A sketch of what this could look like:

```cpp
void fibonacci(int n, int* result) {
    constexpr int serial_cutoff = 10;
    if (n <= serial_cutoff) {
        *result = serial_fibonacci(n);
        return;
    }

    int* a = new int(0);
    int* b = new int(0);

    tbb::task_completion_handle branches =
        tbb::parallel_invoke(tbb::nonblocking_tag,
                             [=]{ fibonacci(n - 1, a); },
                             [=]{ fibonacci(n - 2, b); });

    tbb::task_completion_handle merge =
        tbb::task::run_after(branches, [=]{
            *result = *a + *b;
            delete a;
            delete b;
        });

    tbb::task::transfer_completion_to(merge);
}
```

**On-the-fly task graph construction.** A natural extension of the idea above is to also allow
embedding of manually constructed task graphs into the current execution context.
Task graphs can already be created with the `task_group` preview API, but then each task needs
to be spawned by `task_group::run`, and so the graph binds to a task group. To build embeddable
task graphs, not only the new preview methods but also analogues of `defer` and `run` need to
operate without a `task_group`, inheriting context from the currently executing task. 

**Integration with asynchronous C++ execution models.** We observe that modern C++ asynchronous
programming models - coroutines and the sender/receiver model of C++26 `std::execution` - are built
around the same core concepts as the tasking primitives discussed here: signalling completion and
chaining work onto that signal without blocking the calling thread. Therefore the task dependency
API could plausibly serve as a foundation for possible integration between TBB and these models,
such as providing a TBB-backed coroutine awaiter or exposing TBB algorithms as senders.

We believe these potential directions of oneTBB evolution would benefit from the task dependency API
available in a more generic form, decoupled from `task_group`. While the static member functions
could be used without a task group instance, one could argue that their attribution to `task_group`
does not look intuitive and may cause confusion in the outlined scenarios.

Therefore we propose moving the preview task dependency APIs outside of `task_group` before promoting
the feature to production, to minimize the risk of API breaking changes and keep the door open for
future oneTBB functionality extensions in a backward-compatible way.

## Proposal

For the context, see the current `task_group` class with the preview additions:

```cpp
class task_group {
public:
    // Production API: run, wait, run_and_wait, defer

    // Preview API
    static void set_task_order(task_handle& pred, task_handle& succ);
    static void set_task_order(task_completion_handle& pred, task_handle& succ);
    static void transfer_this_task_completion_to(task_handle& th);

    task_group_status wait_for_task(task_completion_handle& tch);
    task_group_status run_and_wait_for_task(task_handle&& th);
    task_group_status get_status_of(task_completion_handle& tch);
};
```

The proposal is to move the dependency and completion-transfer APIs out of `task_group` and
define them as free functions in the `tbb::task` namespace, consistent with the existing
precedent of `tbb::task::suspend` and `tbb::task::resume`. The production API of `task_group`
is unchanged, so is the preview API for waiting for a single task.
The moved API still remains a preview feature.

```cpp
namespace tbb {

class task_group {
public:
    // Production API - unchanged

    // Preview API that remains in task_group
    task_group_status wait_for_task(task_completion_handle& tch);
    task_group_status run_and_wait_for_task(task_handle&& th);
    task_group_status get_status_of(task_completion_handle& tch);
};

namespace task {

    // Preview API moved to namespace task
    void set_task_order(task_handle& pred, task_handle& succ);
    void set_task_order(task_completion_handle& pred, task_handle& succ);
    void transfer_completion_to(task_handle& th);

} // namespace task

} // namespace tbb
```

No semantic changes to the moved functions are proposed. For `set_task_order`, one of
the preconditions changes from "The tasks referred to by `pred` and `succ` must belong
to the same `task_group`" to  "... belong to the same `task_group_context`".
For `transfer_completion_to`, two preconditions change: the function must be called
from within a running task (but not necessarily a `task_group` task), and the running task
and the task referred to by `th` must belong to the same `task_group_context`.

The value of the `TBB_HAS_TASK_GROUP_DEPENDENCIES` feature test macro must be updated
when the changes are made.

## Open Questions

1. The proposed preconditions for `set_task_order` and `transfer_completion_to` require
   the involved tasks to belong to the same `task_group_context`. In current or future practice,
   could handles refer to tasks from different contexts, or tasks with no context set?

2. The precondition requires `transfer_completion_to` to be called from within a running task.
   Should a violation be undefined behavior, a detectable error, or possibly benign and ignored?

3. The proposal retains `transfer_completion_to` taking a `task_handle` as the only overload. 
   With non-blocking algorithms a `task_completion_handle` overload might be useful when
   no continuation is necessary and completion of the algorithm completes the task - that is,
   completion could be transferred to already-submitted work. Should such overload be added?

4. After the move, `tbb::task::set_task_order` sounds somewhat repetitive. Should the function
   be renamed to `set_order` for brevity, or is the repetition good for semantic clarity?

5. Should the proposal extend to other preview APIs, namely `wait_for_task` and `get_status_of`?
   The motivation would be: if a `task_completion_handle` might be obtained outside `task_group`,
   functions accepting that handle should probably be outside as well. There are no clear use cases
   that would benefit from that, however the argument to make a change before going to production
   applies to these functions as well.



