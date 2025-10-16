.. _rvalue_reduce:

Parallel Reduction for rvalues
==============================

.. contents::
    :local:
    :depth: 1

.. role:: specadd

.. raw:: html

   <style> .specadd {background-color:palegreen;} </style>

Description
***********

|full_name| implementation extends the `ParallelReduceFunc <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/named_requirements/algorithms/par_reduce_func>`_ and
`ParallelReduceReduction <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/named_requirements/algorithms/par_reduce_reduction>`_
to optimize operating with ``rvalues`` using functional form of ``tbb::parallel_reduce`` and ``tbb::parallel_deterministic_reduce`` algorithms.

Extensions
**********

Additions to the API specifications are :specadd:`highlighted`.

**ParallelReduceFunc Requirements: Pseudo-Signature, Semantics**

.. container:: specadd

   .. cpp:function:: Value Func::operator()(const Range& range, Value&& x) const

   or

.. cpp:function:: Value Func::operator()(const Range& range, const Value& x) const

    Accumulates the result for a subrange, starting with initial value ``x``.
    The ``Range`` type must meet the
    `Range requirements <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/named_requirements/algorithms/range>`_.
    The ``Value`` type must be the same as a corresponding template parameter for the
    `parallel_reduce algorithm <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/algorithms/functions/parallel_reduce_func>`_.

    :specadd:`If both ``rvalue`` and ``lvalue`` forms are provided, the ``rvalue`` is preferred.`

**ParallelReduceReduction Requirements: Pseudo-Signature, Semantics**

.. container:: specadd

   .. cpp:function:: Value Reduction::operator()(Value&& x, Value&& y) const

   or

.. cpp:function:: Value Reduction::operator()(const Value& x, const Value& y) const

    Combines the ``x`` and ``y`` results.
    The ``Value`` type must be the same as a corresponding template parameter for the
    `parallel_reduce algorithm <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/algorithms/functions/parallel_reduce_func>`_.

    :specadd:`If both ``rvalue`` and ``lvalue`` forms are provided, the ``rvalue`` is preferred.`

Example
*******

.. literalinclude:: ./examples/rvalue_reduce.cpp
    :language: c++
    :start-after: /*begin_rvalue_reduce_example*/
    :end-before: /*end_rvalue_reduce_example*/

.. rubric:: See also

* `oneapi::tbb::parallel_reduce specification <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/algorithms/functions/parallel_reduce_func>`_
* `oneapi::tbb::parallel_deterministic_reduce specification <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/algorithms/functions/parallel_deterministic_reduce_func>`_
* `ParallelReduceFunc specification <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/named_requirements/algorithms/par_reduce_func>`_
* `ParallelReduceReduction specification <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/named_requirements/algorithms/par_reduce_reduction>`_
