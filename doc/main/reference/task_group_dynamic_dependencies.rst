.. _task_group_dynamic_dependencies:

``task_group`` Dynamic Dependencies
===================================

.. note::
    To enable this extension, define the ``TBB_PREVIEW_TASK_GROUP_EXTENSIONS`` macro with a value of ``1``.

.. contents::
    :local:
    :depth: 2

Description
***********

The |full_name| implementation extends the
`tbb::task_group specification <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/task_scheduler/task_group/task_group_cls>`_
with an API for defining predecessor-successor relationships between tasks,
such that a successor task can begin execution only after all of its predecessors are completed.


Tasks in any state (``created``, ``submitted``, ``executing``, or ``completed``) can serve as predecessors, but only tasks in the ``created`` state may be used as successors.

A ``tbb::task_handle`` represents a task in the ``created`` state, while a ``task_completion_handle`` can represent a task in any state.

.. code:: cpp

    tbb::task_handle task = tg.defer(task_body);
    // The task is in the created state and is represented by a task_handle 
    tbb::task_completion_handle comp_handle = task;
    // The task remains in the created state
    // It is represented by both task_handle and task_completion_handle

    tg.run(std::move(task));
    // The task is in the submitted state, represented by task_completion_handle only
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

                friend bool operator==(const task_completion_handle& lhs,
                                       const task_completion_handle& rhs) noexcept;
                friend bool operator!=(const task_completion_handle& lhs,
                                       const task_completion_handle& rhs) noexcept;

                friend bool operator==(const task_completion_handle& t, std::nullptr_t) noexcept;
                friend bool operator!=(const task_completion_handle& t, std::nullptr_t) noexcept;

                friend bool operator==(std::nullptr_t, const task_completion_handle& t) noexcept;
                friend bool operator!=(std::nullptr_t, const task_completion_handle& t) noexcept;
            }; // class task_completion_handle

            class task_group {
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

``task_completion_handle`` Class
--------------------------------

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
-----------------------------------------

.. code:: cpp

    ~task_handle();

Destroys the ``task_handle`` object and its associated task, if any.
If the associated task is involved in a predecessor-successor relationship, the behavior is undefined.

Member Functions of ``task_group`` Class
----------------------------------------

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
* The task referred by ``pred`` was destroyed before being submitted for execution.

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
----------------------------------------

.. code:: cpp

    void enqueue(task_handle&& handle);

Enqueues the task associated with ``handle`` into the ``task_arena`` for processing, provided its dependencies are satisfied.

.. note::

    If the task associated with ``handle`` has incomplete predecessors, it will be scheduled for execution once all of them have completed.
    The ``enqueue`` function does not wait for predecessors to complete.

Example
-------

The following example demonstrates how to perform parallel reduction over a range using the described API.

.. literalinclude:: ./examples/task_group_extensions_reduction.cpp
    :language: c++
    :start-after: /*begin_task_group_extensions_reduction_example*/
    :end-before: /*end_task_group_extensions_reduction_example*/
