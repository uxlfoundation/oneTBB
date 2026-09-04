# Task Scheduler Bypass Support in `task_group`

This RFC specifies the preview feature that adds Task Scheduler Bypass support to `task_group`.
A task body in `task_group` may return a `task_handle` as an execution hint that designates
the next candidate task to run on the current thread, avoiding the scheduling overhead of spawning.

## Preview Feature Design

Task Scheduler Bypass is a well-known TBB optimization that is widely used internally by oneTBB algorithms and
Flow Graph. With this optimization, a completing or cancelling task nominates the next candidate to be executed
by returning it from `task::execute` or `task::cancel`.

In contrast with spawning a task (that is, pushing the task to the local thread's deque), bypassing does not involve
unnecessary deque operations and, in most cases, does not expose the returned task for stealing, which can improve locality.

The bypassed task is not strictly guaranteed to be executed next, or even to be executed by the current thread, because
the current thread may find a higher-priority task to proceed with.

Task Scheduler Bypass was present in older TBB versions as part of the low-level tasking API. Since that API was removed
and `task_group` was proposed as its direct replacement, the Task Scheduler Bypass capability was introduced as
a preview feature of `task_group` (which is also mentioned in the oneTBB Migration Guide).

This preview feature allows the bodies of tasks in `task_group` to return a `task_handle` object:

```cpp
tbb::task_group tg;

tg.run([&tg] {
    tbb::task_handle next_task = tg.defer(next-task-body);
    return next_task;
});
```

If the returned `task_handle` is non-empty and owns a task with no unresolved dependencies (in case the preview Task Group
Dynamic Dependencies feature is enabled), it serves as an optimization hint for which task should be executed next.

## Moving the Feature to `supported`

Since the feature has been available for a long time and its implementation is stable, it is proposed to move it to `supported`.

From the implementation standpoint, the feature is implemented as several non-virtual member functions of the internal
`task_group` task class. Therefore, the promotion is not a breaking change.

The only behavioral change that is possible is if the user returned `task_handle` already in the existing code. With the
current implementation, this `task_handle` is discarded and the task is never executed. With the change, the return task
will be bypassed and will execute. However, returning a `task_handle` in the existing code is considered valueless, since
the task it owns can never be executed and is destroyed together with the handle. Such code is therefore treated as
erroneous rather than as a legitimate usage, and no reasonable existing program is expected to be affected.

From the documentation perspective, the feature only extends the requirements for a user-provided task body.
Currently, the body is required to be a FunctionObject, as defined in the C++ Standard. It is proposed to define the updated set of
requirements as a named requirement (the same approach that is used for oneTBB algorithms and Flow Graph):

The type `F` satisfies `TaskGroupTaskBody` (alternative name `TaskBody`) if:

1. It is copy constructible. This is an existing, but currently undocumented, requirement.
2. It can be invoked with one of the following pseudo-signatures (which covers function objects, lambdas, and pointers to functions):

   2.1. `void operator()() const` - covers the existing task bodies.

   2.2. `task_handle operator()() const` - covers the bypassing task bodies. The semantics are the following:

   * a. If the returned `task_handle` is non-empty (and owns a task with no unresolved dependencies, in case the dependencies preview
     feature is enabled), it serves as an optimization hint for the task that could be executed next. In any case, the task is
     submitted for execution implicitly.
   * b. The returned `task_handle` must not be submitted explicitly. Otherwise, the behavior is undefined.
   * c. If the returned `task_handle` owns a task that belongs to a different `task_group`, the behavior is undefined.

If the invoke operator of `F` returns a type other than `void` or `task_handle`, it is proposed to discard the returned
value, as the currently released implementation does.

As with other recent preview and production features, it is proposed to add a feature-test macro: `TBB_HAS_TASK_GROUP_BYPASS`.

It is also proposed to have a coarse-grained feature-test macro for the `task_group` API: `TBB_HAS_TASK_GROUP`, with a value that is greater
than or equal to the greatest value of any fine-grained `task_group`-related feature-test macro. For example, if there are two fine-grained
feature-test macros, `TBB_HAS_TASK_GROUP_BYPASS` equal to `202609` and `TBB_HAS_TASK_GROUP_DEPENDENCIES` equal to `202612`, the value
of `TBB_HAS_TASK_GROUP` should be no less than `202612`. It may be greater if small extensions or bug fixes need to be highlighted.

## Open Questions Before Moving to `supported`

1. What is the most appropriate name for the new named requirement? The current options are `TaskGroupTaskBody` and `TaskBody`.
2. Is the proposed approach with coarse-grained and fine-grained feature-test macros justified enough to be implemented?
3. Is it acceptable to silently break the use cases where the returned `task_handle` was discarded, treating them as valueless and error-prone?
4. Should the return types other than `void` and `task_handle` be silently discarded, as the currently released
   implementation does, or should such bodies be ill-formed?