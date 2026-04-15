# ``tbb::optional`` and **Optional-Like** Named Requirement

## Table of Contents

* 1 [Motivation](#motivation)
  * 1.1 [The `tbb::flow_control` Problem](#the-tbbflow_control-problem)
  * 1.2 [Default Constructible Requirements on Flow Graph Node Inputs](#default-constructible-requirements-on-flow-graph-node-inputs)
  * 1.3 [Message Filtering for Flow Graph Nodes](#message-filtering-for-flow-graph-nodes)
* 2 [Concrete Proposal](#concrete-proposal)
  * 2.1 [`tbb::optional` API](#tbboptional-api)
    * 2.1.1 [Functionality of C++17 `std::optional` Excluded from `tbb::optional`](#functionality-of-c17-stdoptional-excluded-from-tbboptional)
    * 2.1.2 [C++17 Behavior](#c17-behavior)
  * 2.2 [**Optional-Like** Named Requirement](#optional-like-named-requirement)
  * 2.3 [`tbb::flow_control` deprecation](#tbbflow_control-deprecation)
* 3 [Possible Additional Applications to Consider](#possible-additional-applications-to-consider)
* 4 [Open Questions](#open-questions)

## Motivation

``std::optional`` is one of the most impactful Standard Library types introduced in modern C++. It represents a 
fundamental concept of a "value that might not be present" and is widely used to replace ad-hoc conventions like
sentinel values, using ``std::pair<T, bool>`` as a return type and others.

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

This API requires the user to construct a throw-away value when signaling stop. TBB runtime guarantees
to discard it if ``flow_control`` was stopped, but a valid output must still be provided, which is
unintuitive and error-prone.

In some cases, such an API implicitly requires the output type to be default constructible (although not
formally required) since a default constructed object is the intuitive choice for signaling end-of-input when
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

Using ``std::optional`` internally for cached/buffered items can eliminate these requirements, 
allowing non-default-constructible types as messages.

### Message Filtering for Flow Graph Nodes

We also have a long-standing idea to provide filtering support for Flow Graph functional
nodes (e.g., ``function_node``) to avoid broadcasting the message to successors if some conditions are satisfied.

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

An alternative could be returning ``std::optional<Output>`` from ``function_node``'s body.
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

Since ``std::optional`` is only available starting from C++17, and oneTBB supports C++11 as the minimum standard,
an alternative type should be provided for C++11/C++14.

The proposal is
* Extend the current union-based optional implementation used by resource-limited Flow Graph nodes to include
a core subset of ``std::optional`` operations. Make this implementation available as the public ``tbb::optional``.
* Introduce the special named requirement **Optional-Like** to allow using ``tbb::optional``, ``std::optional``
or a minimal user class in ``parallel_pipeline``, ``input_node`` and in Flow Graph message filters in the future.
* Deprecate ``tbb::flow_control``.
* Relax named requirements for types where possible.

### ``tbb::optional`` API

``tbb::optional`` API should implement a core subset of ``std::optional`` operations which are sufficient for C++11/C++14.

```cpp
// Defined in header <oneapi/tbb/optional.h>

namespace oneapi {
namespace tbb {

struct in_place_t;

template <typename T>
class optional {
public:
    using value_type = T;

    optional() noexcept;
    optional(const optional& other);
    optional(optional&& other) noexcept(std::is_nothrow_move_constructible<T>::value);
    
    template <typename... Args>
    optional(in_place_t, Args&&... args);

    ~optional();

    optional& operator=(const optional& other);
    optional& operator=(optional&& other)
        noexcept(std::is_nothrow_move_constructible<T>::value &&
                 std::is_nothrow_move_assignable<T>::value);

    const T* operator->() const noexcept;
    T* operator->() noexcept;

    const T& operator*() const noexcept;
    T& operator*() noexcept;

    explicit operator bool() const noexcept;
    bool has_value() const noexcept;

    const T& value() const;
    T& value();

    template <typename... Args>
    T& emplace(Args&&... args);

    void swap(optional& other)
        noexcept(std::is_nothrow_move_constructible<T>::value &&
                 is-nothrow-swappable<T>::value); // Equivalent to C++17 std::is_nothrow_swappable

    void reset() noexcept;
};

template <typename T, typename... Args>
optional<T> make_optional(Args&&... args);

template <typename T>
void swap(optional<T>& lhs, optional<T>& rhs) noexcept(noexcept(lhs.swap(rhs)));

} // namespace tbb
} // namespace oneapi
```

#### Functionality of C++17 ``std::optional`` Excluded from ``tbb::optional``

The following functionality is proposed to be excluded from the set of core operations:

```cpp
// Default-constructed optional can be used instead
struct nullopt_t;

// Inline variables are not available in C++11
inline constexpr nullopt_t nullopt;
inline constexpr in_place_t in_place;

template <typename T>
class optional {
    // Equivalent to default-constructed optional
    optional() noexcept;

    // In-place construction can be used instead
    template <typename U>
    optional(const optional<U>& other);

    template <typename U>
    optional(optional<U>&& other);

    template <typename U, typename... Args>
    optional(in_place_t, std::initializer_list<U>, Args&&... args);

    template <typename U = std::remove_cv_t<T>>
    optional(U&& value);

    // Equivalent to opt = tbb::optional{};, or reset
    optional& operator=(nullopt_t) noexcept;

    // Explicit support for lvalue and rvalue optionals
    // Easy-to-implement, can be included in the core functionality if needed
    T& value() &;
    const T& value() const &;

    T&& value() &&;
    const T&& value() const &&;

    // Can be easily rewritten using has_value
    template <typename U = std::remove_cv<T>>
    T value_or(U&& default_value) const &;

    template <typename U = std::remove_cv<T>>
    T value_or(U&& default_value) &&;

    template <typename U, typename... Args>
    T& emplace(std::initializer_list<U>, Args&&...);
};

// In-place make_optional can be used instead
template <typename T>
optional<std::decay<T>> make_optional(T&& value);

template <typename T, typename U, typename... Args>
optional<T> make_optional(std::initializer_list<U> il, Args&&... args);

// Accessing an empty optional can be documented as UB
struct bad_optional_access;

// Value comparisons can be used directly
template <typename T, typename U>
bool operator==(const optional<T>& lhs, const optional<U>& rhs);
// also operators !=, <, >, <=, >=

template <typename T>
bool operator==(const optional<T>& lhs, nullopt_t);

template <typename T>
bool operator==(nullopt_t, const optional<T>& rhs);
// also operators !=, <, >, <=, >=

template <typename T, typename U>
bool operator==(const optional<T>& lhs, const U& rhs);

template <typename T, typename U>
bool operator==(const U& lhs, const optional<U>& rhs);
// also operators !=, <, >, <=, >=
```

Additionally, it is proposed to drop some constraints and requirements used by ``std::optional``.
For example, ``std::optional<T>`` copy constructor is defined as deleted if ``T`` is not copy constructible.
For ``tbb::optional``, it is proposed to document this as undefined behavior.

Any of the functions and restrictions listed above can be easily included into the core
functionality of ``tbb::optional`` in the future upon request.

#### C++17 Behavior

There are three options for ``tbb::optional`` in C++17 mode (where ``__cpp_lib_optional`` is defined):
1. ``tbb::optional`` can be completely removed.
2. ``tbb::optional`` can be an alias to ``std::optional``.
3. ``tbb::optional`` can be kept as-is. 

The issue with option 1 is that the users who migrate from C++11/14 to C++17 would need to rewrite
the code to use ``std::optional``.

For option 2, such a migration does not require changing the code, but C++11/14 ``tbb::optional``
becomes binary incompatible with C++17 ``tbb::optional``.

Option 3 preserves both support for old code and binary compatibility for ``tbb::optional`` and therefore
is currently proposed. Additionally, implicit converting constructors and assignment operators from/to
``std::optional`` can be defined in ``tbb::optional``.

### **Optional-Like** Named Requirement

An **Optional-Like** named requirement should be defined in TBB to allow ``input_node`` body and ``parallel_pipeline``
first-filter body to use both ``tbb::optional`` and ``std::optional``.

The type ``T`` satisfies the requirements of **Optional-Like** for value type ``U`` when the following
operations are supported:

Let ``t`` be an object of ``T`` and ``ct`` be an object of ``const T``.

| Operation                   | Precondition        | Return Type  | Name                             |
|-----------------------------|---------------------|--------------|----------------------------------|
| ``bool(ct)``                | N/A                 | ``bool``     | Conversion to ``bool``           |
| ``*t``                      | ``bool(t) == true`` | ``U&``       | Access the value                 |
| ``*ct``                     | ``bool(ct) == true``| ``const U&`` | Access the value                 |

These named requirements are satisfied by ``tbb::optional``, ``std::optional``, ``boost::optional`` or
any user-defined lightweight optional.

### ``tbb::flow_control`` deprecation

As a first step for moving ``input_node`` and ``parallel_pipeline`` from ``flow_control``-body to a body
that uses optional, we can mark ``flow_control`` class as ``[[deprecated]]`` and temporarily support both forms.

In the implementation, we could do something like:

```cpp
class [[deprecated]] flow_control { ... };

// Part of input_node<Output> implementation

// Needed to avoid deprecation warning while creating flow_control
// Cross-platform warning suppression would be used
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

template <typename Body, typename = void>
struct body_uses_flow_control : std::false_type {}; // body uses optional

template <typename Body>
struct body_uses_flow_control<Body,
    tbb::detail::void_t<decltype(tbb::detail::invoke(std::declval<Body>(),
                                                     std::declval<flow_control&>()))>>
    : std::true_type {};

template <typename Body>
void select_body(Body body, /*uses_flow_control = */std::true_type) {
    tbb::flow_control fc;
    Output output = body(fc);

    while (!fc.is_pipeline_stopped) {
        successors().try_put(output);
        output = body(fc);
    }
}

#pragma GCC diagnostic pop

template <typename Body>
void select_body(Body body, /*uses_flow_control = */std::false_type) {
    auto output_opt = body();
    while (output_opt) {
        successors().try_put(*output_opt);
        output_opt = body();
    }
}

template <typename Body>
void generate_until_end_of_input(Body body) { // exposition-only
    select_body(body, body_uses_flow_control<Body>{});
}
```

Only the user code where a body taking ``flow_control`` is defined will generate a deprecation warning.
Library internal usage of ``flow_control`` suppresses the warning.

## Possible Additional Applications to Consider

Some other TBB APIs can provide optional-aware alternatives. For example, ``try_pop(T&)`` in
``concurrent_queue`` and ``concurrent_priority_queue`` could return ``tbb::optional``.
Also basic Flow Graph ``sender`` class functions ``try_get(T&)`` and ``try_reserve(T&)`` could
return ``tbb::optional`` (results in ABI break since the functions are virtual).

## Open Questions

1. How should ``tbb::optional`` and ``std::optional`` coexist in C++17?
See [C++17 Behavior](#c17-behavior) section for more detail.
2. Which functions from ``std::optional`` should be included in ``tbb::optional`` implementation.
See [``tbb::optional`` API](#tbboptional-api)
and [Functionality of C++17 ``std::optional`` Excluded from ``tbb::optional``](#functionality-of-c17-stdoptionalexcluded-from-tbboptional)
sections for more detail.
3. Can some requirements from ``std::optional`` be relaxed in ``tbb::optional``?
See [Functionality of C++17 ``std::optional`` Excluded from ``tbb::optional``](#functionality-of-c17-stdoptional-excluded-from-tbboptional)
section for more detail.
4. Should ``tbb::optional::value`` throw exception for empty optional or be assert in debug, UB in release?
