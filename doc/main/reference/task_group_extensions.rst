.. _task_group_extensions:

task_group extensions
=====================

.. note::
    To enable these extensions, set the ``TBB_PREVIEW_TASK_GROUP_EXTENSIONS`` macro to 1.

.. contents::
    :local:
    :depth: 1

Description
***********

|full_name| implementation extends the
`tbb::task_group specification <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/task_scheduler/task_group/task_group_cls>`_
with the following extensions:
1. Task bypassing support, which expands the requirements for user-provided function objects by allowing it to return another task as a hint for further execution.
2. Dynamic Dependencies support, which provide API for establishing predecessor-successor relationships between tasks.

API
***

Header
------

.. code:: cpp

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

TODO insert link

Support for Task Bypassing allows to hint the scheduler which task should be executed next by returning a corresponding ``task_handle`` from the task body.
It is not guaranteed that the task will be actually executed next and by the same thread.

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

Function object ``F`` might return a ``task_handle`` object. If the returned handle is not empty and owns a task without dependencies, it is used as an optimization hint for
a task that would be executed next.

If the returned handle was created using a ``task_group`` differ from ``*this``, the behavior is undefined.

Example
^^^^^^^

Consider an example of implementing a parallel for loop using ``task_group`` and divide-and-conquer pattern.

TODO make me testable

.. code:: cpp
    #define TBB_PREVIEW_TASK_GROUP_EXTENSIONS 1
    #include <oneapi/tbb/task_group.h>

    void foo(std::size_t begin, std::size_t end);

    struct for_task {
        static constexpr std::size_t serial_threshold = 16;
        tbb::task_handle operator()() {
            tbb::task_handle next_task;
            std::size_t size = end - begin;
            if (size < serial_threshold) {
                // Execute the work serially
                foo(begin, end);
            } else {
                // Processed range to big - split it
                std::size_t middle = begin + size / 2;
                tbb::task_handle left_subtask = tg.defer(for_task{begin, middle, tg});
                tbb::task_handle right_subtask = tg.defer(for_task{middle, end, tg});

                // Submit the right subtask for execution
                tg.run(for_task{middle, end, tg});

                // Bypass the left part
                next_task = tg.defer(for_task{begin, middle, tg});
            }
            return next_task;
        }

        std::size_t begin;
        std::size_t end;
        tbb::task_group& tg;
    }; // struct for_task

    int main() {
        tbb::task_group tg;
        // Run the root task
        tg.run_and_wait(for_task{0, N, tg});
    }

Task Dynamic Dependencies
-------------------------

The Dynamic Dependencies APIs allows to establish predecessor-successor dependency between task meaning a successor task can begin execution only after all of its predecessors
are completed.

The task in any state (``created``, ``submitted``, ``executing`` and ``completed``) can be used as predecessors, but only the ``created`` tasks are allowed as successors.

``tbb::task_handle`` represents a task in ``created`` state. The ``task_completion_handle`` object can represent a task in any state.

.. code:: cpp
    tbb::task_handle task = tg.defer(task_body); // task is in created state, represented by task_handle 
    tbb::task_completion_handle comp_handle = task; // task is still in created state, represented by both task_handle and task_completion_handle

    tg.run(std::move(task)); // task is in submitted state, represented by task_completion_handle only
    // Starting from this point, the task can be taken for execution

    // task is in executing state when some thread starts executing task_body
    // Once the task_body is completed, the task is in completed state

    // At any point, comp_handle can be used as a predecessor 

The function ``tbb::task_group::set_task_order(pred, succ)`` establishes a predecessor-successor dependency between ``pred`` and ``succ``:

.. code:: cpp
    tbb::task_handle predecessor = tg.defer(pred_body);
    tbb::task_handle successor = tg.defer(succ_body);

    tbb::task_group::set_task_order(predecessor, successor);

The feature also allows to transfer the completion of the task to another ``created`` task using ``tbb::task_group::transfer_this_task_completion_to``.
This function should be called from inside the task body. All of the successors of the currently executed tasks would be executed after the task
received the completion.

.. code:: cpp
    tbb::task_handle t = tg.defer([] {
        tbb::task_group comp_receiver = tg.defer(receiver_body);
        tbb::task_group::transfer_this_task_completion_to(comp_receiver);
    });

    tbb::task_handle succ = tg.defer(succ_body);

    tbb::task_group::set_task_order(t, succ);
    // Since t body transfers it's completion to comp_receiver
    // succ_body will be executed after receiver_body

``task_completion_handle`` Class
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Constructors
~~~~~~~~~~~~

.. code:: cpp
    task_completion_handle();

Constructs an empty completion handle that does not refer to any task.

.. code:: cpp
    task_completion_handle(const task_handle& handle);

Constructs a completion handle that refers to the task owned by ``handle``.
If ``handle`` is empty, the behavior is undefined.

.. code:: cpp
    task_completion_handle(const task_completion_handle& other);

Copies ``other`` to ``*this``. After this ``*this`` and ``other`` refer to the same task.

.. code:: cpp
    task_completion_handle(task_completion_handle&& other);

Moves ``other`` to ``*this``. After this, ``*this`` refers to the task that was referred by ``other``. ``other`` is left in an empty state.

Destructors
~~~~~~~~~~~

.. code:: cpp

    ~task_completion_handle();

Destroys the completion handle.

Assignment
~~~~~~~~~~

.. code:: cpp
    task_completion_handle& operator=(const task_handle& handle);

Replaces task referred to by ``*this`` with the task owned by ``handle``.
If ``handle`` is empty, the behavior is undefined.

*Returns*: a reference to ``*this``.

.. code:: cpp
    task_completion_handle& operator=(const task_completion_handle& other);

Copy-assigns ``other`` to ``*this``. After this, ``*this`` and ``other`` refer to the same task.

*Returns*: a reference to ``*this``.

.. code:: cpp
    task_completion_handle& operator=(task_completion_handle&& other);

Move assigns ``other`` to ``*this``. After this, ``*this`` refers to the task that was referred by ``other``. ``other`` is left in an empty state.

*Returns*: a reference to ``*this``.

Observers
~~~~~~~~~

.. code:: cpp
    explicit operator bool() const noexcept;

*Returns*: ``true`` if ``*this`` refers to any task, ``false`` otherwise.

Comparison
~~~~~~~~~~

.. code:: cpp
    bool operator==(const task_completion_handle& lhs, const task_completion_handle& rhs) noexcept;

*Returns*: ``true`` if ``lhs`` refers to the same task as ``rhs``, ``false`` otherwise.

.. code:: cpp
    bool operator!=(const task_completion_handle& lhs, const task_completion_handle& rhs) noexcept;

Equivalent to ``!(lhs == rhs)``.

.. code:: cpp
    bool operator==(const task_completion_handle& t, std::nullptr_t) noexcept;

*Returns*: ``true`` if ``t`` is empty, ``false`` otherwise.

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

Destroys the ``task_handle`` object and associated task if it exists.
If the associated task is a predecessor or a successor, the behavior is undefined.

Member Functions of ``task_group`` Class
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code:: cpp
    void run(task_handle&& handle);

Schedules the task object owned by ``handle`` for the execution if the dependencies are satisfied. 

.. note::
    If the task owned by ``handle`` has incomplete predecessors, the task will be scheduled for execution once all of them complete.
    ``run`` function do not wait the predecessors to complete.

.. code:: cpp
    static void set_task_order(task_handle& pred, task_handle& succ);

Registers the task owned by ``pred`` as a predecessor that must complete before the task owned by ``succ`` can begin execution.

It is thread-safe to concurrently add multiple predecessors to a single successor, and to register the same predecessor to multiple successors.

It is thread-safe to concurrently add successors to both the task transferring its completion and the task receiving it.

In any of the following cases, the behavior is undefined:
* ``pred`` or ``succ`` is an empty handle.
* If tasks owned by ``pred`` and ``succ`` belong to different ``task_group``s.

.. code:: cpp
    static void set_task_order(task_completion_handle& pred, task_handle& succ);

Registers the task referred by ``pred`` as a predecessor that must complete before the task owned by ``succ`` can begin execution.

It is thread-safe to concurrently add multiple predecessors to a single successor, and to register the same predecessor to multiple successors.

It is thread-safe to concurrently add successors to both the task transferring its completion and the task receiving it.

In any of the following cases, the behavior is undefined:
* ``pred`` or ``succ`` is empty.
* If the task referred by ``pred`` and the task owned by ``succ`` belong to different ``task_group``s.
* If the task referred by ``pred`` was destroyed without being submitted for execution.

.. code:: cpp
    static void transfer_this_task_completion_to(task_handle& handle);

Transfers the completion of the currently executing task to the task owned by ``handle``.

After the transfer, the successors of the currently executing task will become successors of the task owned by ``handle``.

It is thread-safe to concurrently transfer successors to the task while adding successors to it, or while other threads are adding successors to the
currently executing task.

In any of the following cases, the behavior is undefined:
* ``h`` is an empty handle.
* The function is called outside the body of a ``task_group`` task.
* The function is called for the task, completion ow which was already transferred.
* The currently executing task and the task owned by ``handle`` belong to different ``task_group``s.

Member Functions of ``task_arena`` Class
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code:: cpp
    void enqueue(task_handle&& handle);

Enqueues a task owned by ``handle`` into the ``task_arena`` for processing if the dependencies are satisfied.

.. note::
    If the task owned by ``handle`` has incomplete predecessors, the task will be scheduled for execution once all of them complete.
    ``enqueue`` function do not wait the predecessors to complete.

Example
^^^^^^^

The example above implements the parallel reduction over the range using the API described above:

.. code:: cpp
    struct reduce_task {
        static constexpr std::size_t serial_threshold = 16;

        void operator()() {
            tbb::task_handle next_task;

            std::size_t size = end - begin;
            if (size < serial_threshold) {
                // Do serial reduction
                for (std::size_t i = begin; i < end; ++i) {
                    *result += i;
                }
            } else {
                // The processed range is too big, split it
                std::size_t middle = begin + size / 2;

                std::size_t* left_result = new std::size_t(0);
                tbb::task_handle left_leaf = tg.defer(reduce_task{begin, middle, left_result, tg});

                std::size_t* right_result = new std::size_t(0);
                tbb::task_handle right_leaf = tg.defer(reduce_task{middle, end, right_result, tg});

                tbb::task_handle join_task = tg.defer([]() {
                    *result = *left_result + *right_result;
                    delete left_result;
                    delete right_result;
                });

                tbb::task_group::set_task_order(left_leaf, join_task);
                tbb::task_group::set_task_order(right_leaf, join_task);

                tbb::task_group::transfer_this_task_completion_to(join_task);

                next_task = std::move(left_leaf);

                tg.run(std::move(right_leaf));
                tg.run(join_task);
            }
            return next_task;
        }

        std::size_t begin;
        std::size_t end;
        std::size_t* result;
        tbb::task_group& tg;
    };

    int main() {
        tbb::task_group tg;

        std::size_t reduce_result = 0;
        reduce_task root_reduce_task(0, N, reduce_result, tg);
    }

.. rubric:: See also

* `oneapi::tbb::task_group specification <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/task_scheduler/task_group/task_group_cls>`_
* `oneapi::tbb::task_group_context specification <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/task_scheduler/scheduling_controls/task_group_context_cls>`_
* `oneapi::tbb::task_group_status specification <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/task_scheduler/task_group/task_group_status_enum>`_ 
* `oneapi::tbb::task_handle class <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/task_scheduler/task_group/task_handle>`_
