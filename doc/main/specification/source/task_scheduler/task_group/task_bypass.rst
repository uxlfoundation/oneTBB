.. _task_bypass_support:

Task Bypass Support for ``task_group`` (preview)
================================================

.. note::

    To enable this extension, define the ``TBB_PREVIEW_TASK_GROUP_EXTENSIONS`` macro to ``1``.


The |full_name| implementation extends the requirements for user-provided function object from
:ref:`tbb::task_group<../task_group_cls>` to allow them to return a ``task_handle`` object.

`Task Bypassing <../../../../../tbb_userguide/Task_Scheduler_Bypass.html>`_ allows developers to reduce task scheduling overhead by providing a hint about
which task should be executed next.

Execution of the deferred task owned by a returned ``task_handle`` is not guaranteed to occur immediately, nor to be performed by the same thread.

.. code:: cpp

    tbb::task_handle task_body() {
        tbb::task_handle next_task = group.defer(next_task_body);
        return next_task;
    }

.. code:: cpp

    // Defined in header <oneapi/tbb/task_group.h>

    namespace oneapi {
        namespace tbb {
            class task_group {
            public:
                // Only the requirements for the return type of function F are changed
                template <typename F>
                task_handle defer(F&& f);

                // Only the requirements for the return type of function F are changed
                template <typename F>
                task_group_status run_and_wait(const F& f);

                // Only the requirements for the return type of function F are changed
                template <typename F>
                void run(F&& f);
            }; // class task_group
        } // namespace tbb
    } // namespace oneapi

Member Functions
----------------

.. code:: cpp

    template <typename F>
    task_handle defer(F&& f);

    template <typename F>
    task_group_status run_and_wait(const F& f);

    template <typename F>
    void run(F&& f);

The ``F`` type must meet the *Function Objects* requirements described in the [function.objects] section of the ISO C++ Standard.

.. admonition:: Extension

    ``F`` may return a ``task_handle`` object. If the returned handle is non-empty and owns a task without dependencies, it serves as an optimization hint
    for a task that could be executed next.

    The returned ``task_handle`` must not be explicitly submitted with ``task_group::run`` or another submission function, otherwise, the behavior is undefined.

    If the returned handle was created by a ``task_group`` other than ``*this``, the behavior is undefined.

Example
-------

The example below demonstrates how to process a sequence in parallel using ``task_group`` and the divide-and-conquer pattern.

.. literalinclude:: ../examples/task_group_extensions_bypassing.cpp
    :language: c++
    :start-after: /*begin_task_group_extensions_bypassing_example*/
    :end-before: /*end_task_group_extensions_bypassing_example*/
