# Class template for Least Recently Used cache with concurrent operations
## Introduction
From [the oneTBB documentation](https://uxlfoundation.github.io/oneTBB/main/reference/source/containers/concurrent_lru_cache.html):
> A `concurrent_lru_cache` container maps keys to values with the ability to limit the number of
> stored unused values. For each key, there is at most one item stored in the container.

The purpose of this RFC is to summarize the current state of the `concurrent_lru_cache` container and
to be a starting point for further discussion about its API and implementation. The feature
has been in the experimental stage for a significant time, and its exit criteria are not clear.
This RFC document aims to outline the criteria as well.

Least Recently Used (LRU) is one of the most widely used caching policies since it is simple, predictable,
and effective for workloads where recently accessed data is likely to be accessed again soon.

## API
### Current API summary
```cpp
template <typename Key, typename Value, typename ValueFunctionType = Value (*)(Key)>
class concurrent_lru_cache {
public:
    using key_type = Key;
    using value_type = Value;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using reference = value_type&;
    using const_reference = const value_type&;

    using value_function_type = ValueFunctionType;

    class handle {
    public:
        handle();
        handle( handle&& other );

        ~handle();

        handle& operator=( handle&& other );

        operator bool() const;
        value_type& value();
    }; // class handle

    concurrent_lru_cache( value_function_type f, std::size_t number_of_lru_history_items );
    ~concurrent_lru_cache();

    handle operator[]( key_type key );
}; // class concurrent_lru_cache
```
See more details about the API on the corresponding [reference page in the oneTBB documentation](https://uxlfoundation.github.io/oneTBB/main/reference/source/containers/concurrent_lru_cache.html).

The main idea of the current API is that the cache tracks which items are in use. `operator[]` returns a
`handle` object that refers to an item rather than to its value, and an item becomes unused once no `handle`
refers to it. If no item exists for the given key, `operator[]` calls the user-provided `value_function_type`
object to compute the value and stores it. The value function may run concurrently for different keys, so it
must be thread-safe. The cache keeps all items in use plus at most the specified number of unused items, and
it evicts excess unused items in least-recently-used order. Items in use are never evicted.

### Usage example
The main use case is to provide a cache for expensive-to-compute values (for example, database queries
or filesystem lookups), where the cache is automatically managed to keep the
most recently used items available while evicting the least recently used unused items when their number
exceeds a specified limit. For example:
```cpp
std::string fetch_user_data_from_db(int user_id) {
    // Slow remote database call
    return "User data for user " + std::to_string(user_id);
}

using db_cache_t = tbb::concurrent_lru_cache<int, std::string>;

int main() {
    db_cache_t db_cache(fetch_user_data_from_db, 3); // Keep up to 3 unused entries
     
    std::vector<int> user_ids = {1, 2, 3, 4, 5, 3, 1, 1, 1, 1, 2, 5};
    
    tbb::parallel_for_each(user_ids.begin(), user_ids.end(), [&](int user_id) {
        auto handle = db_cache[user_id];
        std::cout << "Fetched data for user " << user_id << ": " << handle.value() << std::endl;
    });
    return 0;
}
```

### Further API considerations
#### Value function versus conventional container interface
Although positioned as a container, `concurrent_lru_cache` behaves more like a memoizer. It can only be populated
by the value function passed to the constructor, which `operator[]` invokes on a miss. There are no `insert`,
`emplace`, `find`, or `erase` operations.

Advantages:
- Lookup and population are a single get-or-compute operation, avoiding a racy find-compute-insert sequence in user code.
- Concurrent requests for the same key share a single computation, preventing duplicate expensive work.
- Eviction is always safe since any evicted value can be recomputed.

Disadvantages:
- The value function is fixed at construction and there is no way to choose a different function afterwards.
- Precomputed values cannot be inserted, stale values cannot be invalidated or removed, and a key cannot be looked up
  without triggering a computation.
- The value function type is a class template parameter, which makes using lambdas inconvenient.
- The interface differs from other oneTBB and standard containers.

#### Handle object
`handle` serves two purposes. It provides access to the value and it marks the item as in use, so it is not evicted.

Current `handle`:
- Pros
  - The reference counter is stored in the item itself, so no extra allocations are needed.
  - Items in use are never evicted, so at most one value per key is alive at a time.
- Cons
  - Custom type that users need to learn.
  - Move-only, so sharing an item requires another `operator[]` call.
  - Releasing the item requires synchronization with the container.
  - The container must outlive all handles.

##### Possible alternatives

`std::shared_ptr` with a custom deleter that returns the item to the LRU history:
- Pros
  - Standard known type.
  - Copies do not involve the container.
  - `std::weak_ptr` can be used.
- Cons
  - A control block is allocated each time an unused item becomes used.
  - The deleter must not throw.
  - The container must outlive all returned pointers unless its state is shared as well.

Container stores `std::shared_ptr` values and returns copies of them, evicting items regardless of whether they are in use:
- Pros
  - Simplest implementation.
  - Capacity limits all items.
  - No notification is needed on release.
- Cons
  - An evicted value may still be alive while a new one is computed for the same key, so several values
    for the same key may coexist.
  - Changes the documented semantics, since items in use could be evicted.

Visitation (for example, `cache.visit(key, f)`, which keeps the item in use only for the duration of `f`) is another option that
removes the handle and its lifetime issues entirely, but it differs significantly from the current API and might
make migration harder for users of the preview feature.

## Open Questions
* What should the capacity apply to? Currently, it limits only unused items, so the number of items in use
  is not bounded.
  * Should it limit all items instead?
  * Should it be adjustable at runtime?
* What should happen when several threads request a key whose value is still being computed?
  Currently, they spin until the value is ready. Should they block or help with the computation
  (see [#941](https://github.com/uxlfoundation/oneTBB/issues/941))?
* Is the API up to date with the latest C++ standards?
* Should the LRU cache be positioned as a specialization of `concurrent_hash_map`?
* Should the LRU cache be positioned as a wrapper for associative containers?
  * Should `concurrent_lru_cache` accept an associative container as a template parameter?
  * Should `concurrent_lru_cache` be reconsidered as `concurrent_cache` (or `concurrent_cache_map`/`concurrent_evicting_cache`)
    that accepts an eviction policy as a template argument?
* Should `concurrent_lru_cache` provide a conventional STL-like container interface in addition
  to the current get-or-compute operation that uses the value function passed during construction?
  * If get-or-compute is kept, should the callable be passed per call, with the constructor-provided function
    being an optional default?
  * What are the semantics of `insert`/`emplace` racing with an in-flight computation for the same key?
  * Should it be possible to retrieve a value only if it is present, without triggering a computation?
* Should `handle` be replaced with `std::shared_ptr` or modernized?
  * Should items in use be protected from eviction, as they are now?

## Exit criteria
The following conditions must be met before `concurrent_lru_cache` can become fully supported:
1. The open questions above are answered and the API is finalized.
2. The documentation is complete and up to date.
