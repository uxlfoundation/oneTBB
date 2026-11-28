.. _Cook_Until_Done_parallel_do:

Cook Until Done: parallel_for_each
==================================


For some loops, the end of the iteration space is not known in advance,
or the loop body may add more iterations to do before the loop exits.
You can deal with both situations using the function template ``oneapi::tbb::parallel_for_each``.


A linked list is an example of an iteration space that is not known in
advance. In parallel programming, it is usually better to use dynamic
arrays instead of linked lists, because accessing items in a linked list
is inherently serial. But if you are limited to linked lists, the items
can be safely processed in parallel, and processing each item takes at
least a few thousand instructions, you can use ``parallel_for_each`` to
gain some parallelism.


For example, consider the following serial code, where ``Foo`` accepts a
``const Item&``:


.. literalinclude:: ./examples/cook_until_done.cpp
    :language: cpp
    :start-after: /*begin_serial_apply_foo*/
    :end-before: /*end_serial_apply_foo*/


If ``Foo`` takes at least a few thousand instructions to run, you can
get parallel speedup by converting the loop to use
``parallel_for_each``. To do so, define an object with a ``const``
qualified ``operator()``. This is similar to a C++ function object from
the C++ standard header ``<functional>``, except that ``operator()``
must be ``const``.


.. literalinclude:: ./examples/cook_until_done.cpp
    :language: cpp
    :start-after: /*begin_apply_foo*/
    :end-before: /*end_apply_foo*/


The parallel form of ``SerialApplyFooToList`` is as follows:


.. literalinclude:: ./examples/cook_until_done.cpp
    :language: cpp
    :start-after: /*begin_parallel_apply_foo*/
    :end-before: /*end_parallel_apply_foo*/


An invocation of ``parallel_for_each`` never causes two threads to act
on an input iterator concurrently. Thus typical definitions of input
iterators for sequential programs work correctly. This convenience makes
``parallel_for_each`` unscalable, because the fetching of work is
serial. But in many situations, you still get useful speedup over doing
things sequentially.


There are two ways that ``parallel_for_each`` can acquire work scalably.


-  The iterators can be random-access iterators.


-  The body argument to ``parallel_for_each``, if it takes a second
   argument *feeder* of type ``oneapi::tbb::feeder<Item>&``, can add more
   work by calling ``feeder.add(item)``. For example, suppose processing
   a node in a tree is a prerequisite to processing its descendants.
   With ``parallel_for_each``, after processing a node, you could use
   ``feeder.add`` to add the descendant nodes. The invocation of
   ``parallel_for_each`` does not terminate until all items have been
   processed.
