# Waiting for single messages in the Flow Graph

Extending the oneTBB Flow Graph interface with the new ``try_put_and_wait(msg)`` API which allows to wait
for completion of the chain of tasks corresponding to the ``msg``.

The feature should improve the Flow Graph performance in scenarios where multiple threads submits the work into the
Flow Graph simultaneously and each of them should

## Introduction

Current API of oneTBB Flow Graph allows doing two basic actions after building the graph:

- Submitting messages in some nodes using the ``node.try_put(msg)`` API.
- Waiting for completion of **all** messages in ghe graph using the ``graph.wait_for_all()`` API.

Since the only API currently available for waiting until the work would be completed waits for all tasks in the graph
to complete, there can be negative performance impact in use cases when the thread submitting the work should be notified as soon as possible
when the work is done. Having only ``graph.wait_for_all()`` forces the thread to wait until **all** of the tasks in the Flow Graph, no meter
corresponds to the waited message or not.

Consider the following example:

```cpp

struct ComputeInput;
struct ComputeOutput;

// High-level computation instance that performs some work
// using the oneTBB Flow Graph under the hood
// but the Flow Graph details are not expressed as part of public API
class ComputeTool
{
private:
    oneapi::tbb::flow::graph m_graph;
    oneapi::tbb::flow::broadcast_node<ComputeInput> m_start_node;
    // Other Flow Graph nodes
public:
    ComputeTool()
    {
        // Builds the Flow Graph
    }

    // Performs the computation using the user-provided input
    ComputeOutput compute(ComputeInput input)
    {
        m_start_node.try_put(input); // Submit work in the graph
        m_graph.wait_for_all(); // Waiting for input to be processed
    }
};

int main()
{
    ComputeTool shared_tool;

    oneapi::tbb::parallel_for(0, 10,
        [&](int i)
        {
            // Preparing the input for index i

            ComputeOutput output = shared_tool.compute(input);

            // Post process output for index i
        });
}

```

The ``ComputeTool`` is a user interface that performs some computations on top of oneTBB Flow Graph. The function ``compute(input)`` should
perform the computations for the provided input. Since the Flow Graph is used, it should submit the message into the graph and wait for its
completion. This function also can be called concurrently from several threads as it shown in the ``main``.

Since the only available API in the Flow Graph is ``wait_for_all()``, each thread submitting the work to the graph would be required to wait
until **all** of the tasks would be done, no meter if these tasks corresponds to the input processed by this thread or not. If some
post-processing is required on each thread after receiving the computation result, it would be only possible to start it when the Flow Graph would be completed what can be inefficient if the post-processing of lightweight graph tasks would be blocked by processing the more mature input.

To get rid of this negative performance effect, it would be useful to add some kind of new API to the Flow Graph that would wait for
completion of only one message (instead of the full completion of the graph):

```cpp
ComputeOutput compute(ComputeInput input)
{
    m_start_node.try_put(input); // Submit work in the graph
    g.wait_for_one(input); // Pseudo-code. Wait for completion of only messages for computations of input
}
```

## Proposal

The idea of this proposal is to extend the existing Flow Graph API with the new member function of each receiver nodes -
``node.try_put_and_wait(msg)``. This function should submit the msg into the Flow Graph (similarly to ``try_put()``) and wait for its completion. The function should be exited only if all of the tasks corresponds to the ``msg`` and skip waiting for any other tasks to
complete.

Consider the following graph:

```cpp

using namespace oneapi::tbb;

flow::graph g;

flow::broadcast_node<int> start(g);

flow::function_node<int, int> f1(g, unlimited, f1_body);
flow::function_node<int, int> f2(g, unlimited, f2_body);
flow::function_node<int, int> f3(g, unlimited, f3_body);

flow::join_node<std::tuple<int, int, int>> join(g);

flow::function_node<int, int> pf(g, serial, pf_body);

flow::make_edge(start, f1);
flow::make_edge(start, f2);
flow::make_edge(start, f3);

flow::make_edge(f1, flow::input_port<0>(join));
flow::make_edge(f2, flow::input_port<1>(join));
flow::make_edge(f3, flow::input_port<2>(join));

flow::make_edge(join, pf);

// Parallel submission
oneapi::tbb::parallel_for (0, 100, [](int input) {
    start.try_put(input);
});

start.try_put_and_wait(444);
// Post-processing 444

g.wait_for_all();
```

Each message is broadcasted from ``start`` to three concurrent computational functions ``f1``, ``f2`` and ``f3``. The result is when joined into single tuple in ``join`` node and
post-processed in a serial ``pf`` function node. The task queue corresponding to each node in the graph is exposed under the node in the picture. The tasks that corresponds
to the parallel loop 0-100 are shown as blue tasks in the queue. Red tasks corresponds to the message submitted as an argument in ``try_put_and_wait``.
The ``try_put_and_wait`` is expected to exit when all of the red tasks and the necessary amount of blue tasks would be completed. Completion of all blue tasks as in ``wait_for_all``
is not guaranteed.

From the implementation perspective, the feature is implemented currently by creating an instance of special class ``message_metainfo`` with the input message in ``try_put_and_wait``
and then broadcast it through the graph together with the message. The actual value of message can be changed during the computation but the stored metainformation should be preserved.

When the message is buffered in one of the buffering nodes or one of the internal buffers (such as ``queueing`` ``function_node`` or ``join_node``), the corresponding metainformation
instance should be buffered as well.

For reference counting on single messages, the dedicated ``wait_context`` is assigned to each message passed to ``try_put_and_wait``. It is possible to use ``wait_context`` itself
instead of ``message_metainfo``, but it can be useful to pass something with each message through the graph, not only for single message waiting. The initial implementation of
``message_metainfo`` just wrapping the ``wait_context``, but it can be extended to cover additional use-cases. Each task corresponding to the completion of the message
associated with the awaited message holds the reference counting on the corresponding ``wait_context``. In case of buffering the message somewhere in the graph,
the additional reference counter would be held and released when the item is removed from the buffer.

From the implementation perspective, working with metainformation is exposed by adding the new internal virtual functions in the Flow Graph:

| Base Template Class | Existing Function Signature   | New Function Signatures                             | Information                                      |
|---------------------|-------------------------------|-----------------------------------------------------|--------------------------------------------------|
| receiver            | bool try_put_task(const T& t) | bool try_put_task(const T& t) \n                    | Performs an action required by the node logic.   |
|                     |                               | bool try_put_task(const T& t,                       | May buffer both ``t`` and ``metainfo``.          |
|                     |                               |                   const message_metainfo& metainfo) | May broadcast the result and ``metainfo`` to     |
|                     |                               |                                                     | successors of the node.                          |
|                     |                               |                                                     | The first function can reuse the second with     |
|                     |                               |                                                     | the empty metainfo.                              |
|---------------------|-------------------------------|-----------------------------------------------------|--------------------------------------------------|
| sender              | bool try_get(T& t)            | bool try_get(T& t)                                  | For buffers, gets the element from the buffer.   |
|                     |                               | bool try_get(T& t, message_metainfo&)               | The second function provides both placeholders   |
|                     |                               |                                                     | for metainformation and the element.             |
|---------------------|-------------------------------|-----------------------------------------------------|--------------------------------------------------|
| sender              | bool try_reserve(T& t)        | bool try_reserve(T& t)                              | For buffers, reserves the element in the buffer. |
|                     |                               | bool try_reserve(T& t, message_metainfo& metainfo)  | The second function provides both placeholders   |
|                     |                               |                                                     | for metainformation and the element.             |
|---------------------|-------------------------------|-----------------------------------------------------|--------------------------------------------------|

The ``message_metainfo`` class is described in details in the [separate section](#details-about-metainformation-class).
The [Nodes behavior](#nodes-behavior) section describes the behavior of each particular node when the metainformation is received.

## Nodes behavior

This chapter describes detailed behavior of each Flow Graph node when the item and the metainformation is received. Similarly to the message itself, the metainformation
can be received from the predecessor node (explicit ``try_put_task`` call) or initially from ``try_put_and_wait``.

### ``function_node<input, output, queueing>``

If the concurrency of the ``function_node`` is ``unlimited``, the node creates a task for executing the body of the node. The created task should hold the metainfo
received by the function node and broadcast it to the node successors when the task is completed.

Otherwise, similarly to the original ``function_node`` behavior, the node tries to occupy its concurrency. If the limit is not yet reached, creates a body task
similarly to the ``unlimited`` case. If the concurrency limit is reached, both input message and the associated metainformation would be stored in the internal queue, associated
with the node. When one of the other tasks, associated with the node would be completed, it will retrieve the postponed message together with the metainformation and spawn it as
a task.

Since the ``function_node`` guarantees that all of the elements would be retrieved from the internal queue at some time, [buffering issues](#buffering-the-metainfo) cannot take place.

### ``function_node<input, output, rejecting>``

For the ``unlimited`` use-case, behaves the same as ``queueing`` node.

If the concurrency limit of the node is reached, both message and the associated metainfo would be rejected and it is a predecessor responsibility to buffer them.
If the predecessor is not the buffering node, both message and the metainfo would be lost.
When another task would be completed, it will try to get a buffered message together with the metainfo (by calling the ``try_get(msg, metainfo)`` method) from the predecessor node.

Since the ``function_node`` guarantees that all of the elements would be retrieved from the internal queue at some time, [buffering issues](#buffering-the-metainfo) cannot take place
for buffering nodes, preceding the ``function_node``.

### ``function_node<input, output, lightweight>``

In regard to the concurrency limit, the lightweight function node behaves as it described in the corresponding buffering policy section (``queueing`` or ``rejecting``).
The only difference is that for such nodes the tasks would not be spawned and the associated function will be executed by the calling thread. And since we don't have tasks,
the calling thread should broadcast the metainformation to the successors after completing the function.

### ``continue_node``

The ``continue_node`` has one of the most specific semantics in regard to the metainformation. Since the node only executes the associated body (and broadcasts the signal
to the successors) if it receives ``N`` signals from it's predecessors (where ``N`` is the number of predecessors). It means that prior to executing the body,
the node can receive several metainformation instances from different predecessors.

To handle this, the ``continue_node`` initially stores an empty metainfo instance and on each ``try_put_task(continue_msg, metainfo)`` call, it [merges](#details-about-metainformation-class)
the received metainformation with the stored instance. Under the hood the merged instance will contain the ``wait_context`` pointers from its previous state and all of the pointers from
the received ``metainfo``.

When the ``continue_node`` receives ``N`` signals from the predecessors, it wraps stored metainformation into the task for completion of the associated body. Once the task is ready, the stored
metainformation instance switch back to the empty state for further work. Once the function would be completed, the task is expected to broadcast the metainfo to the successors.

The lightweight ``continue_node`` behaves the same as described above, but without spawning any tasks. Everything would be performed by the calling thread.

### Multi-output functional nodes

``multifunction_node`` and ``async_node`` classes are not currently supported by this feature because of issues described in [the separate section](#multi-output-nodes-support).

Passing the metainformation to such a node by the predecessor would have no effect and no metainfo would be broadcasted to further successors.

### Single-push buffering nodes

This section describes the behavior for ``buffer_node``, ``queue_node``, ``priority_queue_node`` and ``sequencer_node`` classes. The only difference between them would be in
ordering of retrieving the elements from the buffer.

Once the buffering node receives a message and the metainformation, both of them should be stored into the buffer.

Since buffering nodes are commonly used as part of the Flow Graph push-pull protocol, e.g. before the rejecting ``function_node`` or reserving ``join_node``, it means that
the waiting for the message should be prolonged once it stored in the buffer. In particular, once the metainformation is in the buffer, the buffer should call ``reserve(1)`` on each
associated ``wait_context`` to prologue the wait and call ``release(1)`` once the element is retrieved from the buffer (while calling ``try_get`` or ``try_consume``).

Once the element and the metainfo are stored in the buffer, the node will try to push them to the successor. If one of the successors accepts the message and the metainfo,
both of them are removed from the buffer. Otherwise, the push-pull protocol works and the successor should return for the item once it becomes available by calling
``try_get(msg, metainfo)`` or ``try_reserve(msg, metainfo)``.

Since placing the buffers before rejecting nodes is not the only use-case, there is a risk of issues relates to buffering. It is described in details in the [separate section](#buffering-the-metainfo).

### Broadcast-push buffering nodes

The issue with broadcast-push ``overwrite_node`` and ``write_once_node`` is these nodes stores the received item and even if this item is accepted by one successor, it would be broadcasted to others and
kept in the buffer. Since the metainformation is kept in the buffer together with the message itself, even if the computation is completed, the ``try_put_and_wait`` would stuck because of the reference
held by the buffer.

Even the ``wait_for_all()`` call would be able to finish in this case since it counting only the tasks in progress and ``try_put_and_wait`` would still stuck.

``try_put_and_wait`` feature for the graph containing these nodes should be used carefully because of this issue:

* The ``overwrite_node`` should be explicitly reset by calling ``node.reset()`` or the element with the stored metainfo should be overwritten with another element without metainfo.
* The ``write_once_node`` should be explicitly reset by calling ``node.reset()`` since the item cannot be overwritten.

### ``broadcast_node``

The behavior of ``broadcast_node` is pretty obvious - the metainformation would just be broadcasted to each successor of the node.

### ``limiter_node``

If the threshold was not reached, both value and the metainformation should be provided to the successors. Otherwise- both should be rejected and buffered by another node. 

Metainformation on the decrement port is ignored since this signal should not be considered part of working on the original message.

### ``join_node<output_tuple, queueing>``

Each input port of the join_node should support the queue for both values and the associated metainformations. Once all of the input ports would contain the value, the values
should be combined into single tuple output and the metainformation objects should be combined into single metainfo using `metainfo1.merge(metainfo2)`, associated with the tuple
and submitted to successors.

### ``join_node<output_tuple, reserving>``

Buffering node should be used before each input port for storing the values and the associated metainformations. Once all of the input ports would be triggered with the input value,
the values and the metainformations should be reserved from the buffering nodes, values should be combined into single tuple output and the metainformation objects should be
combined into single metainfo using `metainfo1.merge(metainfo2)`, associated with the tuple and submitted to successors.

### ``join_node<output_tuple, key_matching>``

Similar to other `join_node` implementations, except the values and the metainformation objects are stored in the hash map inside of the port.

### ``split_node``

The split node should take the tuple object and the corresponding metainformation, split the tuple and submit the single values from the tuple to the corresponding ports. 
Metainformation object copy should be submitted together with each element into each output port.

Metainfo should not be split since is is unclear what is the relation between elements in the tuple and the metainformation objects stored in the internal list so all of them should
be provided to the successors.

### ``indexer_node``

The behavior is pretty obvious - provide the tagged value to all of the successors together with the originally associated metainfo.

### ``composite_node``

``composite_node`` does not require any additional changes. `try_put_and_wait` feature and the explicit support for metainformation should be done by the nodes inside of the composite.

## Additional implementation details

### Details about metainformation class

``message_metainfo`` class synopsis:

```cpp
class message_metainfo
{
public:
    using waiters_type = std::forward_list<d1::wait_context_vertex*>;

    message_metainfo();

    message_metainfo(const message_metainfo&);
    message_metainfo(message_metainfo&&);

    message_metainfo& operator=(const message_metainfo&);
    message_metainfo& operator=(message_metainfo&&);

    const waiters_type& waiters() const &;
    waiters_type&& waiters() &&;

    bool empty() const;

    void merge(const message_metainfo&);
};
```

The initial implementation of ``message_metainfo`` class wraps only the list of single message waiters. The class may be extended if necessary to cover additional use-cases.

The metainfo is required to hold a list of message waiters instead of single waiter to cover the ``continue_node`` and ``join_node`` joining use-cases. Consider the example:

```cpp
using namespace oneapi::tbb;

flow::function_node<int, int> start1(g, ...);
flow::function_node<int, int> start2(g, ...);

flow::join_node<std::tuple<int, int>> join(g);

flow::function_node<std::tuple<int, int>, int> post_process(g, ...);

flow::make_edge(start, flow::input_port<0>(join));
flow::make_edge(start, flow::input_port<1>(join));
flow::make_edge(join, post_process);

std::thread t1([&]() {
    start1.try_put_and_wait(1);
});

std::thread t2([&]() {
    start2.try_put_and_wait(2);
})
```

### Combined or separated wait

Current proposal describes only the case where submitting the work into the flow graph and waiting for it are combined in a single use API `node.try_put_and_wait`.
In theory, it can be useful to also have the ability to split these phases:

```cpp

oneapi::tbb::flow::graph g;
oneapi::tbb::flow::function_node<int, int> start_node(g, ...);

auto desc = node.try_put(work); // other API name can be selected

// Other work
// May submit more work into the Flow Graph
// May create other descriptors that would be waited later

g.wait(desc);

```

In that case it would be needed to extend the node with the new API returning some descriptor that can be used as a hit work waiting function `g.wait` that also should be added.
This descriptor can wrap the metainformation class for the current proposal, but the exact semantics and API should be defined since it makes the metainfo class public in some manner.

### Buffering the metainfo

As described in the [buffering nodes description section](#single-push-buffering-nodes), current proposal requires the nodes that stores the user values as part of buffers to store
also the metainformation associated with these values in the same buffer for future use and also to hold the additional reference counter to force the `try_put_and_wait` to wait 
until the rejecting receiver take the item from the buffer and process it. 

Such a behavior may significantly affect the other common use-case for buffering nodes: when they are used to store the result of the computation at the end of the Flow Graph. 
In such scenarios, the metainformation would be stored in the buffer and never taken from it since the buffering node is not used as part of push-pull protocol and hence there is
no rejecting successor. The `try_put_and_wait` function associated with such a metainformation will hang forever since the reference counter on the stored metainformation would
never be decreased.

It is impossible to rely on the number of successors while making a decision to store the metainformation in the buffer since if the node is used as part of the push-pull protocol,
the number of successors is also equal to `0` since the edge is reversed.

Current proposal considers this scenario is a `try_put_and_wait` feature limitation and does not add any support for that.

### Multi-output nodes support

Multi-output nodes (`multifunction_node` and `async_node`) creates an extra issue for the wait-for-one feature. Consider the following example (all of the examples shown
in this section are for `multifunction_node` but also affects `async_node` in the same manner):

```cpp

using mf_node_type = multifunction_node<int, std::tuple<int, int>>;
using output_ports = typename mf_node_type::output_ports_type;

mf_node_type mfn(g, unlimited,
    [](int input, output_ports& ports) {
        std::get<0>(ports).try_put(output);
        std::get<1>(ports).try_put(output);
    })

```

Unlike the `function_node`, the computed output is not propagated implicitly to the successors. It is done explicitly by the user by calling
`try_put(output)` on the corresponding output_port of the node. Since the metainformation associated with the particular input is hidden from the user
inside of the Flow Graph implementation, it cannot be propagated to the successors as part of the explicit user try_put.

An other interesting use-case is when the `multifunction_node` is used as a reduction for multiple input values with only one output. In that case the metainformation
should not be automatically propagated at all and should be accumulated simultaneously with the outputs.

The current proposal is to extend the body of the `multifunction_node` with the third optional parameter of some tag type that wraps the metainformation:

```cpp

using mf_node_type = multifunction_node<int, std::tuple<int, int>>;
using output_ports = typename mf_node_type::output_ports_type;
using tag_type = typename mf_node_type::tag_type;

mf_node_type mfn(g, unlimited,
    [](int input, output_ports& ports, tag_type&& tag) {
        std::get<0>(ports).try_put(output, tag);
        std::get<1>(ports).try_put(output, std::move(tag));
    })

```

We still need to support the user body with just two parameter for backward compatibility. If such a body is provided, the associated metainformation would be ignored
and never broadcasted to any successors of the node.

The tag can be saved on the user side and provided to some `try_put` call as part of other calls to the body. It will hold an extra reference counted on the associated metainfo
object to extend the wait until the corresponding item is processed. 

Current proposal describes several approaches to implement the `tag_type`:

```cpp
class multi_tag {
    message_metainfo my_metainfo;
public:
    multi_tag() = default;

    // Should definitely be movable
    multi_tag(multi_tag&&);
    multi_tag& operator=(multi_tag&&);

    // Can be copyable
    multi_tag(const multi_tag&)
    multi_tag& operator=(const multi_tag&);

    ~multi_tag();

    void reset(); // Decreases the ref counters in my_metainfo
    void merge(const multi_tag&); // Useful for reduction use-cases, should be thread-safe
};
```

For all of the implementation approaches, as stated above, once creating the `multi_tag` object, the extra reference counter would be added on the associated metainformation object.
The main question is when this reference counter should be decreased.

The first option is to decrease it once the tag object is destroyed (similar to lifetime management in a smart pointer class). 
The second - once it is move-consumed (passed to `try_put` as rvalue) by one of the output ports (similar to the raw pointer object lifetime management).

Let's consider several `multifunction_node` use-cases:
* One-to-one - for each input we have one output on each output port of the node.
* One-to-zero - for some input we don't have any outputs on any of ports.
* Many-to-one - several inputs are accumulated somehow into a single one and submitted once (reduction).
* Many-to-zero - several inputs are accumulated but on some point the reduction is cancelled without output provided.

### One-to-one use-case

Raw-pointer-like implementation approach:

```cpp
node_type node(g, unlimited,
    // tag is created internally holding an extra ref counter
    [](int input, ports_type& ports, tag_type&& tag) {
        // copy-consume, no reference counters increased or decreased
        std::get<0>(ports).try_put(input, tag);

        // 1: move-consume, decrease the ref-counter
        std::get<1>(ports).try_put(input, std::move(tag));

        // 2: explicit reset on the tag, decrease the ref-counter
        tag.reset();
    });
```

It is important to mention that in for such an implementation approach, if only the copy-consumes would be used by the user, the reference counter would never be decreased and
it will cause hangs in the corresponding `try_put_and_wait` function. It is really easy to make a mistake here.

Smart-pointer-like implementation approach:

```cpp
node_type node(g, unlimited,
    // tag is created internally holding an extra ref counter
    [](int input, ports_type& ports, tag_type&& tag) {
        // No difference between copy and move-consume
        // Reference counters are not touched in both use-cases
        std::get<0>(ports).try_put(input, tag);
        std::get<1>(ports).try_put(input, std::move(tag));
    });
```

In that case, even if the tag was not move-consumed or explicitly reset, the corresponding ref counter would be decreased once internal tag type would be destroyed.

### One-to-zero use-case

Raw-pointer-like approach:

```cpp
node_type node(g, unlimited,
    // tag is created internally holding an extra ref counter
    [](int input, ports_type& ports, tag_type&& tag) {
        tag.reset();
    });
```

Even if no outputs are generated for the specific input, the tag should be explicitly reset since the library should receive an external signal to decrease the ref counter.

Smart-pointer-like approach:

```cpp
node_type node(g, unlimited,
    // tag is created internally holding an extra ref counter
    [](int input, ports_type& ports, tag_type&& tag) {});
```

Since the reference counter would be decreased once the internal tag object would be destroyed, no external signals are required and the core is clearly obvious
from the user's perspective.

### Many-to-one use-case

For this use-case, the user code is the same for both raw-pointer and smart-pointer-like approaches:

```cpp
int accumulated_result = 0;
tag_type accumulated_tag;

node_type node(g, unlimited,
    // tag is created internally holding an extra ref counter
    [](int input, ports_type& ports, tag_type&& tag) {
        if (accumulate) {
            accumulated_result += input;
            accumulated_hint.merge(tag); // should be thread-safe
        } else {
            std::get<0>(ports).try_put(accumulated_result, accumulated_hint); // copy-consume
            std::get<1>(ports).try_put(accumulated_result, std::move(accumulated_hint)); // move-consume
        }
    });
```

Some amount of inputs are reduced into the single variable `accumulated_result` and provided as a single output once the accumulate condition is not met.
The idea is to accumulate the tag simultaneously in a `accumulated_tag` variable and submit it together with `accumulated_result`.

For both use-cases, the associated reference counter would be decreased in a move-consume case (or an explicit `reset`), once the management of the `accumulated_hint`
would be transferred from the user side to the library.

### Many-to-zero use-case

For this use-case, the user code is the same for both raw-pointer and smart-pointer-like approaches:

```cpp
int accumulated_result = 0;
tag_type accumulated_tag;

node_type node(g, unlimited,
    // tag is created internally holding an extra ref counter
    [](int input, ports_type& ports, tag_type&& tag) {
        if (accumulate) {
            accumulated_result += input;
            accumulated_hint.merge(tag); // should be thread-safe
        } else if (cancel_accumulation) {
            accumulated_result = 0;
            accumulated_tag.reset();
        }
    });
```

In both use-cases, the tag is required to be explicitly reset to signal the library that the object is not necessary anymore.

### Smart pointer: unique or shared

For the smart-pointer like approach, there also can be two options
* unique_ptr-like semantics. The tag is non-copyable. The reference counter is increased once while creating the tag and decreased once while destroying the tag.
* shared_ptr-like semantics. The tag is copyable. The reference counter is increased when the tag is created or a copy of the tag is created.
  The ref counter is decreased once the tag or it's copy is destroyed.

The shared-ptr-like approach is more flexible from the user perspective because of copy semantics defined but at the same time more dangerous since all of the copies on the user
side are holding it's own reference counted on the metainfo and once the tag is consumed, the user would be expected to reset all of the copies for correct behavior. 

### Tag implementation approaches summary

| Aspect                                                 | raw-pointer-like | shared_ptr-like | unique_ptr-like |
|--------------------------------------------------------|------------------|-----------------|-----------------|
| All use-cases are covered                              | Yes              | Yes             | Yes             |
|--------------------------------------------------------|------------------|-----------------|-----------------|
| Tag is copyable                                        | Yes              | Yes             | No              |
|--------------------------------------------------------|------------------|-----------------|-----------------|
| Copy semantics does not pressure the reference counter | Yes              | No              | Yes (no copy)   |
|--------------------------------------------------------|------------------|-----------------|-----------------|
| Tag is movable                                         | Yes              | Yes             | Yes             |
|--------------------------------------------------------|------------------|-----------------|-----------------|
| Move does not increase ref counters                    | Yes              | Yes             | Yes             |
|--------------------------------------------------------|------------------|-----------------|-----------------|
| Explicit reset method is required                      | Yes              | Yes             | Yes             |
|--------------------------------------------------------|------------------|-----------------|-----------------|
| Required less accuracy from the user                   | No               | Yes             | Yes             |
|--------------------------------------------------------|------------------|-----------------|-----------------|

Following the initial discussions on this proposal, the raw-pointer-like approach was considered the most flexible, but the most dangerous from the user perspective.
The shared_ptr-like approach was still considered flexible, but adds pressure on the reference counter and requires the user to manage all of the copies of the tag
to achieve the correct behavior.
The unique_ptr-like approach was considered a balance between flexibility and danger since it provides the required minimum of operations and requires minimal extra effort from the user.

Currently, the proposal relies on the unique_ptr-like approach as the main one. If required, it would be easy to switch to the shared_ptr-like approach in the future. 

## Process Specific Information

Open questions:

* Multi-output nodes support should be finalized
* More feedback from customers is required
* Move wide testing should be enabled for the proposed implementation
