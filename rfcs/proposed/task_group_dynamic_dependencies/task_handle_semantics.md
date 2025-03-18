# API and semantics details for task group dynamic dependencies

*Note:* This document is a sub-RFC of the [umbrella RFC for task group dynamic dependencies](README.md). 

## Introduction

This document contains a concrete API and semantics proposal for ``task_group`` extensions defined in the parent RFC.
The following cases are covered:
* ``task_group`` extensions allowing handling the tasks in various states: created, submitted, executing and completed.
  Existing API only allows having a non-empty ``task_handle`` object handling the created task and an empty one that does not
  handle any tasks.
* API for setting the dependencies between tasks in various states.
* API for transferring the dependencies from the executing task to the task in various states.

## Semantic extension for handling tasks in various states

The parent proposal for extending the task group with APIs for setting dynamic dependencies defines the following states of the task in the ``task_group``:
* Created 
* Submitted
* Executing
* Completed

Practically, the task is in `created` state when the ``task_group::defer`` was called and the task was registered in the ``task_group``, but one of the submission methods (such as ``task_group::run``) was not yet called. 

The task state is changed to `submitted` when one of the submission methods was called and the task may be scheduled for the execution if all the
dependent tasks are completed.

The state is changed to `executing` when all of the dependent tasks are completed and some thread takes the task for execution is executing the
corresponding task body.

Once the thread finishes executing the task body, the state of the task is changed to `completed`.

The parent RFC proposes to extend possible tasks states that can be handled by ``task_handle`` object to be tasks in any states defined above. Such a change
requires changing the semantics of the task submission methods (such as ``task_group::run`` and others) while working with various states handled by ``task_handle``. 

Unlike this approach, this document proposes keeping the ``task_handle`` as a unique-owning handle that owns the task in created state or does not owns the task.
For handling the task in other states, it is proposed to add a new weak-owning handle ``task_tracker`` that can handles the task in any state.

``task_tracker`` object can be obtained by constructing from the ``task_handle`` object owning the created task. ``task_handle`` cannot be constructed using the
``task_tracker`` argument.

```cpp
tbb::task_group tg;

tbb::task_handle handle = tg.defer([] {/*...*/}); // task is in created state, handle owns the task
tbb::task_tracker tracker = handle; // create the tracker for the task owned by the handle

tg.run(std::move(handle)); // task is in submitted state, handle is left in an empty state

// tracker is non empty and can be used to track progress on the task and set dependencies
```

In this case, no semantic changes are needed for the submission methods, because the ``task_handle`` semantics is not changed.

Alternative approaches for handling tasks in different states are described in the [separate section](#alternative-approaches).

``task_tracker`` class can also support special functions, returning the status of the tracked task:

```cpp
class task_tracker {
public:
    bool was_submitted() const;
    bool is_completed() const;
};
```

Having ``task_tracker.was_submitted()`` equal to ``true`` means the tracked task state was changed from created
to submitted in the past. The task can be either in submitted, executing or completed state.

``is_completed`` method returns ``true`` if the tracked task is completed. This state cannot be changed in the
future.

## Semantics for setting dependencies between tasks

Lets consider creating a predecessor-successor dependency between the task tracked or owned by ``predecessor``
and the task handled by ``successor_task_handle`` -
``task_group::make_edge(predecessor_task_handle, successor_task_handle)``. 

As it was stated in the parent RFC document, we would like to allow adding predecessors in any state described above and to limit the successor to be a task in created state since it can be too late to add predecessors to
the task in executing or completed state.

The second limitation is handled by limiting the successor argument to be only ``task_handle``.

Lets consider the different states of the task tracked or handled by ``predecessor``. 

If the predecessor task is in any state except the completed one (created/scheduled/running), the API registers the successor task
in the list of successors on the predecessor side and increase the corresponding reference counter on the successor side to ensure it
would not be executed before the predecessor task. The successor task can only start executing once the associated reference counter is equal to 0.

If the predecessor task is in `completed` state, the API has no effect in terms of modifying the list of successors and reference counters since no additional
dependencies required and the successor task can be executed if all other dependent tasks are executed as well.

If the predecessor task state has changed while registering the task as a predecessor for any task, the API should react accordingly to make sure
adding dependencies and increasing the corresponding reference counters are not done for completed tasks.

Implementation-wise, this API requires adding a list of successors into the predecessor task and adding the new vertex instance that corresponds
to the successor task. This vertex would contain the reference counter and a pointer to the successor task itself. Each element in the task successor list
is a pointer to the vertex instance of the successor task.

The vertex instance is created once the first task is registered as a predecessor and is reused by any other predecessors. 

Once the predecessor task is completed, it should go through the list of successor vertices and decrement the reference counter. Once the successor's
reference counter is equal to 0, the successor task can be scheduled for execution.

API-wise, the function that decreases the reference counter may also return the pointer to the task. If the reference counter is not equal to 0, the
returned pointer is ``nullptr``. Otherwise, the successor task pointer is returned. It is required to allow bypassing one of the successor tasks
if the body of the predecessor task did not return other task that should be bypassed.

This implementation approach is illustrated in the picture below:

<img src="predecessor_successor_implementation.png" width=800>

### Adding successors to the current task

Consider use-case of parallel wavefront pattern on the 2-d grid. Each cell is computed as part of a separate task in ``task_group``. Each cell task computes
itself and creates more tasks to process the cell below and the cell on the right.

<img src="wavefront_grid.png" width=150>

If there is a strong dependency between the computation of the current cell and the computations of the following cells, it is required to add
currently executed task as a predecessor of the tasks representing the cells below and on the right. Since there is no ``task_handle`` or ``task_tracker`` representing the
currently executed task, the ``make_edge`` function described above cannot be used to set these dependencies. 

It is proposed to add the special function to add successors to the currently executed task to handle this use-case. The API can be 
``tbb::task_group::current_task::add_successor(task_handle& sh)`` and it has the same effect as ``make_edge`` between the ``task_handle`` or ``task_tracker`` handling
the current task and ``sh``. 

## Semantics for transferring successors from the currently executing task to the other task

Lets consider the use-case where the successors of the task ``current`` are transferred to the task ``target`` owned or tracked by the ``target_handler``. 
In this case, the API ``tbb::task_group::current_task::transfer_successors_to(target_handler)`` should be called from the body of ``current``.

As it was mentioned in the parent RFC, if ``transfer_successors_to`` is called outside of task belonging to the same ``task_group``, the behavior is
undefined.

It is also useful for this API to be flexible in regard to ``target_handler`` and to allow different task states.

If ``target`` task is in `created`, `scheduled` or `executing` state, this API should merge together the successors list of ``current``
and ``target`` and sets ``target`` to have the merged successors list. It should be thread-safe to add new successors to ``current`` and ``target``
by using the ``make_edge`` API with ``current`` or ``target`` as predecessors.

If ``target`` task is in `completed` state, it does not make sense to do any merging of successors list since new dependent task for the successors
that are transferring is already completed. In that case, the responsibility for "releasing" the successors is on the ``current`` task. The API should
release the reference counter of all successors of ``current`` in this case.

<img src="transferring_between_two_basic.png" width=800>

It is clear that while transferring from ``current`` to ``target`` the successors list of ``target`` should contain both previous successors of ``target``
and the successors of ``current``.

Interesting aspect is what should be done with the successors list of ``current``.

The first option is to consider ``current`` and ``target`` a separate tasks even after the transferring the successors from one to another.

In this case, after the transfer, the task ``current`` will have an empty successors list, and ``target`` will have a merged successors list:

<img src="transferring_between_two_separate_tracking.png" width=800>

After the transfer, the successors of ``current`` and ``target`` are tracked separately and adding new successors to one of them would only
affect the successors list of one task:

<img src="transferring_between_two_separate_tracking_new_successors.png" width=800>

Alternative approach is to keep tracking ``current`` and ``target`` together after transferring. This requires introducing the new state of task - a `proxy` state.
The task changes its state to `proxy` once the ``transfer_successors_to`` is executed from the body of the task.

If the task ``current`` is a proxy to ``target`` they are sharing the single merged list of successors:

<img src="transferring_between_two_merged_tracking.png" width=800>

Any changes in the successors list operated on ``current`` or ``target`` will modify the same list of successors - adding or transferring will modify the
state of both "real" and proxy tasks:

<img src="transferring_between_two_merged_tracking_new_successors.png" width=800>

Two racing use-cases should be considered as well for each approach:
* (a) Adding new successors to ``A`` while it transfers it's successors to ``B``,
* (b) Transferring successors from ``A`` to ``B`` while ``B`` is transferring it's successors to ``C``.

Lets consider the use-case (a) first.

There are two options how the actual modifications of the ``A`` successors list can be linearized - the new successor can be added before actual transferring
the entire list to ``B`` or after that. 

If the successors of ``A`` and ``B`` are tracked separately (the first option described), if the new successor was added before the transfer, the new successor
would be transferred to ``B`` together with the entire list.

If the transferring was done before adding the successor - the new successor would be added to ``A`` only and would not appear in the successors list of ``B``.

If the successors of ``A`` and ``B`` are tracked together (the second option described) in both linearization case the newly added successor will appear in both
successors list of ``B`` ("real" task) and ``A`` (task in `proxy` state).

In the use-case (b), there are also two options how the modifications can be linearized - the successors would be transferred from ``A`` to ``B`` before
transferring the successors from ``B`` to ``C`` or after that.

In case of separate successors tracking, and if the transfer ``A->B`` was done before the transfer ``B->C``, both successors lists from
``A`` and ``B`` would be transferred to ``C``. Successors lists of ``A`` and ``B`` will be empty after doing both transfers.

In the other case, the successors of ``B`` will be first transferred to ``C`` and then the successors from ``A`` will be transferred to ``B``. As a result, the
successors list of ``A`` will be empty, the successors list of ``B`` will contain all previous successors of ``A`` and the successors list of ``C`` will
contain the previous successors of ``B``.

In case of merged successors tracking, in both linearization scenarios ``A``, ``B`` and ``C`` will have the same successors list containing all of the successors.
Tasks ``A`` and ``B`` will be in a `proxy` state to task ``C``. 

Such semantics can be challenging to implement. Since the task ``A`` is not participating in transferring successors from ``B`` to ``C``, the task ``B`` would need
to track it's proxy ``A`` (and all other proxies) and update the pointed list once the successors of ``B`` are transferred.

## API proposal summary

```cpp
namespace oneapi {
namespace tbb {

// Existing API
class task_handle;

// New APIs
class task_tracker {
public:
    task_tracker();

    task_tracker(const task_tracker& other);
    task_tracker(task_tracker&& other);

    task_tracker(const task_handle& handle);

    ~task_tracker();

    task_tracker& operator=(const task_tracker& other);
    task_tracker& operator=(task_tracker&& other);

    task_tracker& operator=(const task_handle& handle);

    explicit operator bool() const noexcept;

    bool was_submitted() const;
    bool is_completed() const;
}; // class task_tracker

bool operator==(const task_tracker& t, std::nullptr_t) noexcept;
bool operator==(std::nullptr_t, const task_tracker& t) noexcept;

bool operator!=(const task_tracker& t, std::nullptr_t) noexcept;
bool operator!=(std::nullptr_t, const task_tracker& t) noexcept;

bool operator==(const task_tracker& t, const task_tracker& rhs) noexcept;
bool operator!=(const task_tracker& t, const task_tracker& rhs) noexcept;

class task_group {
    static void make_edge(task_handle& pred, task_handle& succ);
    static void make_edge(task_tracker& pred, task_handle& succ);

    struct current_task {
        static void add_successor(task_handle& succ);

        static void transfer_successors_to(task_handle& h);
        static void transfer_successors_to(task_tracker& h);
    };
}; // class task_group

} // namespace tbb
} // namespace oneapi
```

### ``task_tracker`` class

#### Construction, destruction and assignment

``task_tracker()``

Creates an empty ``task_tracker`` object that does not track any task.

``task_tracker(const task_tracker& other)``

Copies ``other`` to ``*this``. After this, ``*this`` and ``other`` track the same task.

``task_tracker(task_tracker&& other)``

Moves ``other`` to ``*this``. After this, ``*this`` tracks the task previously tracked by ``other``.
Other is left in empty state.

``task_tracker(const task_handle& handle)``

Creates ``task_tracker`` object that tracks the task owned by ``handle``. 

``~task_tracker()``

Destroys the ``task_tracker`` object.

``task_tracker& operator=(const task_tracker& other)``

Replaces the task tracked by ``*this`` to be a task tracked by ``other``.
After this, ``*this`` and ``other`` track the same task.

Returns a reference to ``*this``.

``task_tracker& operator=(task_tracker&& other)``

Replaces the task tracked by ``*this`` to be a task tracked by ``other``.
After this, ``*this`` tracks the task previously tracked by ``other``.
Other is left in empty state.

Returns a reference to ``*this``.

``task_tracker& operator=(const task_handle& handle)``

Replaces the task tracked by ``*this`` to be a task owned by ``handle``.

Returns a reference to ``*this``.

#### Progress functions

``explicit operator bool() const noexcept``

Checks if `this`` tracks any task object.

Returns ``true`` if ``*this`` is a non-empty tracker, ``false`` otherwise.

``bool was_submitted() const``

Returns ``true`` if the task tracked by ``*this`` was submitted for execution,
``false`` otherwise.

``bool is_completed() const``

Returns ``true`` if the task tracked by ``*this`` is completed, ``false`` otherwise.

#### Comparison operators

``bool operator==(const task_tracker& t, std::nullptr_t) noexcept``

Returns ``true`` if ``t`` is a non-empty tracker, ``false`` otherwise.

``bool operator==(std::nullptr_t, const task_tracker& t) noexcept``

Equivalent to ``t == nullptr``.

``bool operator!=(const task_tracker& t, std::nullptr_t) noexcept``

Equivalent to ``!(t == nullptr)``.

``bool operator!=(std::nullptr_t, const task_tracker& t) noexcept``

Equivalent to ``!(t == nullptr)``.

``bool operator==(const task_tracker& lhs, const task_tracker& rhs) noexcept``

Returns ``true`` if ``lhs`` tracks the same task as ``rhs``, ``false`` otherwise.

``bool operator!=(const task_tracker& lhs, const task_tracker& rhs) noexcept``

Equivalent to ``!(lhs == rhs)``.

### Member functions of ``task_group`` class

``static void task_group::make_edge(task_handle& pred, task_handle& succ)``

Registers the task owned by ``pred`` to be a predecessor that must complete before the task owned
by ``succ`` can start executing.

If ``pred`` or ``succ`` are empty, the behavior is undefined.

It is safe to add multiple predecessors to the same successor and add the same predecessor for multiple successor tasks.

It is safe to add successors to the task that currently transfers it's successors to another task and
to the task to which the successors are transferred.

``static void task_group::make_edge(task_tracker& pred, task_handle& succ)``

Registers the task tracked by ``pred`` to be a predecessor that must complete before the task owned
by ``succ`` can start executing.

If ``pred`` or ``succ`` are empty, the behavior is undefined.

It is safe to add multiple predecessors to the same successor and add the same predecessor for multiple successor tasks.

It is safe to add successors to the task that currently transfers it's successors to another task and
to the task to which the successors are transferred.

### Member functions of ``task_group::current_task``

``static void add_successor(task_handle& succ)``

Resisters currently executing task to be a predecessor that must complete before the task handled by ``succ`` can start executing.

If this function is called outside of the body of the ``task_group`` task, the behavior is undefined.

If is safe to use this function simultaneously with ``make_edge`` that adds more predecessors to ``succ``.

``static void transfer_successors_to(task_handle& h)``

The exact wording for the semantics of this method should be defined after making a decision about merged or separate tracking of tasks,
that was described above.

``static void transfer_successors_to(task_tracker& h)``

The exact wording for the semantics of this method should be defined after making a decision about merged or separate tracking of tasks,
that was described above.

## Alternative approaches 

The alternative approaches are to keep only the ``task_handle`` as the only was to track the task, set the
dependencies and submit the task for execution.

### ``task_handle`` as a unique owner

The first option is to keep the ``task_handle`` to be a unique owner of the task in various states and to allow
to use non-empty handle for setting or transferring the dependencies.

Since the current API allows submitting the ``task_handle`` for execution only as rvalue, having any usage of ``task_handle`` object after submitting for execution 
(e.g. using ``task_group::run(std::move(task_handle))``) looks misleading even if some guarantees are provided for the referred handle object.

To handle this, it would be needed to extend all functions that take the task handled by the ``task_handle``
with the new overload taking an non-const lvalue reference and provide the following guarantees:
* Overloads accepting rvalue reference to ``task_handle`` take a non-empty handle and leave the handle in an empty state in the end (current behavior is preserved).
* New overloads accepting lvalue references to ``task_handle`` also take a non-empty handle object but does not leave it in an empty state after submission. Hence, the ``task_handle`` can be
  used after execution of the method to represent a task in submitted, executing or completed state and to set the dependencies on such tasks.
  Using such a task handle once again as an argument to the submission function results in undefined behavior.

Extension for all of the submission functions would be required:
* ``task_group::run`` and ``task_group::run_and_wait``
* ``task_arena::enqueue`` and ``this_task_arena::enqueue``

Also, the submission functions would work only with the handles of the tasks in created states.
Submitting the ``task_handle`` handling tasks in any other state results in undefined behavior.

When the ``task_group`` preview extensions are enabled, returning a non-empty ``task_handle`` handling a task
in the state other than created results in undefined behavior.

### ``task_handle`` as a shared owner

Another approach is to have ``task_handle`` to be a shared owner on the task allowing multiple instances of
``task_handle`` to co-own the task. But since the task can only be submitted for execution once, using a
``task_handle`` as an argument to one of the submission functions would invalidate all copies or set them
in the "weak" state that allows only to set dependencies between tasks.

Open questions:
* Which approach for ``transfer_successors_to`` (merged or separate tracking) should be implemented?
* Are concrete names of APIs good enough and reflects the purpose of the methods?
* Should we allow the ``task_tracker`` in a non-submitted state as a successor argument to ``make_edge`` and
  ``add_successor``?
* Should comparison functions between ``task_tracker`` and ``task_handle`` be defined?
* The performance targets for this feature were not defined by this RFC
* Exit criteria for this feature was not defined by this RFC
