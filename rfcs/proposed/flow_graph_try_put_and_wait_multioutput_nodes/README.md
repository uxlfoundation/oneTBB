# Flow Graph ``try_put_and_wait`` support for multi-output nodes

## Introduction

The Flow Graph ``try_put_and_wait`` feature is intended to support waiting for the single messages to complete execution
on each node in the Flow Graph.

It is implemented by assigning the special internal object ``message_metainfo`` with each input of ``try_put_and_wait``. This object
is broadcasted through the Flow Graph together with the input message itself. The type and the value of the message can change
but the assigned metainfo points to the same reference counter that was assigned while calling the ``try_put_and_wait``.

Current implementation supports all graph nodes except the ``multifunction_node`` and the ``async_node``. The issue with these nodes
is that the message is not broadcasted to the successors automatically when the body execution finishes. It is a user responsibility
to push the output to the corresponding successor by calling the ``try_put`` method on one of the ``output_port``s:

```cpp
using namespace tbb::flow;

graph g;

function_node<int, int> fn(g, unlimited,
    [](int input) {
        // a copy of output is pushed to all of the successors of fn automatically
        return output;
    });

multifunction_node<int, std::tuple<int, int>> mfn(g, unlimited,
    [](int input, auto& ports) {
        // it is a user responsibility to push the output to all of the successors
        // that should receive the output
        std::get<0>(ports).try_put(output);
        std::get<1>(ports).try_put(output);
    })

async_node<int, int> asn(g, unlimited,
    [](int input, auto& gateway) {
        // also a user responsibility to push the output to the gateway for
        // further processing
        gateway.try_put(output);
    })
```

Since the ``message_metainfo`` is automatically passed together with the output itself, it is impossible to implicitly pass it in for
``multifunction_node`` and ``async_node``. Some API extensions are allowed to support this.

All of the examples in this paper are shown for the ``multifunction_node`` but all of the issues and extensions affects the
``async_node`` as well.

## Motivating examples

This section describes the potential usage scenarios for multi-output nodes that would be considered while designing the API extensions
for ``try_put_and_wait``.

### One or more outputs for each input message

```cpp
using mf_node_type = tbb::flow::multifunction_node<int, std::tuple<int, int>>;
using ports_type = typename mf_node_type::output_ports_type;

graph g;

mf_node_type mf_node(g, unlimited,
    [](int input, ports_type& ports) {
        for (size_t i = 0; i < n_outputs; ++i) {
            std::get<0>(ports).try_put(outputs[i]);
            std::get<1>(ports).try_put(outputs[i]);
        }
    });
```

### No outputs for an input

```cpp
using mf_node_type = tbb::flow::multifunction_node<int, std::tuple<int, int>>;
using ports_type = typename mf_node_type::output_ports_type;

graph g;

mf_node_type mf_node(g, unlimited,
    [](int input, ports_type&) {
        // No actions required in the basic case since no output is provided
    })
```

### Multiple inputs to one output (reduction)

```cpp
using sum_node_type = tbb::flow::multifunction_node<int, std::tuple<int>>;
using ports_type = typename sum_node_type::output_ports_type;

graph g;

const std::size_t num_expected_inputs = 100;

std::atomic<int> reduction_result = 0;
std::atomic<size_t> num_inputs = 0;

sum_node_type sum_node(g, unlimited,
    [&](int input, ports_type& ports) {
        if (++num_inputs != num_expected_inputs) {
            // Doing the reduction
            reduction_result += input;
        } else {
            // Enough inputs collected - submit single output
            std::get<0>(ports).try_put(reduction_result);
            reduction_result = 0;
            num_inputs = 0;
        }
    });
```

### No outputs for multiple inputs (cancelled reduction)

```cpp
using sum_node_type = tbb::flow::multifunction_node<int, std::tuple<int>>;
using ports_type = typename sum_node_type::output_ports_type;

graph g;

const std::size_t num_expected_inputs = 100;
// Reserved tag
const int cancelled_reduction_tag = std::numeric_limits<int>::max();

std::atomic<int> reduction_result = 0;
std::atomic<size_t> num_inputs = 0;

sum_node_type sum_node(g, unlimited,
    [&](int input, ports_type& ports) {
        if (input == cancelled_reduction_tag) {
            // Cancelling the reduction
            reduction_result = cancelled_reduction_tag;
            num_inputs = 0;
        } else {
            if (reduction_result != cancelled_reduction_tag) {
                // Reduction was not cancelled by other body calls
                if (++num_inputs != num_expected_inputs) {
                    // Doing the reduction
                    reduction_result += input;
                } else {
                    // Enough inputs collected - submit single output
                    std::get<0>(ports).try_put(reduction_result);
                    reduction_result = 0;
                    num_inputs = 0;
                }
            }
        }
    });
```

## Design approaches

### Automatic metainfo broadcast using the `output_ports_type` object

The first idea is try to hide the ``message_metainfo`` inside of the elements ``output_ports_type`` tuple. Implementation-wise, it would require changing the
``output_ports_type`` to a proxy type wrapping the "real" output ports and the stored metainfo. But from the spec and API perspective, ``output_ports_type`` is
required to be ``std::tuple`` of output_ports. This forces not changing the ``output_ports_type`` to be a proxy to tuple, but changing each element in the
tuple to be a proxy to the output port.

```cpp
// Exposition-only, does not describes the real class layout and function names
template <typename Input, typename OutputTuple>
class multifunction_node {
    class output_port;
    NodeBody                my_body;
    std::tuple<output_port> my_real_output_ports;

    class output_port_proxy {
        output_port&     my_port_ref;
        message_metainfo my_metainfo;

        bool try_put(const OutputType& output) {
            // Refers to the try_put_task method of the output_port
            // that can accept the metainfo
            return output_port.try_put_task(output, my_metainfo);
        }
    };
public:
    using output_ports_type = std::tuple<output_port_proxy>;

private:
    // metainfo is passed from the predecessor
    void try_put_task(const Input& input, const message_metainfo& metainfo) {
        // Each element of the proxy tuple refers to the corresponding element of my_real_output_ports
        // and contains a copy of metainfo
        output_port_proxy port_proxies{my_real_output_ports, metainfo};

        my_body(input, port_proxies);
    }
};
```

On the user-side, the code remains unchanged:

```cpp
using multifunction_node_type = multifunction_node<int, std::tuple<int, int>>;
using output_ports_type = typename multifunction_node_type::output_ports_type;

graph g;
multifunction_node_type node(g, unlimited,
    [](int input, output_ports_type& ports) {
        // ports is a tuple of proxies
        // real output_ports are not visible to the user
        std::get<0>(ports).try_put(input);
    });
```

That is the main advantage of this approach - the user does not need to modify the code. The first downside is that the reduction use-cases
cannot be covered since the ports proxy only work with the currently available metainfo. In the reduction case, the submission of the output
can be done as part of the body call with the input element without the assigned metainfo. But one of the elements that were reduced can contain
a metainfo, but it would be lost one the corresponding call to the body would be completed.

Other downside of this approach is having the ``output_ports()`` method in the ``multifunction_node`` specification:

```cpp
template <typename Input, typename OutputTuple>
class multifunction_node {
    output_ports_type output_ports();
};
```

It is specified that the ``output_ports()`` returns exactly the ``output_ports_type`` object, the object of the same type is used as a second
argument for the node body. If ``output_ports_type`` would be changed to be a tuple a proxies, the return type of this function will be changed
automatically. Since this method returns ``output_ports_type`` tuple by value, it should be stored as part of the layout of the node.

It results in having two tuples in the layout of the ``multifunction_node`` - one tuple of real output ports and another tuple of proxies to the
real output ports, required only to fulfill the requirements of ``output_ports()`` member function:

```cpp
template <typename Input, typename OutputTuple>
class multifunction_node {
    class output_port;
    class output_port_proxy {
        output_port&     my_port_ref;
        message_metainfo my_metainfo;
    };

    NodeBody                my_body;
    std::tuple<output_port> my_real_output_ports;

    // Each elements refers to the element in my_real_output_ports
    // and contains an empty metainfo
    std::tuple<output_port_proxy> my_output_ports_proxy;
public:
    using output_ports_type = std::tuple<output_port_proxy>;

    output_ports_type output_ports() {
        return my_output_ports_proxy;
    }
};
```

Also, the following usage of the ``multifunction_node`` is not prohibited by the spec (even it is not really practical):

```cpp
using multifunction_node_type = multifunction_node<int, std::tuple<int>>;

multifunction_node_type* node_ptr = nullptr;

graph g;
multifunction_node node(g, unlimited,
    [](int input, auto&) {
        // Ports argument is unused, output_ports is used instead
        std::get<0>(node_ptr->output_ports()).try_put(input);
    });

node_ptr = &node;
```

Since only the ``output_ports_type`` provided as an argument is supposed to contain a real metainfo, it would not be broadcasted to the successors
of the node.

### Providing a metainfo as an additional argument to the node body

The second possible approach is to define a new member type in the multi-output nodes that will wrap a metainfo and extend the body of the
node with the third optional argument of this type:

```cpp
using node_type = multifunction_node<int, std::tuple<int>>;
using ports_type = typename node_type::output_ports_type;
using hint_type = typename node_type::hint_type;

graph g;
node_type node(g, unlimited,
    [](int input, ports_type& ports, hint_type&& hint) {
        std::get<0>(ports).try_put(input, hint);
        std::get<1>(ports).try_put(input, std::move(hint))
    });
```

It becomes the responsibility of the user to provide the ``hint`` argument to the ``try_put`` calls to which the metainfo should be provided.

We will still be needed to support the user body with just two parameters for the compatibility with the existing usages. If such a body is provided,
the metainformation received from the node's successors would be ignored and never broadcasted to any successor of the node.

The ``hint_type`` can be defined as follows:

```cpp
class node_hint_type {
    message_metainfo my_metainfo;

    node_hint_type(const message_metainfo& metainfo)
        : my_metainfo(metainfo) {}
public:
    node_hint_type() = default;

    // Should be movable
    node_hint_type(node_hint_type&& other);
    node_hint_type& operator=(node_hint_type&& other);

    // Can be copyable (see details below)
    node_hint_type(const node_hint_type& other);
    node_hint_type& operator=(const node_hint_type& other);

    ~node_hint_type();

    // APIs needed to support the reduction use-cases (see details below)
    void reset();
    void merge(const node_hint_type& other);
};
```

Once the ``hint_type`` object is created using the private constructor with the ``metainfo`` argument, it reserves an extra reference counter
to each ``wait_context`` associated with it. The 

## Proposal

## Open Questions in Design
