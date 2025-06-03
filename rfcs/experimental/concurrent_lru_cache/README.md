# Class template for Least Recently Used cache with concurrent operations
## Introduction
To be copied from  reference or link to reference?
## Proposal
### Proposed API
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
### Usage
???
### Technical details
???
## Open Questions
* [Implementation enhacement suggestion](https://github.com/uxlfoundation/oneTBB/issues/941).
* Is the API up to date with the latest C++ standards?
## Exit criteria
The following conditions must be met before this can become fully supported:
1. The API is up to date with the latest C++ standards.
2. oneTBB specification needs to be updated.
3. ????
