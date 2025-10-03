.. _task_group_extensions:

task_group extensions
=====================

.. note::
    To enable these extensions, define the ``TBB_PREVIEW_TASK_GROUP_EXTENSIONS`` macro with a value of ``1``.

.. contents::
    :local:
    :depth: 3

Description
***********

The |full_name| implementation extends the
`tbb::task_group specification <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/task_scheduler/task_group/task_group_cls>`_
with the following extensions:

1. Task bypassing support extends the requirements for user-provided function objects by allowing them to return a ``task_handle`` as a hint for subsequent execution.
2. Dynamic Dependencies support introduces an API for defining predecessor-successor relationships between tasks.

API
***

Header
------

.. code:: cpp

    #define TBB_PREVIEW_TASK_GROUP_EXTENSIONS 1
    #include <oneapi/tbb/task_group.h>
    #include <oneapi/tbb/task_arena.h>

Synopsis
--------

.. code:: cpp

    // <oneapi/tbb/task_group.h> synopsis
    namespace oneapi {
        namespace tbb {
            class task_handle {
            public:
                // Only the requirements for destroyed object are changed
                ~task_handle();
            };

            class task_completion_handle {
            public:
                task_completion_handle();

                task_completion_handle(const task_handle& handle);
                task_completion_handle(const task_completion_handle& other);
                task_completion_handle(task_completion_handle&& other);

                ~task_completion_handle();

                task_completion_handle& operator=(const task_handle& handle);
                task_completion_handle& operator=(const task_completion_handle& other);
                task_completion_handle& operator=(task_completion_handle&& other);
   
                explicit operator bool() const noexcept;

                friend bool operator==(const task_completion_handle& lhs, const task_completion_handle& rhs) noexcept;
                friend bool operator!=(const task_completion_handle& lhs, const task_completion_handle& rhs) noexcept;

                friend bool operator==(const task_completion_handle& t, std::nullptr_t) noexcept;
                friend bool operator!=(const task_completion_handle& t, std::nullptr_t) noexcept;

                friend bool operator==(std::nullptr_t, const task_completion_handle& t) noexcept;
                friend bool operator!=(std::nullptr_t, const task_completion_handle& t) noexcept;
            }; // class task_completion_handle

            class task_group {
                // Only the requirements for the return type of function F are changed
                template <typename F>
                task_handle defer(F&& f);

                // Only the requirements for the return type of function F are changed
                template <typename F>
                task_group_status run_and_wait(const F& f);

                // Only the requirements for the return type of function F are changed
                template <typename F>
                void run(F&& f);

                // Only the behavior in case of dependent tasks is changed
                void run(task_handle&& handle);

                static void set_task_order(task_handle& pred, task_handle& succ);
                static void set_task_order(task_completion_handle& pred, task_handle& succ);

                static void transfer_this_task_completion_to(task_handle& handle);
            };
        
        } // namespace tbb
    } // namespace oneapi

.. code:: cpp

    // // <oneapi/tbb/task_arena.h> synopsis
    namespace oneapi {
        namespace tbb {
            class task_arena {
                // Only the behavior in case of dependent tasks is changed
                void enqueue(task_handle&& handle);
            }; // class task_arena

            namespace this_task_arena {
                // Only the behavior in case of dependent tasks is changed
                void enqueue(task_handle&& handle);
            } // namespace this_task_arena
        } // namespace tbb
    } // namespace oneapi

Task Bypassing support
----------------------

`Task Bypassing <../tbb_userguide/Task_Scheduler_Bypass.html>` support allows developers to reduce task scheduling overhead by providing a hint to the scheduler
about which task should be executed next by returning a corresponding ``task_handle`` object from the task body.
Execution of the hinted task is not guaranteed to occur immediately, nor to be performed by the same thread.

.. code:: cpp

    tbb::task_handle task_body() {
        tbb::task_handle next_task = group.defer(next_task_body);
        return next_task;
    }

Member Functions of ``task_group`` Class
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code:: cpp

    template <typename F>
    task_handle defer(F&& f);

    template <typename F>
    task_group_status run_and_wait(const F& f);

    template <typename F>
    void run(F&& f);

The function object ``F`` may return a ``task_handle`` object. If the returned handle is non-empty and owns a task without dependencies, it serves as an optimization
hint for a task that could be executed next.

If the returned handle was created by a ``task_group`` other than ``*this``, the behavior is undefined.

Example
^^^^^^^

Consider an example of implementing a parallel for loop using ``task_group`` and divide-and-conquer pattern.

.. literalinclude:: .examples/task_group_extensions_bypassing.cpp
    :language: c++
    :start-after: /*begin_task_group_extensions_bypassing_example*/
    :end-before: /*end_task_group_extensions_bypassing_example*/

Task Dynamic Dependencies
-------------------------

The Dynamic Dependencies API enables developers to define predecessor-successor relationships between tasks,
meaning a successor task can begin execution only after all of its predecessors are completed.

Tasks in any state (``created``, ``submitted``, ``executing``, or ``completed``) can serve as predecessors, but only tasks in the ``created`` state may be used as successors.

A ``tbb::task_handle`` represents a task in the ``created`` state, while a ``task_completion_handle`` can represent a task in any state.

.. code:: cpp

    tbb::task_handle task = tg.defer(task_body); // The task is in the created state and is represented by a task_handle 
    tbb::task_completion_handle comp_handle = task; // The task remains in the created state and is represented by both task_handle and task_completion_handle

    tg.run(std::move(task)); // The task is in the submitted state, represented by task_completion_handle only
    // From this point onward, the task becomes eligible for execution

    // The task enters the executing state when a thread begins executing task_body
    // Once task_body completes, the task transitions to the completed state

    // At any stage, comp_handle may be used as a predecessor 

The ``tbb::task_group::set_task_order(pred, succ)`` function establishes a dependency such that ``succ`` cannot begin execution until ``pred`` has completed.

.. code:: cpp

    tbb::task_handle predecessor = tg.defer(pred_body);
    tbb::task_handle successor = tg.defer(succ_body);

    tbb::task_group::set_task_order(predecessor, successor);

The feature also allows transferring the completion of the currently executing task to another ``created`` task using ``tbb::task_group::transfer_this_task_completion_to``.
This function must be invoked from within the task body. All successors of the currently executing task will execute only after the task receiving the completion has finished.

.. code:: cpp

    tbb::task_handle t = tg.defer([] {
        tbb::task_group comp_receiver = tg.defer(receiver_body);
        tbb::task_group::transfer_this_task_completion_to(comp_receiver);
    });

    tbb::task_handle succ = tg.defer(succ_body);

    tbb::task_group::set_task_order(t, succ);
    // Since t transfers its completion to comp_receiver,
    // succ_body will execute after receiver_body

``task_completion_handle`` Class
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Constructors
~~~~~~~~~~~~

.. code:: cpp

    task_completion_handle();

Constructs an empty ``task_completion_handle`` that does not refer to any task.

.. code:: cpp

    task_completion_handle(const task_handle& handle);

Constructs a ``task_completion_handle`` that refers to the task associated with ``handle``.
If ``handle`` is empty, the behavior is undefined.

.. code:: cpp

    task_completion_handle(const task_completion_handle& other);

Copies ``other`` into ``*this``. After the copy, both ``*this`` and ``other`` refer to the same task.

.. code:: cpp

    task_completion_handle(task_completion_handle&& other);

Moves ``other`` into ``*this``. After the move, ``*this`` refers to the task previously referenced by ``other``, which is left in an empty state.

Destructors
~~~~~~~~~~~

.. code:: cpp

    ~task_completion_handle();

Destroys the ``task_completion_handle``.

Assignment
~~~~~~~~~~

.. code:: cpp

    task_completion_handle& operator=(const task_handle& handle);

Replaces the task referenced by ``*this`` with the task associated with ``handle``.
If ``handle`` is empty, the behavior is undefined.

*Returns*: a reference to ``*this``.

.. code:: cpp

    task_completion_handle& operator=(const task_completion_handle& other);

Performs copy assignment from ``other`` to ``*this``. After the assignment, both refer to the same task.

*Returns*: a reference to ``*this``.

.. code:: cpp

    task_completion_handle& operator=(task_completion_handle&& other);

Performs move assignment from ``other`` to ``*this``. After the move, ``*this`` refers to the task previously referenced by ``other``, which is left empty.

*Returns*: a reference to ``*this``.

Observers
~~~~~~~~~

.. code:: cpp

    explicit operator bool() const noexcept;

*Returns*: ``true`` if ``*this`` references a task; otherwise, ``false``.

Comparison
~~~~~~~~~~

.. code:: cpp

    bool operator==(const task_completion_handle& lhs, const task_completion_handle& rhs) noexcept;

*Returns*: ``true`` if ``lhs`` and ``rhs`` reference the same task; otherwise, ``false``.

.. code:: cpp

    bool operator!=(const task_completion_handle& lhs, const task_completion_handle& rhs) noexcept;

Equivalent to ``!(lhs == rhs)``.

.. code:: cpp

    bool operator==(const task_completion_handle& t, std::nullptr_t) noexcept;

*Returns*: ``true`` if ``t`` does not reference any task; otherwise, ``false``.

.. code:: cpp

    bool operator!=(const task_completion_handle& t, std::nullptr_t) noexcept;

Equivalent to ``!(t == nullptr)``.

.. code:: cpp

    bool operator==(std::nullptr_t, const task_completion_handle& t) noexcept;

Equivalent to ``t == nullptr``.

.. code:: cpp

    bool operator!=(std::nullptr_t, const task_completion_handle& t) noexcept;

Equivalent to ``!(t == nullptr)``.

Member Functions of ``task_handle`` Class
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code:: cpp

    ~task_handle();

Destroys the ``task_handle`` object and its associated task, if any.
If the associated task is involved in a predecessor-successor relationship, the behavior is undefined.

Member Functions of ``task_group`` Class
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code:: cpp

    void run(task_handle&& handle);

Schedules the task object associated with ``handle`` for execution, provided its dependencies are satisfied.

.. note::
    If the task associated with ``handle`` has incomplete predecessors, it will be scheduled for execution once all of them have completed.
    The ``run`` function does not wait for predecessors to complete.

.. code:: cpp

    static void set_task_order(task_handle& pred, task_handle& succ);

Registers the task associated with ``pred`` as a predecessor that must complete before the task associated with ``succ`` can begin execution.

It is thread-safe to concurrently add multiple predecessors to a single successor and to register the same predecessor with multiple successors.

It is thread-safe to concurrently add successors to both the task transferring its completion and the task receiving the completion.

The behavior is undefined in the following cases:

* Either ``pred`` or ``succ`` is an empty.
* The tasks referenced by ``pred`` and ``succ`` belong to different ``task_group`` instances.

.. code:: cpp

    static void set_task_order(task_completion_handle& pred, task_handle& succ);

Registers the task referenced by ``pred`` as a predecessor that must complete before the task associated with ``succ`` can begin execution.

It is thread-safe to concurrently add multiple predecessors to a single successor and to register the same predecessor with multiple successors.

It is thread-safe to concurrently add successors to both the task transferring its completion and the task receiving the completion.

The behavior is undefined in the following cases:

* Either ``pred`` or ``succ`` is empty.
* The tasks referred by ``pred`` and ``succ`` belong to different ``task_group`` instances.
* The task referred by ``pred`` was destroyed before begin submitted for execution.

.. code:: cpp

    static void transfer_this_task_completion_to(task_handle& handle);

Transfers the completion of the currently executing task to the task associated with ``handle``.

After the transfer, the successors of the currently executing task will be reassigned to the task associated with ``handle``.

It is thread-safe to transfer successors to the task while concurrently adding successors to it or to the currently executing task.

The behavior is undefined in the following cases:

* ``handle`` is empty.
* The function is called outside the body of a ``task_group`` task.
* The function is called for the task whose completion has already been transferred.
* The currently executing task and the task associated with ``handle`` belong to different ``task_group`` instances.

Member Functions of ``task_arena`` Class
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code:: cpp

    void enqueue(task_handle&& handle);

Enqueues the task associated with ``handle`` into the ``task_arena`` for processing, provided its dependencies are satisfied.

.. note::

    If the task associated with ``handle`` has incomplete predecessors, it will be scheduled for execution once all of them have completed.
    The ``enqueue`` function does not wait for predecessors to complete.

Example
^^^^^^^

The following example demonstrates how to perform parallel reduction over a range using the described API.

.. code:: cpp

    struct reduce_task {
        static constexpr std::size_t serial_threshold = 16;

        void operator()() {
            tbb::task_handle next_task;

            std::size_t size = end - begin;
            if (size < serial_threshold) {
                // Perform serial reduction
                for (std::size_t i = begin; i < end; ++i) {
                    *result += i;
                }
            } else {
                // The range is too large to process directly
                // Divide it into smaller segments for parallel execution
                std::size_t middle = begin + size / 2;

                std::shared_ptr<std::size_t> left_result = std::make_shared<std::size_t>(0);
                tbb::task_handle left_leaf = tg.defer(reduce_task{begin, middle, left_result, tg});

                std::shared_ptr<std::size_t> right_result = std::make_shared<std::size_t>(0);
                tbb::task_handle right_leaf = tg.defer(reduce_task{middle, end, right_result, tg});

                tbb::task_handle join_task = tg.defer([=]() {
                    *result = *left_result + *right_result;
                });

                tbb::task_group::set_task_order(left_leaf, join_task);
                tbb::task_group::set_task_order(right_leaf, join_task);

                tbb::task_group::transfer_this_task_completion_to(join_task);

                // Save the left leaf for further bypassing
                next_task = std::move(left_leaf);

                tg.run(std::move(right_leaf));
                tg.run(join_task);
            }

            return next_task;
        }

        std::size_t begin;
        std::size_t end;
        std::shared_ptr<std::size_t> result;
        tbb::task_group& tg;
    };

    int main() {
        tbb::task_group tg;

        std::shared_ptr<std::size_t> reduce_result = std::make_shared<std::size_t>(0);
        reduce_task root_reduce_task(0, N, reduce_result, tg);
    }

.. rubric:: See also

* `oneapi::tbb::task_group specification <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/task_scheduler/task_group/task_group_cls>`_
* `oneapi::tbb::task_group_context specification <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/task_scheduler/scheduling_controls/task_group_context_cls>`_
* `oneapi::tbb::task_group_status specification <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/task_scheduler/task_group/task_group_status_enum>`_ 
* `oneapi::tbb::task_handle class <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/task_scheduler/task_group/task_handle>`_
