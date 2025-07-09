# Class template for Least Recently Used cache with concurrent operations
## Introduction
From [the oneTBB documentation](https://uxlfoundation.github.io/oneTBB/main/reference/concurrent_lru_cache_cls.html):
> A `concurrent_lru_cache` container maps keys to values with the ability to limit the number of
> stored unused values. For each key, there is at most one item stored in the container.

The purpose of this RFC is to summarize the current state of `concurrent_lru_cache` container and
to be a starting point for further discussion about its API and implementation. The feature
has been in experimental stage for a significant time, and its exit criteria is not clear.
This RFC document aims to outline the criterias as well.

TODO: tell about the usefulness of LRU

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
See more details about the API on the corresponding [reference page in the oneTBB documentation](https://uxlfoundation.github.io/oneTBB/main/reference/concurrent_lru_cache_cls.html).

The main idea of the current API is 
The container tracks which items are in use by returning a proxy concurrent_lru_cache::handle object that refers to an item instead of its value. Once there are no handle objects holding reference to an item, it is considered unused.
When no item is found for a given key, the container calls the user-specified value_function_type object to construct a value for the key, and stores that value. The value_function_type object must be thread-safe.
The container stores all the items that are currently in use plus a limited number of unused items. Excessive unused items are erased according to least recently used policy.

### Usage example
The main use case is to provide a cache for expensive-to-compute values (for example, fetching data
from a data base, filesystem lookup, etc), where the cache is automatically managed to keep the
most recently used items available while evicting the least recently used items when the cache size
exceeds a specified limit. For example:
```cpp
std::string fetch_user_data_from_db(int user_id) {
    // Slow remote database call
    return "User data for user " + std::to_string(user_id);
}

using db_cache_t = tbb::concurrent_lru_cache<int, std::string>;

int main() {
    db_cache_t db_cache(fetch_user_data_from_db, 3); // Cache for 3 most recently used user data
     
    std::vector<int> user_ids = {1, 2, 3, 4, 5, 3, 1, 1, 1, 1, 2, 5};
    
    tbb::parallel_for_each(user_ids.begin(), user_ids.end(), [&](int user_id) {
        auto handle = db_cache[user_id];
        std::cout << "Fetched data for user " << user_id << ": " << handle.value() << std::endl;
    });
    return 0;
}
```

### Further API considerations
Potential disadvantages???

1. Do we need position LRU cahe as a specialization for concurrent_hash_map?
2. Do we need position LRU cache as a wrapper for associative containers?
    2.1. Shoud concurrent_lru_cache accept Associative Container as template parameter?
    2.2. Should we reconsider concurrent_lru_cache as concurrent_cache (or concurrent_cache_map/concurrent_evicicing_cache) which would accept Eviction Policy as a template argument?

## Technical details of the implementation
### Current implementation
TODO: tell about how thread safety is implemented
Potential disadvantages???
### Further implementation considerations
## Open Questions
* [Implementation enhacement suggestion](https://github.com/uxlfoundation/oneTBB/issues/941).
* Is the API up to date with the latest C++ standards?
## Exit criteria
The following conditions must be met before this can become fully supported:
1. The API is up to date with the latest C++ standards.
2. oneTBB specification needs to be updated.
3. ????
