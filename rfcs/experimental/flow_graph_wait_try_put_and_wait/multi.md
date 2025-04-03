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