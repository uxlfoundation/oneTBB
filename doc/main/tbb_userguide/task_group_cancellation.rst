.. _task_group_cancellation:

task_group cancellation
=======================

The |full_name| library provides a way to cancel tasks using
``oneapi::tbb::task_group_context``. This is useful when you want
to stop the execution of tasks that are no longer needed or when a
condition is met that makes further execution unnecessary. 

Here is an example of how to use task cancellation with ``oneapi::tbb::task_group``
using an explicit ``oneapi::tbb::task_group_context``:

.. literalinclude:: ./examples/task_examples.cpp
    :language: c++
    :start-after: /*begin_parallel_search_cancellation*/
    :end-before: /*end_parallel_search_cancellation*/

The call to ``ctx.cancel_group_execution()`` cancels the tasks in the ``task_group``
that have been submitted but have not started to execute. The task scheduler will not
execute these tasks. Those tasks that have already started will not be interrupted
but they can query the cancellation status of the ``task_group_context`` by calling
``ctx.is_group_execution_cancelled()`` and exit early if they detect cancellation.