.. _parallel_for_func

parallel_for
============
**[algorithms.parallel_for]**

Function template that performs parallel iteration over a range of values.

.. code:: cpp

    // Defined in header <oneapi/tbb/parallel_for.h>

    namespace oneapi {
        namespace tbb {

            template<typename Index, typename Func>
            void parallel_for(Index first, Index last, const Func& f, /* see-below */ partitioner, task_group_context& context);
            template<typename Index, typename Func>
            void parallel_for(Index first, Index last, const Func& f, task_group_context& context);
            template<typename Index, typename Func>
            void parallel_for(Index first, Index last, const Func& f, /* see-below */ partitioner);
            template<typename Index, typename Func>
            void parallel_for(Index first, Index last, const Func& f);

            template<typename Index, typename Func>
            void parallel_for(Index first, Index last, Index step, const Func& f, /* see-below */ partitioner, task_group_context& context);
            template<typename Index, typename Func>
            void parallel_for(Index first, Index last, Index step, const Func& f, task_group_context& context);
            template<typename Index, typename Func>
            void parallel_for(Index first, Index last, Index step, const Func& f, /* see-below */ partitioner);
            template<typename Index, typename Func>
            void parallel_for(Index first, Index last, Index step, const Func& f);

            template<typename Range, typename Body>
            void parallel_for(const Range& range, const Body& body, /* see-below */ partitioner, task_group_context& context);
            template<typename Range, typename Body>
            void parallel_for(const Range& range, const Body& body, task_group_context& context);
            template<typename Range, typename Body>
            void parallel_for(const Range& range, const Body& body, /* see-below */ partitioner);
            template<typename Range, typename Body>
            void parallel_for(const Range& range, const Body& body);

        } // namespace tbb
    } // namespace oneapi

A ``partitioner`` type may be one of the following entities:

* ``const auto_partitioner&``
* ``const simple_partitioner&``
* ``const static_partitioner&``
* ``affinity_partitioner&``

Requirements:

* The ``Range`` type must meet the :doc:`Range requirements <../../named_requirements/algorithms/range>`.
* The ``Body`` type must meet the :doc:`ParallelForBody requirements <../../named_requirements/algorithms/par_for_body>`.
* The ``Index`` type must meet the :doc:`ParallelForIndex requirements <../../named_requirements/algorithms/par_for_index>`.
* The ``Func`` type must meet the :doc:`ParallelForFunc requirements <../../named_requirements/algorithms/par_for_func>`.

The ``oneapi::tbb::parallel_for(first, last, step, f)`` overload represents parallel execution of the loop:

.. code:: cpp

    for (auto i = first; i < last; i += step) f(i);

The loop must not wrap around. The step value must be positive. If omitted, it is implicitly 1. 
There is no guarantee that the iterations run in parallel. A deadlock may occur if a lesser 
iteration waits for a greater iteration. The partitioning strategy is ``auto_partitioner`` when 
the parameter is not specified.

The ``parallel_for(range,body,partitioner)`` overload provides a more general form of parallel
iteration. It represents parallel execution of ``body`` over each value
in ``range``. The optional ``partitioner`` parameter specifies a partitioning strategy.

``parallel_for`` recursively splits the range into subranges to the point such that ``is_divisible()``
is false for each subrange, and makes copies of the body for each of these subranges.
For each such body/subrange pair, it invokes the ``body``.

Some of the copies of the range and body may be destroyed after ``parallel_for`` returns.
This late destruction is not an issue in typical usage, but is something to be aware of
when looking at execution traces or writing range or body objects with complex side effects.

``parallel_for`` may execute iterations in non-deterministic order.
Do not rely on any particular execution order for correctness. However, for efficiency, do expect
``parallel_for`` to tend towards operating on consecutive runs of values.

In case of serial execution, ``parallel_for`` performs iterations from left to right.

All overloads can accept a :doc:`task_group_context <../../task_scheduler/scheduling_controls/task_group_context_cls>` object
so that the algorithm’s tasks are executed in this context. By default, the algorithm is executed in a bound context of its own.

**Complexity**

If the range and body take *O(1)* space, and the range splits into nearly equal pieces,
the space complexity is *O(P log(N))*, where *N* is the size of the range and *P* is the number of threads.

Examples
--------

Parallel Average
****************

This example sets ``output[i]`` to the average
of ``input[i-1]``, ``input[i]``, and ``input[i+1]``, for each ``i`` in a range ``[1, n)``.

.. literalinclude:: ./examples/parallel_for_example.cpp
    :language: c++
    :start-after: /*begin_parallel_for_average_example*/
    :end-before: /*end_parallel_for_average_example*/

Parallel Merge
**************

This example shows the power of ``parallel_for`` beyond flat iteration spaces.

The code performs a parallel merge of two sorted sequences. It works for any sequence with a random-access
iterator.

The algorithm (Akl 1987) works recursively as follows:
1. If the sequences are too short for effective use of parallelism, do a sequential merge.
2. Swap the sequences if necessary, so that the first sequence ``[begin1, end1)`` is at
   least as long as the second sequence ``[begin2, end2)``.
3. Set ``m1`` to the middle position in ``[begin1, end1)``. Call the item at that location *key*.
4. Set ``m2`` to where *key* would fall in ``[begin2, end2)``.
5. Merge ``[begin1, m1)`` and ``[begin2, m2)`` to create the first part of the merged
   sequence.
6. Merge ``[m1, end1)`` and ``[m2, end2)`` to create the second part of the merged
   sequence.

The example below implements this algorithm using the range object to perform most
of the steps.

Predicate ``is_divisible`` performs the test in steps 1 and 2.

The splitting constructor does steps 3-6.

The body object does the sequential merges.

.. literalinclude:: ./examples/parallel_for_example.cpp
    :language: c++
    :start-after: /*begin_parallel_for_merge_example*/
    :end-before: /*end_parallel_for_merge_example*/

See also:

* :ref:`Partitioners <Partitioners>`
