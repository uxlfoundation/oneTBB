# Dining philosophers node

Flow-graph provides a facility to serialize the execution of a node to allow some code that would not otherwise be thread safe to be executed in parallel graphs.
Various flow-graph nodes accept a constructor argument that specifies the maximum concurrency the graph can invoke for that particular node.
This allows one to use, in such a node, a function body that is not threadsafe, while ensuring that the graph into which the node is inserted is threadsafe.
For example, in the field of particle physics, an algorithm might reconstruct particle trajectories ("tracks") from energy deposits ("hits") left behind by those particles, and recorded by an experiment's detector.
If the algorithm is not threadsafe, a reasonable node construction could be:

``` c++
using namespace tbb;
flow::graph g;
flow::function_node<Hits, Tracks> track_maker{
  g,
  flow::serial,
  [](Hits const&) -> Tracks { ... }
};
```

where the `flow::serial` argument constrains the flow graph to execute the node body by no more than one thread at a time.

There are cases, however, where specifying a concurrency limit of `flow::serial` is insufficient to guarantee thread-safety of the full graph.
For example, suppose `track_maker` needs exclusive access to some database connection, and another node `cluster_maker` also needs access to the same database:

``` c++
flow::function_node<Hits, Tracks> track_maker{
  g,
  flow::serial,
  [](Hits const&) -> Tracks { auto a = db_unsafe_access(); ... }
};
flow::function_node<Signals, Clusters> cluster_maker{
  g,
  flow::serial,
  [](Signals const&) -> Clusters { auto a = db_unsafe_access(); ... }
};
```

In the above, the function `db_unsafe_access()` returns a handle, providing thread-unsafe access to the database.
To avoid data races, the function bodies of `track_maker` and `cluster_maker` must not execute at the same time.
Achieving with flow graph such serialization between function bodies is nontrivial.
Some options include:

1. placing an explicit lock around the use of the database, resulting in inefficiencies in the execution of the graph,
2. creating explicit edges between `track_maker` and `cluster_maker` even though there is not obvious data dependency between them,
3. creating a token-based system that can limit access to a shared resource.

This RFC proposes an interface that pursues option 3, which we describe in the "Implementation experience" section, below.
Our proposal, however, does not mandate any implementation but suggests an API similar to:

``` c++
auto& db_resource = flow::limited_resource(g, 1); // Only 1 database "token" allowed in the entire graph

flow::function_node<Hits, Tracks> track_maker{
  g,
  db_resource,
  [](Hits const&) -> Tracks { auto a = db_unsafe_access(); ... }
};
flow::function_node<Signals, Clusters> cluster_maker{
  g,
  db_resource,
  [](Signals const&) -> Clusters { auto a = db_unsafe_access(); ... }
};
```

where `db_resource` represents a limited resource to which both `track_maker` and `cluster_maker` require sole access.
Note that if the only reason that the bodies of `track_maker` and `cluster_maker` were thread unsafe was their access to the limited resource indicated by `db_resource` it is no longer necessary to declare that the nodes have concurrency `flow::serial`.
It may be possible to have the node `track_maker` active at the same time, if the nature of `db_resource` were to allow two tokens to be available, and as long as each activation was given a different token.

## Proposal

> [!NOTE]
> Although we focus on the `flow::function_node` class template in this proposal, the concepts discussed here apply to nearly any flow-graph node that accepts a user-provided function body.

Our proposal consists of:
1. Introducing the equivalent of a `flow::limited_resource` class template that, when connected with another node, ensures limited access to the resource it represents.
2. Adding `flow::function_node` constructors that allow the specification of limited resource nodes instead of a `concurrency` value.

``` c++
auto& gpu_resource = flow::limited_resource<GPU>(g, 2);
auto& root_resource = flow::limited_resource<ROOT>(g, 1);

flow::function_node<
  Hits,
  Tracks,
  tuple<GPU, ROOT>> fn{g,
                       make_tuple(gpu_resource, root_resource),
                       [](Hits const&, tuple<GPU, ROOT>) -> Tracks { ... }
                      };

```
### `flow::limited_resource` class template

The `flow::limited_resource` class template heuristically looks like:

> [!NOTE]
> Would it be reasonable to make `limited_resource` a constrained template,
> and to introduce a concept of `resource` that would be used to define the
> constraint?

```c++
template <typename Resource = /* implementation-defined */>
class limited_resource {
public:
  template <typename... Ts>
  limited_resource(flow::graph&, Ts... args_to_Resource_ctor);
};
```

where `Resource` represents a policy class.

#### Default `Resource` policy

For serialized nodes that do not need to access details of the resource, a default policy can be provided:

```c++
flow::limited_resource f{g, 2}; // Is there a use case for needing more than one token but not having access to the resource?
```

#### User-defined `Resource` policies

### Resource handles

Different token types that can carry state.

> A full and detailed description of the proposal with highlighted consequences.
>
> Depending on the kind of the proposal, the description should cover:
>
> - New use cases supported by the extension.
> - The expected performance benefit for a modification.
> - The interface of extensions including class definitions or function
> declarations.
>
## Implementation experience

The image below depicts a system constructed implemented within the https://github.com/knoepfel/meld-serial repository.

![Demonstration of token-based serialization system.](function-serialization.png)


- We can never lose input data

## Future work

> A proposal should clearly outline the alternatives that were considered,
> along with their pros and cons. Each alternative should be clearly separated
> to make discussions easier to follow.
>
> Pay close attention to the following aspects of the library:
> - API and ABI backward compatibility. The library follows semantic versioning
>   so if any of those interfaces are to be broken, the RFC needs to state that
>   explicitly.
> - Performance implications, as performance is one of the main goals of the library.
> - Changes to the build system. While the library's primary building system is
>   CMake, there are some frameworks that may build the library directly from the sources.
> - Dependencies and support matrix: does the proposal bring any new
>   dependencies or affect the supported configurations?
>
> Some other common subsections here are:
> - Discussion: some people like to list all the options first (as separate
>   subsections), and then have a dedicated section with the discussion.
> - List of the proposed API and examples of its usage.
> - Testing aspects.
> - Short explanation and links to the related sub-proposals, if any. Such
>   sub-proposals could be organized as separate standalone RFCs, but this is
>   not mandatory. If the change is insignificant or doesn't make any sense
>   without the original proposal, you can have it in the RFC.
> - Execution plan (next steps), if approved.

## Open Questions

> For new proposals (i.e., those in the `rfcs/proposed` directory), list any
> open questions.
