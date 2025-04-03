# Waiting for single messages in the Flow Graph

This feature extends the oneTBB Flow Graph interface with a ``try_put_and_wait(msg)`` that supports waiting
for completion of the chain of tasks related to the ``msg``.

The feature may improve Flow Graph performance in scenarios where multiple threads submit work into the same
Flow Graph and each of them need to wait for only the work associated with their message to complete.

## Introduction

Without this feature, the oneTBB Flow Graph supports two basic actions after building the graph:

- Submitting messages in some nodes using the ``receiver::try_put`` or ``input_node::activate`` API.
- Waiting for completion of **all** messages in the graph using the ``graph::wait_for_all``.

Since the only API currently available for waiting until the work would be completed is ``wait_for_all`` and it waits for all
tasks in the graph to complete, there can be negative performance impact in use cases when the thread submitting the work 
should be notified as soon as possible when the work is done. 

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
submit the input provided to the computation graph and wait for completion of the input. 
This function also can be executed concurrently from several threads as it shown in the ``main``.

While using the ``wait_for_all()``, each thread submitting the work to the graph is required to wait
until **all** of the tasks to be done, even those that are unrelated to the input submitted. If some
post-processing is required on each thread after receiving the computation result, it would be only safe to start it when the 
Flow Graph is completed. That is inefficient since the post-processing of lightweight graph tasks are blocked by processing of unrelated inputs.

To remove this negative performance effect, the ``try_put_and_wait`` extension waits only for completion of one message by each node in the graph
(instead of the full completion of the graph):

```cpp
ComputeOutput compute(ComputeInput input)
{
    // Submits input and waits for its completion
    m_start_node.try_put_and_wait(input);
}
```

## Overview of experimental feature

This feature extends the existing Flow Graph API with an additional member function for each of the receiver nodes -
``node.try_put_and_wait(msg)``. This function submits the ``msg`` into the Flow Graph (similarly to ``try_put(msg)``)
and wait for its completion.

The function blocks until all of the tasks related to processing ``msg`` complete and is allowed to skip waiting for any other tasks to
complete.

Consider the following graph:

```cpp
namespace flow = oneapi::tbb::flow;

flow::graph g;

flow::function_node<int, int> pre_process(g, flow::serial, pre_process_body);

flow::function_node<int, int> f1(g, flow::unlimited, f1_body);
flow::function_node<int, int> f2(g, flow::unlimited, f2_body);

flow::join_node<std::tuple<int, int>, flow::queueing> join(g);

flow::function_node<int, int> post_process(g, flow::serial, post_process_body);

flow::make_edge(pre_process, f1);
flow::make_edge(pre_process, f2);

flow::make_edge(f1, flow::input_port<0>(join));
flow::make_edge(f2, flow::input_port<1>(join));

flow::make_edge(join, post_process);

// Parallel submission
oneapi::tbb::parallel_for (0, 100, [](int input) {
    pre_process.try_put(input);
});

start.try_put_and_wait(444);
// Post-processing 444

g.wait_for_all();
```

<img src="try_put_and_wait_graph.png" width=400>

Each input message is pre-processed in the serial ``pre_process`` node and the output is broadcasted to two concurrent computational functions
``f1`` and ``f2``.

The result is joined into a single tuple in ``join`` node and then post-processed in a serial ``post_process`` functional node.

The order of processing the tasks corresponding to each input item is exposed under each node in the picture. 

The tasks are shown in the right-to-left order - the tasks on the right are submitted earlier then the tasks on the left.

Blue tasks relates to the parallel loop 0-100. Red tasks relates to the ``444`` message submitted as an input to ``try_put_and_wait``.

Since ``pre_process`` is a serial ``function_node``, the items processing cannot be re-ordered and the red task would be in the leftmost position
since it was submitted after the blue tasks.

``f1`` and ``f2`` are ``unlimited`` ``function_node``s, so the tasks ordering is arbitrary since tasks for each computed item are spawned.

``join`` is a queueing ``join_node`` and it should preserve the ordering of items as they are processed by ``f1`` and ``f2``. The tasks are shown
as partially blue and partially red since the output of red task from ``f1`` can be combined with the output of blue task from ``f2`` and wise versa.

Since ``post_process`` is a serial ``function_node``, the ordering of the tasks would be the same as in ``join``.

The ``try_put_and_wait`` is expected to exit when all of the red (including partially red) tasks are completed. It may require some amount of blue tasks
to be completed as well, e.g. to execute previously stored tasks in the ``function_node``'s queue. 

Completion of all blue tasks as in ``wait_for_all`` is not guaranteed.

## Feature Design

This feature is implemented by creating an instance of special private class ``message_metainfo`` for each input message in ``try_put_and_wait``. This instance
wraps the pointer to the dedicated ``wait_context`` object representing the reference counter for single input message.

``message_metainfo`` is broadcasted through the graph together with the message itself. The actual value and the type of the message can be changed during the computation, but the stored 
metainformation is preserved.

The reference counter in the ``wait_context`` inside of the ``message_metainfo`` is increased in the following cases:
* The task associated with the computations of the ``try_put_and_wait`` input message is created (*).
* The item associated with the computations of the desired input is stored in the buffering node or in the internal buffer of the other node type.
* When the ``continue_msg`` with the non-empty associated waiter is received by ``continue_node``. In this case, the node buffers the ``message_metainfo``s received from each
  predecessor and increases the underlying reference counter to prolonge the wait until signals from each predecessor would be received. 

The reference counter is decreased when:
* The task associated with the computations of the ``try_put_and_wait`` input is finalized. If the output of the task should be propagated to the successors of the node, it is done
  from the task body and hence the reference counter would be decreased only after creating the task for each successor and increasing the reference counter for them.
* The item associated with the computations is taken from the buffering node or from the internal buffer.
* When the desired number of signals from the predecessors was received by the ``continue_node``. This case is equivalent to retrieving the set of buffered ``message_metainfo``s received previously
  from each predecessor. 

``message_metainfo`` class may be reused in the future to support additional use-cases when it is required to push additional information about the input message through the graph. E.g. supporting
priorities for single messages - the corresponding priority tag can be assigned as part of the metainfo.

Metainformation class supports containing multiple reference counters at the same time to support joining messages with different associated reference counters in the ``join_node``. 
See [separate section](#details-about-metainformation-class) for more details.

Implementation-wise, processing the metainformation is exposed by adding new internal virtual member functions to various Flow Graph instance:

``` cpp
template <typename T>
class receiver {
protected:
    virtual bool try_put_task(const T& t) = 0; // Existing API
    virtual bool try_put_task(const T& t, const message_metainfo& info) = 0; // New API
};
```

For each particular implementation of ``receiver``, the ``try_put_task`` performs ab action that is required by the corresponding Flow Graph node. 

It may buffer both ``t`` and ``metainfo`` or broadcast the result and the ``info`` to the successors of the node. 

The existing API ``try_put_task(const T& t)`` can reuse the new one with the empty metainfo object.

```cpp
template <typename T>
class sender {
public:
    // Existing API
    virtual bool try_get(T& t) { return false; }
    virtual bool try_reserve(T& t) { return false; }

    // New API
    virtual bool try_get(T& t, message_metainfo& info) { return false; }
    virtual bool try_reserve(T& t, message_metainfo& info) { return false; }
};
```

For each particular implementation of ``sender``, ``try_get`` gets the element and the metainfo from the buffer and assigns the message to ``t`` and
the metainfo to ``info``. The reference counter/s, associated with the stored metainfo are decremented.

``try_reserve`` implementation reserves the element and the corresponding metainfo inside of the buffer and feels the placeholders provided. Since the elements are not
removed from the buffer, the reference counter/s remains unchanged. They will be decremented when ``try_consume`` is called. 

## Nodes behavior

This chapter describes detailed behavior of each Flow Graph node when the item and the metainformation is received. Similarly to the message itself, the metainformation
can be received from the predecessor node (explicit ``try_put_task`` call) or initially from ``try_put_and_wait``.

### Queueing ``function_node``

If the concurrency of the ``function_node`` is set to ``unlimited``, the node creates a task for executing the body of the node. The created task holding the reference counter on each
``wait_context``s stored in ``metainfo`` and also wraps the ``metainfo`` object itself since it would be broadcasted to the successors when the task is completed.

If the concurrency is not ``unlimited``, the call to ``try_put_task`` tries to occupy the concurrency of the node. If the thread limit is not yet reached - behaves the same as 
in the ``unlimited`` case. Otherwise, both input message and the metainfo are stored in the internal queue of the node. When one of the node tasks is completed, it retrieves 
the postponed message and the corresponding metainfo from the queue and spawns a task to process it.

Since the ``function_node`` guarantees that all of the elements would be retrieved from the internal queue at some time, [buffering issues](#buffering-the-metainfo) cannot take place.

### Rejecting ``function_node``

If the concurrency of the node is set to ``unlimited``, behaves the same as in the ``queueing`` case described above.

Otherwise, if the concurrency limit of the node is reached, both message and the associated metainformations would be rejected and the predecessor that called the ``try_put_task``
is responsible on buffering both of them.

If the predecessor is not a buffering node, both message and the metainfo would not be processed or stored somewhere. From the waiting perspective, it means that the computations
of the waited message are considered completed and if there are no other tasks/ buffered items corresponding to the same ``try_put_and_wait`` input in the graph, the ``try_put_and_wait``
call would be exited.

When some ``function_node`` task is completed, it will try to get a buffered message and the metainfo from the predecessor by calling the ``try_get(msg, metainfo)`` method. 

Since the ``function_node`` guarantees that all of the elements would be retrieved from the predecessor, [buffering issues](#buffering-the-metainfo) cannot take place
for buffering nodes, preceding the ``function_node``.

### Lightweight ``function_node``

Calls to ``try_put_task`` in the lightweight node will operate on the concurrency limit of the node in the same manner as is defined by the message buffering policy -
``queueing`` (default)  or ``rejecting``.

The difference is that for lightweight nodes the tasks would not be spawned in most of the cases and the node body will be executed by the calling thread. 
Since there are no tasks, the calling thread would broadcast the output and the metainformation to the successors after completing the function.

### ``continue_node``

``continue_node`` only executes the associated body (and broadcasts the signal to the successors) when it receives ``N`` signals from the predecessors, where ``N``
is the number of predecessors. 

It means that prior to executing the body, the node can receive several ``metainfo`` instances from different predecessors. To handle this, the node initially stores an
empty metainfo instance inside itself and each call to ``try_put_task`` with non-empty metainfo, merges the received metainformation with the stored instance.

Additional reference counter would be held on each input ``wait_context`` to make sure the corresponding ``try_put_and_wait`` will remain blocked until the item would leave the
``continue_node``. 

When the ``continue_node`` receives the last signal from the predecessors, it first saves a copy of the metainfo stored in the node and containing the merged metainfo from all of the
predecessors and stores the empty metainformation in the node. Then, the node creates and spawns a task to complete the associated body. The copy of the previously stored metainfo objects
is associated with the new task.

Implementation-wise, copying and resetting the metainfo is done under the ``continue_node`` mutex together with the internal predecessor counter update and check. The task is spawned after
releasing the mutex, so other threads can operate with the stored counter and metainfo while body task is executing.

The lightweight ``continue_node`` behaves the same as described above, but without creating any tasks. Everything would be performed by the calling thread.

### Multi-output functional nodes

``multifunction_node`` and ``async_node`` classes are not currently supported by this feature because of issues described in [the separate section](#multi-output-nodes-support).

Passing the metainformation to such a node by the predecessor would have no effect and no metainfo would be broadcasted to further successors.

### Single-push buffering nodes

This section describes the behavior for ``buffer_node``, ``queue_node``, ``priority_queue_node`` and ``sequencer_node`` classes. The only difference between them would be in
ordering of retrieving the elements from the buffer.

As it was described above, once the buffering node receives a message and the metainformation, both of them should be stored into the buffer.

Since buffering nodes are commonly used as part of the Flow Graph push-pull protocol, e.g. before the rejecting ``function_node`` or reserving ``join_node``,
the waiting for the message should be prolonged once it is stored into the buffer. In particular, once the metainformation is in the buffer, the buffer should call ``reserve(1)`` on each
associated ``wait_context`` to prologue the wait and call ``release(1)`` once the element is retrieved from the buffer (while calling ``try_get`` or ``try_consume``).

Once the element and the metainfo are stored in the buffer, the node will try to push them to the successor. If one of the successors accepts the message and the metainfo,
both of them are removed from the buffer. Otherwise, the push-pull protocol works and the successor should return for the item once it becomes available by calling
``try_get(msg, metainfo)`` or ``try_reserve(msg, metainfo)``.

Since placing the buffers before rejecting nodes is not the only use-case, there is a risk of issues relates to buffering. It is described in details in the [separate section](#buffering-the-metainfo).

### Broadcast-push buffering nodes

The issue with broadcast-push ``overwrite_node`` and ``write_once_node`` is these nodes stores the received item and even if this item is accepted by one of the successors,
it would be broadcasted to others and kept in the buffer.

Since the metainformation is kept in the buffer together with the message itself, even if the message is consumed by a successor, 
``try_put_and_wait`` will not complete because of the reference held by the buffer until the node is explicitly cleared.

Even the ``wait_for_all()`` call would be able to finish in this case since it counting only the tasks in progress and ``try_put_and_wait`` would still be blocked.

``try_put_and_wait`` feature for the graph containing these nodes should be used carefully because of this issue:

* The ``overwrite_node`` should be explicitly reset by calling ``node.clear()`` or the element with the stored metainfo should be overwritten with another element.
* The ``write_once_node`` should be explicitly reset by calling ``node.clear()`` since the item cannot be overwritten.

### ``broadcast_node``

While ``broadcast_node::try_put_task`` is called with the metainfo argument - both item and the associated metainformation would be broadcasted to each successor of the node.

### ``limiter_node``

If the threshold of the node was not reached, both value and the metainformation should be broadcasted to the successors. Otherwise- both should be rejected and buffered by another node. 

Metainformation on the decrement port is ignored since this signal should not be considered part of working on the original message.

### Queueing ``join_node``

Each input port of the join_node should support the queue for both values and the associated metainformations. Once all of the input ports contain the value, the values
should be combined into single tuple output and the metainformation objects should be combined into single metainfo using `metainfo1.merge(metainfo2)`, associated with the tuple
and submitted to successors.

If an item with the metainformation is stored in the internal queue of one of the input ports, but items are never received by other ports, the item and the metainformation will be kept in the
queue and block the corresponding ``try_put_and_wait`` call.

### Reserving ``join_node``

Buffering node should be used before each input port for storing the values and the associated metainformations.

Once all of the input ports would be triggered with the input value, the values and the metainformations would be reserved from the buffering nodes,
values would be combined into single tuple output and the metainformation objects would be combined into single metainfo using `metainfo1.merge(metainfo2)`,
associated with the tuple and submitted to successors.

Similar to the ``queueing`` case, if one of the input ports was triggered with the input value, but others never receive any values, the item and the metainformation would be kept in the
buffer and block the corresponding ``try_put_and_wait`` call.

### Key-matching ``join_node``

Behaves the same as other ``join_node`` policies, except the values and the metainformation objects are stored in the hash map inside of the port.

### ``split_node``

The split node takes the tuple object and the corresponding metainformation, split the tuple and submit the single values from the tuple to the corresponding ports. 
Metainformation object copy is submitted together with each element into each output port.

Metainfo should not be split since is is unclear what is the relation between elements in the tuple and the metainformation objects stored in the internal list so all of them should
be provided to the successors.

### ``indexer_node``

``indexer_node`` only tags the input value and broadcasts it with the original metainfo to the successors.

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

The current implementation of ``message_metainfo`` class wraps only the list of single message waiters. The class may be extended if necessary to cover additional use-cases.

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

In that case it would be needed to extend the node with the new API returning some descriptor that can be used as the argument to the work waiting function `g.wait` that also should be added.
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

Current implementation does not support ``multifunction_node`` and ``async_node``. 

Possible approaches for implementing such support:

* Hiding metainfo inside of ``output_ports`` to preserve automatic metainfo propagation.
* Merging the input and the ``message_metainfo`` together into some publicly available type ``tagged_input<T>`` and require the user to explicitly specify this type
for multi-output nodes.
* Introducing an extra unspecified type ``node_type::tag_type`` and require the user to accept it as a third argument of the body. 

## Open Questions

The following questions should be addressed before moving this feature to ``supported``:

* Multi-output nodes support should be described and implemented
* Since the buffered item holds an additional reference counter on the associated metainfo, all elements should be retrieved from buffers to allow ``try_put_and_wait`` to exit.
    * Should ``clear()`` member function be added to all of the buffering nodes in the graph (currently supported only in ``write_once_node`` and ``overwrite_node``).
    * Should ``clear()`` member function be added to the non-rejecting ``join_node`` to handle the case when when we don't have the input present on each input port.
    * Concurrent safety guarantees for ``clear()`` should be defined (e.g. is it safe to clear the buffer when other thread tries to insert the item).
* Feedback from the customers should be received
* More multithreaded tests should be implemented for the existing functionality
* The corresponding oneAPI specification update should be done
