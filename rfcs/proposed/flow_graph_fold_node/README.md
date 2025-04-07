# Folding node in the Flow Graph

## Introduction

Current oneTBB Flow Graph API provides basic functional nodes for expressing basic user-scenarios:

* `continue_node` that converts multiple input signals (usually from several different predecessors) to the single signal to each of successors.
* `function_node` converting each input message from one or several predecessors to the single output message.
* `multifunction_node` converting each input message into some amount of output messages (zero messages are also allowed) to one or several successors.

But expressing the use-case converting multiple input messages (input stream) into single output is not straightforward to express using only the existing API.

Such an API is extremely useful for expressing _reductions_ (or _folds_) using the oneTBB Flow Graph which is transformation of a sequence of
objects (_fold input stream_) into a single object (_fold result_) starting from the user-defined initial value (_fold initializer_) by repeating the user defined binary operation (_fold operation_).
Consider the basic example:

*_S = sum(1, n) = 0 + 1 + 2 + 3 + 4 + ... + n_*

_fold input stream_ is a sequence _(1, 2, 3, 4, ..., n)_, _fold result_ is _S_, _fold initializer_ is _0_ and the _fold operation_ is binary _+_.

This paper proposes to add a new functional node to the oneTBB Flow Graph that would allow the user to specify _fold initializer_ and _fold operation_
and would compute _fold_ on multiple _fold input stream_s in parallel (both several operations on a single stream and different streams can be
calculated in parallel).

## Proposal

The new ``oneapi::tbb::flow::fold_node<InputType, OutputType>`` is a new oneTBB Flow Graph functional node that allows to calculate parallel _fold_
on multiple input streams. The concurrency limit (similar to other functional nodes), the _fold initializer_ and a binary _fold operation_
should be specified while constructing the node:

```cpp
tbb::flow::graph g;

tbb::flow::fold_node<int, int> f_node(g, tbb::flow::unlimited,
    /*fold initializer = */0,
    /*fold operation = */std::plus<int>{});
```

Since the ``fold_node`` needs to operate multiple _fold input stream_s in parallel, some identifier of the stream should be provided
as an input message together with the actual data to compute. It is proposed to use ``tbb::flow::tagged_msg`` class for that purpose
as an input for the ``fold_node`` that would be sent by the predecessor or by manual ``try_put`` calls:

```cpp
tbb::flow::graph g;

tbb::flow::fold_node<int, int> f_node(g, tbb::flow::unlimited,
    /*fold initializer = */0,
    /*fold operation = */std::plus<int>{});

tbb::flow::buffer_node<int> buf(g);

tbb::flow::make_edge(f_node, buf);

std::size_t stream1_index = 1, stream2_index = 2;

std::vector<int> stream1 = {1, 3, 5, 7, 9};
std::vector<int> stream2 = {2, 4, 6, 8, 10};

using fold_input = typename tbb::flow::fold_node<int, int>::input_type;

// Submit stream1
for (auto item : stream1) {
    f_node.try_put(input_type{stream1_index, item});
}

for (auto item : stream2) {
    f_node.try_put(input_type{stream2_index, item});
}

int result = 0;
buf.try_get(result);

// result is expected to be either 25 (result of the first stream fold) or
// 30 (result of the second stream fold)
```

Explicit specification the ``tagged_msg`` as an ``Input`` template parameter as well as allowing ``tagged_msg`` as a fold operation argument
are [open question](#process-specific-information).

An other important aspect is since the ``fold_node`` takes the elements from the _fold input stream_ one-by-one, not the entire stream and
computes partial folds for each element, it is unclear for the node when the computed result is final and all of the elements in the stream
were processed. To handle this, it is required to take a special signal from the user indicating that no more elements from the input stream
with the specified tag are expected. Some special type defined by the Flow Graph is required for that purpose, e.g. ``tbb::flow::fold_stream_end``.
``fold_stream_end`` instance should be explicitly submitted to the node once the input stream end was reached:

```cpp
// Submit stream1
for (auto item : stream1) {
    f_node.try_put(input_type{stream1_index, item});
}
f_node.try_put(tbb::flow::fold_stream_end{stream1_index});

for (auto item : stream2) {
    f_node.try_put(input_type{stream2_index, item});
}
f_node.try_put(tbb::flow::fold_stream_end{stream2_index});

int result = 0;
buf.try_get(result);

// result is expected to be either 25 (result of the first stream fold) or
// 30 (result of the second stream fold)
```

From the implementation perspective, there can be multiple approaches for implementing the ``fold_stream_end`` that affects the user API.
Some of them were considered in a [separate section](#fold_stream_end-implementation-approaches).

## ``fold_stream_end`` Implementation Approaches

Consider the possible implementation of ``fold_node`` described in this paper as a wrapper of ``tbb::flow::multifunction_node`` combined with
concurrent hash table for storing the partial results. Let's also consider the ``multifunction_node`` to be ``tbb::flow::unlimited`` for simplicity.
Since the elements from the input stream and the ``fold_stream_end`` indicator are submitted in the same manner, the current TBB implementation
of ``multifunction_node`` would create and spawn _N + 1_ tasks, where _N_ is a number of elements in the input stream. Since the tasks are processed by
the TBB scheduler in an unspecified order, it is possible that the task processing the ``fold_stream_end`` can be processed before other tasks processing
the input stream.

### ``fold_stream_end`` containing the number of elements in the stream

The first implementation approach suggests ``fold_stream_end`` to contain the number of elements in the corresponding input stream.
The ``fold_node`` should support an internal *signed* integer counter of elements for each input stream. Once the node is
processing the element that is not a ``fold_stream_end``, the counter should be increased by 1 for each element. If the node is
processing the ``fold_stream_end`` element, the counter should be decreased by _N_, where _N_ is a number of elements in the stream
which is stored as part of the ``fold_stream_end``.

In this case, having the internal counter not equal to _0_ indicates that there are still elements to process and once it is equal to _0_, the result
of the fold is considered full and can be propagated to the ``fold_node`` successors.

### Automated approach

The second implementation approach suggests ``fold_stream_end`` to be a simple flag type and not to spawn a tasks while receiving this flag.
The ``fold_node`` in that case should support a counter indicating the number of tasks spawned for each input stream and another flag indicating that
``fold_stream_end`` flag was received. Each executed task should decrease the reference counting after the completion and check if the end flag was received.
If the node is processing the ``fold_stream_end``, it stores the end flag to ``true`` and check if all of the tasks were processed.
The result would be considered complete and propagated to successors if both completion flag was set to ``true`` and all of the tasks were processed, i.e.
the corresponding reference counter equals to _0_.

## Process Specific Information

### Propagating the stream index to the successors

It may be useful for the successors of the ``fold_node`` to identify the input stream, the result for which was received:

```cpp
tbb::flow::graph g;

tbb::flow::fold_node<int, int> fold(g, ...);
tbb::flow::function_node<int, int> postprocess(g, tbb::flow::unlimited,
    [](const tbb::flow::tagged_msg<std::size_t, int>& tagged_input) {
        // tagged_input.tag() - a unique tag of the input stream
        // cast_to<int>(tagged_input) - the fold result
    });
```

One of the solutions is to provide flexibility:

* If the ``OutputType`` template argument of the ``fold_node`` is specified as a specialization of ``tagged_msg`` - the tag should be propagated to the
successors. In this case the successors should be ready to take the same ``tagged_msg`` (be a ``receiver<tagged_msg<...>>``).
* If the ``OutputType`` template argument of the ``fold_node`` is not a specialization of ``tagged_msg`` - the tag should *not* be propagated and the
successors are not expected to be receivers of ``tagged_msg``.

### Propagation of the stream index to the node body

Same as in the previous question, there may be useful for some use-cases to propagate the index of the input stream to the fold operation to
implement the specific logic for different stream tags:

```cpp
tbb::flow::graph g;

using fold_node_type = fold_node<int, int>;
using input_type = typename fold_node_type::input_type;

fold_node_type fold(g, tbb::flow::unlimited,
    /*fold initializer = */0,
    [](int lhs, const input_type& rhs) {
        if (rhs.tag() == 0) {
            // Fold logic for tag 0
            return lhs + cast_to<int>(rhs);
        } else {
            // Fold logic for any other streams
            return (lhs / 2) + (cast_to<int>(rhs) / 2);
        }
    });
```

The same flexible approach can be applied for solving this as well:
* If the invocation of _fold operation_ with an object of type

It is also an open question, should ``input_type`` be allowed as a first, second, or both operands of _fold operation_.

Similar to the previous open question, the flexible approach can be applied to allow both use-cases:
* If the invocation of the _fold operation_ associated with the ``fold_node`` with the argument of type ``input_type`` (i.e. ``tagged_msg``)
is well-formed - propagate the index of the stream to the body.
* Otherwise - the index of the input stream is not propagated to the _fold operation_.

### Should ``InputType`` and ``OutputType`` be different?

Basic use-case of computing a fold is reducing a set of single type objects to a single object of the same type, but in general
there can be folds where _fold operation_ returns different type. To support such use-cases it should be allowed to specify different
``InputType`` and ``OutputType`` template arguments.

But in that case the requirements for _fold operation_ should be clearly defined since the partial results would be of the type ``OutputType``
so ``fold_operation(OutputType, InputType)`` should be defined. Should other variations be defined as well is an open question.

### Computation of the single fold

This question is not related to the ``fold_node`` design, only to it's possible implementation on top of
``multifunction_node<InputType, std::tuple<OutputType>>`` and ``concurrent_hash_map<std::size_t, partial-result-holder>`` for
containing the partial results of the fold.

In that case, each task would need to either calculate the initial fold ``fold_operation(fold_initializer, input)`` if there are no partial
result yet and put it to the hash map.

The first thread calculating the fold would insert the _empty_ element to the partial results hash_map and keep holding the ``accessor``
instance (with the underlying mutex) and while computing the initial fold ``fold_operation(fold_initializer, input)``.
If the single fold is not a lightweight operation, it may negatively affect performance because of holding the mutex inside of the accessor.

An other option is to check for the partial result presence and if there is no element - calculate the ``result = fold_operation(fold_initializer, input)``
without holding an ``accessor`` and try to update the partial result. If an other thread inserts simultaneously an other partial result
with the same index, the current thread needs to take that result into account and calculate ``fold_operation(prev_result, result)`` one more time.
Ideally without holding the lock. Further partial results may also fail due to other threads putting the results first.

On the one hand, holding the accessor while calculating the _fold operation_ can negatively affect the performance and on the other hand,
trying to update the result each time after the calculation and failing can also have negative effect because of recalculation on each failure.

An other approach is to use ``std::atomic`` as a partial result inside of the hash map but it will significantly narrow the amount of types that can
be used as ``InputType`` and ``OutputType`` arguments of the ``fold_node``.
