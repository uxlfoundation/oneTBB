.. _blocked_nd_range_ctad:

Deduction Guides for ``blocked_nd_range`` class
===============================================

.. note::
    To enable this feature, define the ``TBB_PREVIEW_BLOCKED_ND_RANGE_DEDUCTION_GUIDES`` macro to 1.

.. contents::
    :local:
    :depth: 1

Description
***********

oneTBB ``blocked_nd_range`` class represents a recursively divisible N-dimensional half-open interval for oneTBB
parallel algorithms. This feature extends it to support Class Template Argument Deduction feature (starting from C++17) that allows
dropping the explicit template arguments specification while creating a ``blocked_nd_range`` object if they can be determined
using the arguments provided to the constructor:

.. code:: cpp

    oneapi::tbb::blocked_range<int> range1(0, 100);
    oneapi::tbb::blocked_range<int> range2(0, 200);
    oneapi::tbb::blocked_range<int> range3(0, 300);

    // Since 3 unidimensional ranges of type int are provided, the type of nd_range
    // can be deduced as oneapi::tbb::blocked_nd_range<int, 3>
    oneapi::tbb::blocked_nd_range nd_range(range1, range2, range3);

API
***

.. code:: cpp
    
    #include <oneapi/tbb/blocked_nd_range.h>

Synopsis
--------

.. code:: cpp

    namespace oneapi {
    namespace tbb {

        template <typename Value, unsigned int N>
        class blocked_nd_range {
        public:
            // Member types and constructors defined as part of oneTBB specification
            using value_type = Value;
            using dim_range_type = blocked_range<value_type>;
            using size_type = typename dim_range_type::size_type;

            blocked_nd_range(const dim_range_type& dim0, /*exactly N arguments of type const dim_range_type&*/); // [1]
            blocked_nd_range(const value_type (&dim_size)[N], size_type grainsize = 1);                          // [2]
            blocked_nd_range(blocked_nd_range& r, split);                                                        // [3]
            blocked_nd_range(blocked_nd_range& r, proportional_split);                                           // [4]
        }; // class blocked_nd_range

        // Explicit deduction guides
        template <typename Value, typename... Values>
        blocked_nd_range(blocked_range<Value>, blocked_range<Values>...)
        -> blocked_nd_range<Value, 1 + sizeof...(Values)>;

        template <typename Value, unsigned int... Ns>
        blocked_nd_range(const Value (&...)[Ns])
        -> blocked_nd_range<Value, sizeof...(Ns)>;

        template <typename Value, unsigned int N>
        blocked_nd_range(const Value (&)[N], typename blocked_nd_range<Value, N>::size_type = 1)
        -> blocked_nd_range<Value, N>;

        template <typename Value, unsigned int N>
        blocked_nd_range(blocked_nd_range<Value, N>, split)
        -> blocked_nd_range<Value, N>;

        template <typename Value, unsigned int N>
        blocked_nd_range(blocked_nd_range<Value, N>, proportional_split)
        -> blocked_nd_range<Value, N>;
    } // namespace tbb
    } // namespace oneapi

Deduction Guides
----------------

Copy and move constructors of ``blocked_nd_range`` provide implicitly-generated deduction guides. 
In addition, the following explicit are provided:

.. code:: cpp

    template <typename Value, typename... Values>
    blocked_nd_range(blocked_range<Value>, blocked_range<Values>...)
    -> blocked_nd_range<Value, 1 + sizeof...(Values)>;

**Effects**: Allows deduction from a set of ``blocked_range`` objects provided to the constructor ``[1]`` of ``blocked_nd_range``.
**Constraints**: This deduction guide participates in overload resolution only if all of the types in `Values` are same as `Value`.
**Example**:

    .. code:: cpp

        oneapi::tbb::blocked_range<int> range1(0, 100);
        oneapi::tbb::blocked_range<int> range2(0, 200);

        // Deduced as blocked_nd_range<int, 2>
        oneapi::tbb::blocked_nd_range nd_range(range1, range2);

.. code:: cpp

    template <typename Value, unsigned int... Ns>
    blocked_nd_range(const Value (&...)[Ns])
    -> blocked_nd_range<Value, sizeof...(Ns)>;

**Effects**: Allows deduction from a set of ``blocked_range`` objects represented as a set of braced-init-lists while using
the constructor ``1`` of ``blocked_nd_range``.
**Constraints**: This deduction guide participates in overload resolution only if ``sizeof...(Ns) >= 2`` and each integer ``Ni`` in ``Ns``
is equal to either ``2`` or ``3`` representing the constructors of ``blocked_range`` with 2 and 3 arguments respectfully. 
**Example**

    .. code:: cpp
        // Deduced as blocked_nd_range<int, 2>
        oneapi::tbb::blocked_nd_range nd_range({0, 100}, {0, 200, 5});

.. note:: 
    This guide only allow deduction from braced-init-lists of objects of the same type. Setting the explicit grainsize for ranges of non-integral types
    is not supported by the deduction guides and requires explicit template arguments to be specified.

.. code:: cpp

    template <typename Value, unsigned int N>
    blocked_nd_range(const Value (&)[N], typename blocked_nd_range<Value, N>::size_type = 1)
    -> blocked_nd_range<Value, N>;

**Effects**: Allows deduction from a single C array object indicating a set of dimension endings using the constructor 
``2`` of ``blocked_nd_range``.
**Example**

    .. code:: cpp

        int endings[3] = {100, 200, 300};

        // Deduced as blocked_nd_range<int, 3>
        oneapi::tbb::blocked_nd_range nd_range(endings);

        // Deduced as blocked_nd_range<int, 3>
        oneapi::tbb::blocked_nd_range nd_range({100, 200, 300}, /*grainsize = */10);

.. code:: cpp

    template <typename Value, unsigned int N>
    blocked_nd_range(blocked_nd_range<Value, N>, split)
    -> blocked_nd_range<Value, N>;

**Effects**: Allows deduction while using the splitting constructor ``3`` of ``blocked_nd_range``.

.. code:: cpp

    template <typename Value, unsigned int N>
    blocked_nd_range(blocked_nd_range<Value, N>, proportional_split)
    -> blocked_nd_range<Value, N>;

**Effects**: Allows deduction while using the proportional splitting constructor ``4`` of ``blocked_nd_range``.