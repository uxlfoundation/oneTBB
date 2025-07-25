.. _Creating_Tasks_with_task_group:

Creating Tasks with task_group
==============================

The |full_name| library supports parallelization directly with tasks. The class 
``oneapi::tbb::task_group`` is used to run and wait for independent tasks in a less
structured way than ``oneapi::tbb::parallel_invoke``. It is useful when you want to
create a set of tasks that can be run in parallel, but you do not know how many there
will be in advance.

Here is code that uses ``oneapi::tbb::task_group`` to implement a parallel search in
a binary tree:

.. literalinclude:: ./examples/task_examples.cpp
    :language: c++
    :start-after: /*begin_parallel_search*/
    :end-before: /*end_parallel_search*/

In ``parallel_search_impl``, ``task_group::run`` is used to create a new task for searching
in the right subtree. Unlike ``parallel_invoke``, the recursion does not wait at each level
and reuses the current task to search in the left subtree. This can reduce the
overhead of task creation and management, allowing for more efficient use of resources.

This example uses recursion to create many tasks. The depth of the parallel recursion is
limited by the ``depth_threshold`` parameter. After this depth is reached, no new tasks
are created. The value of ``result`` is periodically checked to see if the value has been
found by other concurrent tasks, and if so, the search in the current task is terminated.

In this example, tasks that execute within the ``task_group tg`` add additional tasks
by calling ``run`` on the same ``task_group`` object. These calls are thread-safe and the 
call to ``tg.wait()`` will block until all of these tasks are complete. Although these
additional tasks might be added from different worker threads, these additions are logically nested
within the top-most calls to ``tg.run``. There is therefore no race in adding these tasks from
the worker threads and waiting for them in the main thread.