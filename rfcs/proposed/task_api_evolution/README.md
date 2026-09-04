# Evolution of TBB Tasking API

This RFC proposes decoupling the task dependency and completion-transfer capabilities of the preview "Task Group Dynamic
Dependencies" feature from `task_group` and exposing them as generic operations over tasks (in the `task` and `this_task`
namespaces). This makes the dependency machinery reusable beyond `task_group` - most importantly, as the foundation for a
non-blocking form of nested parallel algorithm composition that avoids the per-level blocking waits of today's nested
algorithms.

## Motivation

Let's consider usage scenarios where tasks executing in parallel add more parallel work that should finish before
the task is considered completed (nested parallel composition).
For example, a well-known parallel Fibonacci computation - a task calculating
N-th Fibonacci number should calculate (N-1)-th and (N-2)-th numbers (in parallel) and produce the sum.

Current production oneTBB offers only *blocking nested composition* approaches.
Let's consider two options for implementing a Fibonacci example using a blocking approach.
The first one is a nested composition on top of `task_group`:

```cpp
void serial_fibonacci(int n, int& result);

void parallel_blocking_tg_fibonacci(int n, int& result) {
    if (n < serial_cutoff) {
        serial_fibonacci(n, result);
    } else {
        tbb::task_group tg;
        int left_result = 0, right_result = 0;

        auto calculate_left = [&] {
            parallel_blocking_tg_fibonacci(n - 1, left_result);
        };
        auto calculate_right = [&] {
            parallel_blocking_tg_fibonacci(n - 2, right_result);
        };

        tg.run(calculate_left);
        tg.run_and_wait(calculate_right);

        result = left_result + right_result;
    }
}

int main() {
    int result = 0;
    parallel_blocking_tg_fibonacci(n, result);
}
```

On each nested level, `parallel_blocking_tg_fibonacci` creates another `task_group` instance, submits two tasks
into it, and waits for the completion of the nested group.

The second approach is to use parallel algorithms, for example, `parallel_invoke`:

```cpp
void serial_fibonacci(int n, int& result);

void parallel_blocking_invoke_fibonacci(int n, int& result) {
    if (n < serial_cutoff) {
        serial_fibonacci(n, result);
    } else {
        int left_result = 0, right_result = 0;

        auto calculate_left = [&] {
            parallel_blocking_invoke_fibonacci(n - 1, left_result);
        };
        auto calculate_right = [&] {
            parallel_blocking_invoke_fibonacci(n - 2, right_result);
        };

        tbb::parallel_invoke(calculate_left, calculate_right);

        result = left_result + right_result;
    }
}

int main() {
    int result = 0;
    parallel_blocking_invoke_fibonacci(n, result);
}
```

Like the blocking `task_group` example, each nested iteration of `parallel_blocking_invoke_fibonacci`
runs a nested blocking `parallel_invoke` algorithm.

Similar patterns are used by many other parallel computation examples that nest algorithms and task groups into each other.

The recently added preview "`task_group` Dynamic Dependencies" feature opened a way to implement a *non-blocking* approach. It adds
two fundamental building blocks:
1. Predecessor-successor dependencies between tasks in a `task_group`. This allows specifying that a successor task may run only after
   all its predecessor tasks are completed.
2. Completion transferring - allows specifying that the currently executing task may only be considered completed once another
   task (the receiver of the completion) is completed. This means all successors of the currently executing task become successors
   of the receiver, i.e. the currently executing task is effectively replaced by the receiver in the task graph.

The Fibonacci example above can be rewritten using the new functionality:

```cpp
void serial_fibonacci(int n, int& result);

void parallel_non_blocking_tg_fibonacci(tbb::task_group& tg, int n, int* result) {
    if (n < serial_cutoff) {
        serial_fibonacci(n, *result);
    } else {
        int* left_result = new int(0);
        int* right_result = new int(0);

        auto calculate_left = [&tg, left_result, n] {
            parallel_non_blocking_tg_fibonacci(tg, n - 1, left_result);
        };
        auto calculate_right = [&tg, right_result, n] {
            parallel_non_blocking_tg_fibonacci(tg, n - 2, right_result);
        };

        tbb::task_handle left_task = tg.defer(calculate_left);
        tbb::task_handle right_task = tg.defer(calculate_right);
        tbb::task_handle merge = tg.defer([=] {
            *result = *left_result + *right_result;
            delete left_result;
            delete right_result;
        });

        tbb::task_group::set_task_order(left_task, merge);
        tbb::task_group::set_task_order(right_task, merge);

        tbb::task_group::transfer_this_task_completion_to(merge);

        tg.run(std::move(left_task));
        tg.run(std::move(right_task));
        tg.run(std::move(merge));
    }
}

int main() {
    tbb::task_group tg;
    int result;

    tg.run_and_wait([&] {
        parallel_non_blocking_tg_fibonacci(tg, n, &result);
    });
}
```

In contrast with the previous examples, `parallel_non_blocking_tg_fibonacci` does not create a different `task_group` on
each level - it reuses the same one. Each iteration creates tasks to calculate the (N-1)-th and (N-2)-th Fibonacci numbers as
part of the same group. The group is waited on only once - on the calling thread in `main`.

Each decomposition contains four steps:
1. Creating 3 tasks - two to calculate the preceding Fibonacci numbers and one to calculate the sum of them.
2. Establishing the predecessor-successor relationships. The task performing the sum can only start once both operands are calculated.
3. Transferring the completion to the task performing the sum, meaning the calculation of each N-th number is only considered
   completed once the final sum is calculated.
4. Submitting all tasks for execution.

Advantages of the non-blocking approach include:

1. Flat stacks. In the blocking approach, each nested algorithm introduces a new dispatch-loop stack frame; for deep nesting,
   that risks stack overflow. The non-blocking approach has just one wait at the top level, and therefore just one dispatch-loop stack frame.
2. Avoids the hazards of moonlighting. While a thread is blocked in an un-isolated inner-level algorithm, it may take a task from
   the outer-level algorithm, which can in some cases result in issues such as deadlocks.
3. No per-level barrier on waiting. Each blocking level fully drains before the outer level proceeds; the non-blocking approach
   removes that barrier, so independent work can proceed and the continuation runs as soon as its predecessors complete.

No current production or preview oneTBB API provides an alternative for non-blocking nested algorithm composition.

Task dependencies and completion transferring are currently bound to `task_group` only. The idea of this proposal is that
extending these capabilities beyond `task_group` may close this gap in the future. Consider the following pseudocode:

```cpp
void serial_fibonacci(int n, int& result);

void parallel_non_blocking_invoke_fibonacci(int n, int* result) {
    if (n < serial_cutoff) {
        serial_fibonacci(n, *result);
    } else {
        int* left_result = new int(0);
        int* right_result = new int(0);

        auto calculate_left = [=] {
            parallel_non_blocking_invoke_fibonacci(n - 1, left_result);
        };
        auto calculate_right = [=] {
            parallel_non_blocking_invoke_fibonacci(n - 2, right_result);
        };

        tbb::task_completion_handle invoke =
            tbb::nested::parallel_invoke(calculate_left, calculate_right);

        tbb::task_completion_handle merge = tbb::task::run_after(invoke, [=] {
            *result = *left_result + *right_result;
            delete left_result;
            delete right_result;
        });

        tbb::this_task::transfer_completion_to(merge);
    }
}

int main() {
    int result = 0;
    tbb::parallel_invoke([&] {
        parallel_non_blocking_invoke_fibonacci(n, &result);
    });
}
```

`tbb::nested::parallel_invoke` is a non-blocking algorithm that spawns the required tasks and returns
the `task_completion_handle` of the algorithm root instead of waiting.

`tbb::task::run_after(pred, func)` is a function that creates a standalone task that runs after
the `pred` task and executes the function `func`.

`tbb::this_task::transfer_completion_to` is semantically the same as the current `tbb::task_group::transfer_this_task_completion_to`,
except that it accepts a `task_completion_handle` argument.

Implementing such an API implies reusing the same machinery currently used to implement `task_group`
dependencies. Therefore, the implementation details and the API of "Task Group Dynamic Dependencies" should
not be bound to `task_group`, to allow such extensions.

## Proposal

The proposal involves several stages. If accepted, stage 1 should be done before moving the "Task Group Dynamic Dependencies" feature
to production. Stages 2-3 can be done at any point in the future as additional enhancements.

1. Decouple the existing preview parts of "Task Group Dynamic Dependencies" from `task_group` and define them as generic functions
   over tasks.
2. Implement enhancements for dependencies as part of `task_group`. See the section below for more details.
3. Implement nested non-blocking algorithms as an alternative to current nested algorithms. See the section below for more details.

### Phase 1 - Decouple the existing preview API

The current `tbb::task_group` core API is the following:

```cpp
class task_group {
public:
    // Production API
    template <typename Func> void run(Func&& f);
    
    template <typename Func> task_handle defer(Func&& f);

    void run(task_handle&& th);

    template <typename Func>
    task_group_status run_and_wait(const Func& f);

    task_group_status run_and_wait(task_handle&& th);

    task_group_status wait();

    // Preview API
    static void set_task_order(task_handle& pred, task_handle& succ);
    static void set_task_order(task_completion_handle& pred, task_handle& succ);
    static void transfer_this_task_completion_to(task_handle& th);

    task_group_status wait_for_task(task_completion_handle& tch);
    task_group_status run_and_wait_for_task(task_handle&& th);
    task_group_status get_status_of(task_completion_handle& tch);
};
```

The proposal is to move most of the preview APIs outside the `task_group` class. The production API remains the same:

```cpp
enum class task_status {
    not_complete,
    complete,
    canceled
};

class task_group {
public:
    // Production API is the same as above

    // Preview API:
    task_status run_and_wait_for_task(task_handle&& th);
};

namespace task {
    void set_order(task_handle& pred, task_handle& succ);
    void set_order(task_completion_handle& pred, task_handle& succ);

    task_status wait_for(task_completion_handle& tch);
    task_status get_status_of(task_completion_handle& tch);
} // namespace task

namespace this_task {
    void transfer_completion_to(task_handle& th);
} // namespace this_task
```

Moving the API outside `task_group` requires the waiting functions (which may in the future accept completion handles of
standalone or algorithm tasks) to return a type that is not related to `task_group`.

The proposal is a `task_status` type with the same set of values describing the status of the task.

Both overloads of `tbb::task::set_order(pred, succ)` are equivalent to `tbb::task_group::set_task_order(pred, succ)`.
The precondition is that `pred` and `succ` must belong to the same `task_group_context`.

`tbb::this_task::transfer_completion_to(task)` is equivalent to `tbb::task_group::transfer_this_task_completion_to(task)`.
The precondition is that the currently executing task must belong to the same `task_group_context` as `task`.

`tbb::task::wait_for(task)` is equivalent to `tbb::task_group::wait_for_task(task)`.
`tbb::task::get_status_of(task)` is equivalent to `tbb::task_group::get_status_of(task)`.

`tbb::task_group::run_and_wait_for_task(std::move(th))` remains in the `task_group` class to keep all APIs that run tasks
inside `task_group`.

### Phase 2 - `task_group` Enhancements

The current approach to implementing dynamic task graph growth using `task_group` can be simplified. The idea
is to modify the existing `task_group::run` function to return a completion handle and to introduce a running function
that can take a set of predecessors.

Consider the same recursive Fibonacci example as before:

```cpp
void serial_fibonacci(int n, int& result);

void parallel_non_blocking_tg_fibonacci_v2(tbb::task_group& tg, int n, int* result) {
    if (n < serial_cutoff) {
        serial_fibonacci(n, *result);
    } else {
        int* left_result = new int(0);
        int* right_result = new int(0);

        auto calculate_left = [&tg, left_result, n] {
            parallel_non_blocking_tg_fibonacci_v2(tg, n - 1, left_result);
        };
        auto calculate_right = [&tg, right_result, n] {
            parallel_non_blocking_tg_fibonacci_v2(tg, n - 2, right_result);
        };

        tbb::task_completion_handle left = tg.run(calculate_left);
        tbb::task_completion_handle right = tg.run(calculate_right);

        tbb::task_completion_handle merge = tg.run_after(left, right, [=] {
            *result = *left_result + *right_result;
            delete left_result;
            delete right_result;
        });

        tbb::this_task::transfer_completion_to(merge);
    }
}
```

Compared to the current approach, this version immediately submits `left` and `right`, even before adding any dependencies.
The task graph is built while the tasks are running.

The following APIs should be added to support the code above:

```cpp
class task_group {
public:
    template <typename Func>
    task_completion_handle run(Func&& func);

    template <typename Func>
    task_completion_handle run(task_handle&& th); // Possible to implement

    template <typename... Predecessors, typename Func>
    task_completion_handle run_after(Predecessors&... predecessors, Func&& func); // pseudo-code
};

namespace this_task {
    void transfer_completion_to(task_completion_handle& tch);
};
```

Note that `task_group::run` changes its return type from `void` to `task_completion_handle`. Because the returned handle
can be ignored, this change is source-compatible; and because `run` is an inline template, it is also ABI-safe.

Implementing the API above would require a major shift in approach. The current dependency/completion machinery is implemented
as part of the `task_dynamic_state` that is lazily assigned to a task in the group. The state is only created when
the first dependency is added or the completion is received.

With the API above, a task must be ready to set dependencies or receive the completion from the time it is created, which
makes the dynamic state effectively a static part of the task. Preserving the existing lazy initialization is possible if new functions are
added instead of changing the existing ones.

### Phase 3 - Nested Algorithms and Standalone Tasks

In the future phase 3, non-blocking versions of the parallel algorithms can be added, as well as the API to support standalone
tasks. Using the `parallel_invoke` algorithm as an example, to support the code above:

```cpp
template <typename Function1, typename Function2, typename... Functions>
task_completion_handle parallel_invoke(non_blocking_t, Function1 function1, Function2 function2, Functions... functions);

namespace task {
    template <typename... Predecessors, typename Func>
    task_completion_handle run_after(Predecessors&... predecessors, Func&& func);
};
```

An important implementation consideration of adding such algorithms is that not only must the new non-blocking versions
be implemented, but the existing blocking algorithms must also be modified, since each task that can execute non-blocking
parallel algorithms or create standalone tasks for further completion transferring must be dependency-friendly. In terms
of the current implementation, it must have an assigned `task_dynamic_state`.

The only purpose (at least currently) of these algorithms and standalone tasks is to grow the currently executing task graph.
The necessity of non-blocking parallel algorithms and standalone tasks outside the executing task scope is an open question.

The main complication of supporting this use case is the question of `task_group_context`. If the functions above are called
from within the task context, the algorithm or a standalone task can inherit the `task_group_context` from the currently
executing task. Or, in the case of `run_after`, bind the new task to the same context as the `predecessors`.

But if the non-blocking algorithm is executed outside the task scope, it is unclear which context this task should belong to.
The only obvious option is to assign a new context to such an algorithm. The context instance's lifetime must also
be prolonged beyond the return of the non-blocking `parallel_invoke`, until all tasks have executed.

Another important consideration is missing waits. If these functions are executed from within the task scope, the waiting
happens at the upper level (in `task_group::wait` or a blocking algorithm). If the algorithm is called outside the task scope,
a missing wait on the returned completion handle would mean no forward-progress guarantee for the entire algorithm.

## Open Questions

General questions:

0. Is the proposed direction and staging feasible and worth doing?

Open questions to [Phase 1](#phase-1---decouple-the-existing-preview-api):

1. Is the "same `task_group_context`" precondition feasible for setting `task_group` dependencies?
2. Should we keep both `task_group_status` and a separate `task_status` enum?
3. Should calling `this_task::transfer_completion_to` from outside a task body be a no-op?
4. Should `run_and_wait_for_task` belong to `task_group` or be moved to namespace `task`?

Open questions to [Phase 2](#phase-2---task_group-enhancements):

1. Which model should we choose for the dependency state - the current dynamic state with lazy allocation, or a static state?
2. The predecessor argument of `task_group::run_after` needs to be defined.

Open questions to [Phase 3](#phase-3---nested-algorithms-and-standalone-tasks):

1. What is the API for non-blocking parallel algorithms (namespaces, tags, etc.)?
2. Which tasks are dependency-friendly/may transfer their completion? I.e. which tasks host the related static or dynamic state?
3. What is the semantics of calling a non-blocking algorithm from outside the task?

Future work:

1. Should we allow cross-`task_group_context` dependencies to be set? What is the semantics?
