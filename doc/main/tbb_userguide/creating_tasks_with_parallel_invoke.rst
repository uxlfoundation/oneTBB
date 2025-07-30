.. _creating_tasks_with_parallel_invoke:

Creating Tasks with parallel_invoke
===================================

Suppose you want to search a binary tree for the node that contains a specific value.
Here is sequential code to do this:

.. literalinclude:: ./examples/task_examples.cpp
    :language: c++
    :start-after: /*begin_search_serial*/
    :end-before: /*end_search_serial*/


To improve performance, you can use ``oneapi::tbb::parallel_invoke`` to search the tree 
in parallel:

.. literalinclude:: ./examples/task_examples.cpp
    :language: c++
    :start-after: /*begin_parallel_invoke_search*/
    :end-before: /*end_parallel_invoke_search*/

The function ``oneapi::tbb::parallel_invoke`` runs multiple independent tasks in parallel.
In this example, two lambdas are passed that define tasks that search the left and right subtrees of the
current node in parallel. If the value is found the pointer to the node that contains the value is stored
in the ``std::atomic<TreeNode*> result``. This example uses recursion to create many tasks, instead of
just two. The depth of the parallel recursion is limited by the ``depth_threshold`` parameter. After this depth is
reached, the search falls back to a sequential approach. The value of ``result`` is periodically checked
to see if the value has been found by other concurrent tasks, and if so, the search in the current task is
terminated.

Because ``oneapi::tbb::parallel_invoke`` is a fork-join algorithm, each level of the recursion does not
complete until both the left and right subtrees have completed.