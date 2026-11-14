# Optional Semantics for oneTBB

## Table of Contents

* 1 [Motivation](#motivation)
  * 1.1 [The `tbb::flow_control` Problem](#the-tbbflow_control-problem)
  * 1.2 [Default Constructible Requirements on Flow Graph Node Inputs](#default-constructible-requirements-on-flow-graph-node-inputs)
  * 1.3 [Message Filtering for Flow Graph Nodes](#message-filtering-for-flow-graph-nodes)
* 2 [Concrete Proposal](#concrete-proposal)
  * 2.1 [**Optional-Like** Named Requirement](#optional-like-named-requirement)
  * 2.2 [Optional-Like API in oneTBB](#optional-like-api-in-onetbb)
  * 2.3 [`tbb::flow_control` Deprecation](#tbbflow_control-deprecation)
* 3 [Possible Additional Applications to Consider](#possible-additional-applications-to-consider)

## Motivation

``std::optional`` is one of the most impactful Standard Library types introduced in modern C++. It represents a 
fundamental concept of a "value that might not be present" and is widely used to replace ad-hoc conventions such as
sentinel values, ``std::pair<T, bool>`` return types, and similar patterns.

oneTBB supports C++11 as a minimum C++ Standard version, which means ``std::optional`` is not directly available
across the entire support matrix. This forces the library to implement internal workarounds where optional-like
semantics are required.

TBB already provides a custom union-based optional internally for the resource-limiting protocol used by
``tbb::flow::resource_limited_node`` and ``tbb::flow::resource_limiter``.

### The ``tbb::flow_control`` Problem

Currently, ``tbb::flow::input_node`` and ``tbb::parallel_pipeline`` first-filter bodies use ``tbb::flow_control``
as a signaling mechanism to indicate end-of-input:

```cpp
// body of tbb::flow::input_node<int>
int operator()(tbb::flow_control& fc) {
    if (end-of-input) {
        fc.stop();
        return 0; // dummy value
    }
    return next_input;
}
```

This API requires the user to construct a throwaway value when signaling stop. The TBB runtime guarantees
that it discards the value if ``flow_control`` is stopped, but a valid output must still be provided, which is
unintuitive and error-prone.

In some cases, such an API implicitly requires the output type to be default constructible,
although this is not a formal requirement. A default-constructed object is the intuitive choice for signaling end-of-input when
no sentinel is available. Non-default-constructible output types require contrived workarounds.

With ``std::optional``, this same body becomes self-documenting:

```cpp
std::optional<int> operator()() {
    if (end-of-input) {
        return {};
    }
    return next_input;
}
```

### Default Constructible Requirements on Flow Graph Node Inputs

Several Flow Graph nodes impose ``DefaultConstructible`` requirements on inputs or outputs even though neither
the user's body nor the node itself logically needs default-constructed objects:

* ``function_node`` with ``rejecting`` policy that uses default-constructed ``input_type``
as a placeholder before getting items from predecessors.
* ``multifunction_node`` with ``rejecting`` policy (same pattern as for ``function_node``).
* ``async_node`` which inherits the limitation from ``multifunction_node``.
* ``input_node`` initially default-constructs the cached item.
* ``overwrite_node`` initially default-constructs the buffered item.
* ``write_once_node`` which inherits the limitation from ``overwrite_node``.
* ``join_node`` initially default-constructs the output tuple, then fills it element-by-element.
* ``limiter_node`` that default-constructs the value before trying to reserve the item.

Using optional internally for cached/buffered items can eliminate these requirements, 
allowing non-default-constructible types as messages.

### Message Filtering for Flow Graph Nodes

We also have a long-standing idea to provide filtering support for Flow Graph functional
nodes (e.g., ``function_node``) to avoid broadcasting the message to successors if certain conditions are met.

Currently, the only way to implement this is to use a ``multifunction_node`` and conditionally put items to an output port:

```cpp
multifunction_node<int, std::tuple<int>>
    filter(g, unlimited, [](int input, auto& output_ports) {
        int output = get_output(input);
        if (should-broadcast) {
            std::get<0>(output_ports).try_put(output);
        }
    });
```

An alternative could be to return ``std::optional<Output>`` from the ``function_node`` body.
If the optional object is empty, the item is not broadcast:

```cpp
function_node<int, int>
    filter(g, unlimited, [](int input) -> std::optional<int> {
        int output = get_output(input);
        if (should-broadcast) {
            return {output};
        } else {
            return {};
        }
    });
```

## Concrete Proposal

The step-by-step proposal is:
* Introduce the special named requirement **Optional-Like** to allow using objects implementing optional
semantics in ``parallel_pipeline``, ``input_node``, and Flow Graph message filters in the future.
This allows using ``std::optional``, ``boost::optional``, or any other minimal user-defined
class as a return type for a filter or node body.
* Provide the current union-based optional implementation used by resource-limited Flow Graph nodes
as a public API. Since it does not implement the full set of ``std::optional`` operations, it can be named
without using the word "optional" to highlight that the class is not a direct C++11 replacement for ``std::optional``.
A possible name is ``tbb::possible_value<T>``.
* Relax named requirements for Flow Graph types where possible.
* Deprecate and remove ``tbb::flow_control``.

### **Optional-Like** Named Requirement

An **Optional-Like** named requirement should be defined in TBB to allow ``input_node`` body and ``parallel_pipeline``
first-filter body to use types implementing the optional semantics instead of ``flow_control``.

The type ``T`` satisfies the requirements of **Optional-Like** for value type ``U`` when the following
operations are supported:

Let ``t`` be an lvalue of type ``T`` and ``ct`` be an lvalue of type ``const T``.

| Operation                   | Precondition        | Return Type  | Name                             |
|-----------------------------|---------------------|--------------|----------------------------------|
| ``bool(ct)``                | N/A                 | ``bool``     | Conversion to ``bool``           |
| ``*t``                      | ``bool(t) == true`` | ``U&``       | Access the value                 |
| ``*ct``                     | ``bool(ct) == true``| ``const U&`` | Access the value                 |

These named requirements are satisfied by ``std::optional``, ``boost::optional``, or any user-defined lightweight optional type.

As a first step, it is proposed to extend ``tbb::parallel_pipeline`` and ``tbb::flow::input_node``
semantics to allow both bodies that use ``tbb::flow_control`` and bodies
that return any **Optional-Like** object:

```cpp
int main() {

    auto flow_control_body = [](tbb::flow_control& fc) -> int {};
    auto std_optional_body = []() -> std::optional<int> {};

    tbb::flow::graph g;
    tbb::flow::input_node<int> ctl_input(g, flow_control_body); // OK, current API
    tbb::flow::input_node<int> opt_input(g, std_optional_body); // Also OK, proposed API
}
```

### Optional-Like API in oneTBB

oneTBB can provide a minimalistic class satisfying the **Optional-Like** requirements. This API is targeted
for users with C++11 or C++14 who do not want to take a dependency on Boost, or implement their own
minimal optional API.

The current ``resource_handle_optional`` API from Flow Graph internals can be used (already fully implemented and tested).

To highlight that this API is not a direct implementation of ``std::optional`` for C++11/14, it is proposed to avoid
using "optional" in the name of the API. The currently proposed name is ``tbb::possible_value``.

```cpp
// Defined in header <oneapi/tbb/flow_graph.h>
// Defined in header <oneapi/tbb/parallel_pipeline.h>

namespace oneapi {
namespace tbb {

template <typename T>
class possible_value {
public:
    using value_type = T;

    struct in_place_t {};

    possible_value() noexcept;
    possible_value(const possible_value& other);
    possible_value(possible_value&& other) noexcept(std::is_nothrow_move_constructible<T>::value);

    template <typename... Args>
    possible_value(in_place_t, Args&&... args);

    ~possible_value();

    possible_value& operator=(const possible_value& other);
    possible_value& operator=(possible_value&& other)
        noexcept(std::is_nothrow_move_constructible<T>::value &&
                 std::is_nothrow_move_assignable<T>::value);

    const T& operator*() const noexcept;
    T& operator*() noexcept;

    explicit operator bool() const noexcept;

}; // class possible_value

} // namespace tbb
} // namespace oneapi
```

An earlier version of this document proposed implementing core functionality of ``std::optional`` as a
``tbb::optional`` API. Downsides of this approach are:
* A requirement to provide a broad set of functions as part of ``tbb::optional`` that should also be
tested and documented.
* Semantic difference between ``tbb::optional`` and ``std::optional`` should be either completely
absent or carefully reviewed.
* Coexistence policy between ``tbb::optional`` and ``std::optional`` for C++17 should be defined.

Having a minimal API, most parts of which are already implemented and tested for Flow Graph
resolves the first issue.

Issues 2 and 3 are resolved by having ``tbb::possible_value`` as a completely separate API that does
not imply any relationship with ``std::optional``. The semantics of its member functions can be defined
as needed and the coexistence of two separate APIs is not an issue.

### ``tbb::flow_control`` Deprecation

After having both the **Optional-Like** named requirement and the ``tbb::possible_value`` class, the ``flow_control``
class can be deprecated and removed in the future.

Currently, it is used for
* ``flow::input_node`` body
* ``parallel_pipeline`` first-filter body
* ``parallel_pipeline`` single-filter body

``input_node`` and first-filter body usages are completely replaced by **Optional-Like** and ``possible_value``.

A single-filter body requires a different approach since the body signature does not expect anything to be returned:

```cpp
auto single_filter_body = [](tbb::flow_control& fc) {
    if (should-stop) fc.stop();
    // Do work
};
```

Since a single-filter body does not have an output, there is nothing to be wrapped into **Optional-Like**.

One of the options considered was to use optional over a special flag type, for example ``tbb::flow::continue_msg``:

```cpp
auto single_filter_body = []() -> std::optional<tbb::flow::continue_msg> {
    if (should-stop) return {};
    // Do work
    return {tbb::flow::continue_msg};
};
```

The issue with this approach is a semantic difference from actual usages of ``tbb::flow::continue_msg`` in the Flow Graph.

Another option is to simply use a boolean flag to indicate whether the pipeline should be continued (or stopped):

```cpp
auto single_filter_body = []() {
    if (should-stop) return false;
    // Do work
    return true;
};
```

The current document proposes using a boolean flag: ``true`` if the pipeline should be continued and ``false`` if it should be
stopped.

## Possible Additional Applications to Consider

Some other TBB APIs can provide optional-aware alternatives. For example, ``try_pop(T&)`` in
``concurrent_queue`` and ``concurrent_priority_queue`` could return ``tbb::possible_value``.
Also basic Flow Graph ``sender`` class functions ``try_get(T&)`` and ``try_reserve(T&)`` could
return ``tbb::possible_value`` (which results in an ABI break since the functions are virtual).
