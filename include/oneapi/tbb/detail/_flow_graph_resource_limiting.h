/*
    Copyright (c) 2026 UXL Foundation Contributors

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#ifndef __TBB__flow_graph_resource_limiting_H
#define __TBB__flow_graph_resource_limiting_H

#ifndef __TBB_flow_graph_H
#error Do not #include this internal file directly; use public TBB headers instead.
#endif

#include <algorithm>
#include <unordered_map>
#include <forward_list>
#include <list>
#include <functional>
#include <utility>
#include <atomic>
#include <tuple>
#include <queue>
#include <map>
#include <cstdio>
#include <chrono>

#if __TBB_DEBUG_RESOURCE_ACQUISITION
#include "_debug_buffer.h"
#endif

namespace tbb {
namespace detail {
namespace d2 {

template <typename ResourceHandle>
class resource_consumer_base;

// A request id is made up of
// 1) an integer that is either unique across all consumers and requests, or unique across requests from the same consumer.
// 2) optionally, a timestamp that, while global, is not guaranteed to be unique, since it has nanosecond resolution.
//
// A request id is used to both uniquely identify a request when stored by a provider as well as to prioritize requests.
//
// If a global counter is used to set the integer, it alone is sufficient to uniquely identify the request and priority. No two
// requests will have the same integer. Earlier ids can also be given higher priority if global FIFO is important.
//
// If a consumer-local counter is used, then the consumer pointer can be used with it to uniquely identify the request.
// No request will have the same combination of consumer pointer and integer.
// Priority can be based on a combination of the timestamp and the integer, with earlier timestamps being higher priority and the
// integer being used to break ties in timestamp (with lower integers being higher priority). Or, ties can be treated as equal in
// priority.
//
// Pressure can be combined with the counter / timestamp priorities to implement different prioritiziation policies
//

// if defined to 0, usage of local counters will result in non-global FIFO ordering
#ifndef __TBB_USE_TIMESTAMP_IN_REQUEST_ID
#define __TBB_USE_TIMESTAMP_IN_REQUEST_ID 1
#endif

// if defined to 0, this implies a global counter
#ifndef __TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID
#define __TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID 1
#endif


// if defined to 0, the priority-aware provider uses only request_id ordering
#ifndef __TBB_USE_PRESSURE
#define __TBB_USE_PRESSURE 1
#endif

// if defined to 0, the provider may update its pressure-awareness more lazily
#ifndef __TBB_USE_NOTIFY_ON_REPORT_PRESSURE
#define __TBB_USE_NOTIFY_ON_REPORT_PRESSURE 1
#endif

// if defined to 1, enables debug logging of resource acquisition attempts and pressure values to stderr
#ifndef __TBB_DEBUG_RESOURCE_ACQUISITION
#define __TBB_DEBUG_RESOURCE_ACQUISITION 0
#endif
template <typename Input, typename OutputPorts>
class resource_limited_input;

class request_id {
    std::uint64_t m_unique_integer;
#if __TBB_USE_TIMESTAMP_IN_REQUEST_ID
    std::chrono::steady_clock::time_point m_time_point;
#endif
public:

    // defaults to max uint64_t and max time_point
    // the least priority request, so that any request with a valid id will be
    // prioritized over this default constructed id
    request_id()
        : m_unique_integer(std::numeric_limits<std::uint64_t>::max())
#if __TBB_USE_TIMESTAMP_IN_REQUEST_ID
        , m_time_point(std::chrono::steady_clock::time_point::max())
#endif
    {}

    // time stamp is taken at construction
    request_id(const std::uint64_t& unique_integer)
        : m_unique_integer(unique_integer)
#if __TBB_USE_TIMESTAMP_IN_REQUEST_ID
        , m_time_point(std::chrono::steady_clock::now())
#endif
    {
    }

    bool operator<(const request_id& rhs) const {
#if __TBB_USE_TIMESTAMP_IN_REQUEST_ID
        // first by time and then by unique integer to break ties in time stamp
        return m_time_point < rhs.m_time_point
               || (m_time_point == rhs.m_time_point && m_unique_integer < rhs.m_unique_integer);
#else
        return m_unique_integer < rhs.m_unique_integer;
#endif
    }

    bool operator==(const request_id& rhs) const {
        // Equality based on unique integer (identity), not timestamp
        return m_unique_integer == rhs.m_unique_integer;
    }

    struct hash : protected std::hash<std::uint64_t> {
        std::size_t operator()(request_id id) const {
            return std::hash<std::uint64_t>::operator()(id.m_unique_integer);
        }
    };

    struct equal : protected std::equal_to<std::uint64_t> {
        bool operator()(request_id lhs, request_id rhs) const {
            return std::equal_to<std::uint64_t>::operator()(lhs.m_unique_integer, rhs.m_unique_integer);
        }
    };

#if __TBB_DEBUG_RESOURCE_ACQUISITION
    // Accessor for debugging only
    std::uint64_t get_unique_integer() const { return m_unique_integer; }
#endif
}; // class request_id

#if !__TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID
// Global counter for request_id when consumer-local counters are not used
// Using function-local static ensures thread-safe initialization (C++11+)
inline std::atomic<std::uint64_t>& get_global_request_counter() {
    static std::atomic<std::uint64_t> counter{0};
    return counter;
}
#endif

template <typename ResourceHandle>
class resource_handle_optional {
    union {
        ResourceHandle m_resource_handle;
    };
    bool m_has_value;

    template <typename... Args>
    void construct(Args&&... args) {
        ::new(&m_resource_handle) ResourceHandle(std::forward<Args>(args)...);
    }
public:
    struct in_place_t {};

    resource_handle_optional()
        : m_has_value(false)
    {}

    template <typename... Args>
    resource_handle_optional(in_place_t, Args&&... args)
        : m_has_value(true)
    {
        construct(std::forward<Args>(args)...);
    }

    resource_handle_optional(const resource_handle_optional&) = delete;
    resource_handle_optional& operator=(const resource_handle_optional&) = delete;

    resource_handle_optional(resource_handle_optional&& other)
        : m_has_value(other.m_has_value)
    {
        if (m_has_value) {
            construct(std::move(other.m_resource_handle));
        }
    }

    resource_handle_optional& operator=(resource_handle_optional&& other) {
        if (this != &other) {
            if (m_has_value) m_resource_handle.~ResourceHandle();
            m_has_value = other.m_has_value;
            if (other.m_has_value) {
                construct(std::move(other.m_resource_handle));
            }
        }
        return *this;
    }

    ~resource_handle_optional() {
        if (m_has_value) {
            m_resource_handle.~ResourceHandle();
        }
    }

    bool has_value() const { return m_has_value; }

    ResourceHandle& value() {
        __TBB_ASSERT(has_value(), nullptr);
        return m_resource_handle;
    }
}; // class resource_handle_optional

template <typename ResourceHandle>
class resource_provider_base {
public:
    using consumer_type = resource_consumer_base<ResourceHandle>;
    using optional_type = resource_handle_optional<ResourceHandle>;

    virtual void          request(consumer_type&, request_id) = 0;
    virtual optional_type acquire(consumer_type&, request_id) = 0;
    virtual void          report_pressure(consumer_type&, std::size_t) {};
    virtual void          release(consumer_type&, request_id, optional_type&&) = 0;
    virtual ~resource_provider_base() = default;
};

template <typename ResourceHandle>
class resource_consumer_base {
public:
    using provider_type = resource_provider_base<ResourceHandle>;
    virtual void notify(provider_type&, request_id) = 0;
    virtual ~resource_consumer_base() = default;
};

//
// This is a simmple first-come-first-serve provider.
// Notifications and acquires are only limited by available resources.
// Requests that cannot be immediately satisfied are stored until resources become available.
// When resources are released, all pending requests are notified
//
template <typename ResourceHandle>
class resource_limiter : public resource_provider_base<ResourceHandle> {
public:
    using resource_handle_type = ResourceHandle;
    using consumer_type = typename resource_provider_base<ResourceHandle>::consumer_type;
    using optional_type = typename resource_provider_base<ResourceHandle>::optional_type;

    template <typename Handle, typename... Handles>
    resource_limiter(Handle&& handle, Handles&&... handles) {
        emplace_handles(std::forward<Handle>(handle), std::forward<Handles>(handles)...);
    }

    void request(consumer_type& consumer, request_id id) override {
        // TODO: consider using an aggregator instead of mutex
        tbb::spin_mutex::scoped_lock lock(m_mutex);

        if (m_resource_handles.empty()) {
            m_pending.emplace_front(std::piecewise_construct, std::forward_as_tuple(id), std::forward_as_tuple(&consumer));
        } else {
            // Resource available immediately
            lock.release();
            consumer.notify(*this, id);
        }
    }

    optional_type acquire(consumer_type& consumer, request_id id) override {
        tbb::spin_mutex::scoped_lock lock(m_mutex);

        if (m_resource_handles.empty()) {
            // No resources available - return empty optional
            // The consumer must request again to be notified when resources are available
            return optional_type{};
        } else {
            ResourceHandle handle = std::move(m_resource_handles.front());
            m_resource_handles.pop_front();
            return {typename optional_type::in_place_t{}, std::move(handle)};
        }
    }

    void release(consumer_type&, request_id, optional_type&& handle) override {
        __TBB_ASSERT(handle.has_value(), nullptr);
        tbb::spin_mutex::scoped_lock lock(m_mutex);

        m_resource_handles.emplace_front(std::move(handle.value()));
        
        auto pending = std::move(m_pending);
        m_pending.clear();

        lock.release();
        for (auto consumer_dt : pending) {
            consumer_dt.second->notify(*this, consumer_dt.first);
        }
    }

    // nothing to do since there are no priorities used
    void report_pressure(consumer_type&, std::size_t) override {}

    using consumer_data = std::pair<request_id, resource_consumer_base<ResourceHandle>*>;

private:
    template <typename Handle, typename... Handles>
    void emplace_handles(Handle&& handle, Handles&&... handles) {
        m_resource_handles.emplace_front(std::forward<Handle>(handle));
        emplace_handles(std::forward<Handles>(handles)...);
    }

    void emplace_handles() {}

    tbb::spin_mutex m_mutex;
    std::forward_list<ResourceHandle> m_resource_handles;
    std::forward_list<consumer_data>  m_pending;
}; // class resource_limiter


// A priority_aware_resource_limiter prioritizes notifications and acquisitions based on:
//
// 1) the current pressure on the consumer, with higher pressure being prioritized
// 2) and then the request id, with lower request ids being prioritized
// 
// The pressure is reported by the consumer and priority is based on the last
// reported pressure. So if a request is made and is in m_pending or m_notified, 
// its priority may change over time as the associated consumer's pressure is adjusted.
//
// Configuration options:
// __TBB_USE_TIMESTAMP_IN_REQUEST_ID: when set to 1 a timestamp is included in the request id. 
//     The timestamp is used for comparisons and ordering. Combined with the local counter option, 
//     this allows for requests to be prioritized based on when they were made, with ties broken by 
//     the unique integer.
// __TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID: when set to 1, the unique integer in the request 
//     id is based on a counter local to the consumer. Otherwise, the unique integer is based on a global counter.
// __TBB_USE_NOTIFY_ON_REPORT_PRESSURE: when set to 1, notifications are triggered when
//     pressure is reported, which allows for changes in priority to be reflected in the
//     notified requests as soon as possible. But this adds overheads.
//
// The defaults are to include a timestamp in the request id, to use a consumer-local counter, 
// and to notify on report pressure.

template <typename ResourceHandle>
class priority_aware_resource_limiter : public resource_provider_base<ResourceHandle> {
public:
    using resource_handle_type = ResourceHandle;
    using consumer_type = typename resource_provider_base<ResourceHandle>::consumer_type;
    using optional_type = typename resource_provider_base<ResourceHandle>::optional_type;

    template <typename Handle, typename... Handles>
    priority_aware_resource_limiter(Handle&& handle, Handles&&... handles) {
        emplace_handles(std::forward<Handle>(handle), std::forward<Handles>(handles)...);
    }

    void request(consumer_type& consumer, request_id id) override {
        tbb::spin_mutex::scoped_lock lock(m_mutex);
        size_t num_handles = m_resource_handles.size();
        size_t num_notified = m_notified.size();

#if __TBB_DEBUG_RESOURCE_ACQUISITION
        std::size_t my_pressure = m_consumer_pressure[&consumer].first;
        TBB_DEBUG_LOG("[REQUEST] consumer=%p id=%llu pressure=%zu num_handles=%zu num_notified=%zu num_pending=%zu\n",
                    (void*)&consumer, (unsigned long long)id.get_unique_integer(), my_pressure,
                    num_handles, num_notified, m_pending.size());
#endif

        increment_ref_count_for_consumer(consumer);

        if (num_handles == 0) {
            // no resource available right now, add to pending list
            // the pending list is unordered
            m_pending.emplace_back(std::make_unique<consumer_data>(id, &consumer, m_consumer_pressure[&consumer].first));
#if __TBB_DEBUG_RESOURCE_ACQUISITION
            TBB_DEBUG_LOG("[REQUEST] consumer=%p id=%llu -> PENDING (no handles available)\n",
                        (void*)&consumer, (unsigned long long)id.get_unique_integer());
#endif
            return;
        }

        consumer_data new_consumer_data{id, &consumer, m_consumer_pressure[&consumer].first};
        if (num_notified < num_handles) {
            // there are more resources available than notified requests, so we can
            // notify this request without comparing priorities
            m_notified.emplace_back(std::make_unique<consumer_data>(std::move(new_consumer_data)));
            m_needs_recompute = true;
#if __TBB_DEBUG_RESOURCE_ACQUISITION
            TBB_DEBUG_LOG("[REQUEST] consumer=%p id=%llu -> NOTIFY (no priority check needed)\n",
                        (void*)&consumer, (unsigned long long)id.get_unique_integer());
#endif
            lock.release();
            consumer.notify(*this, id);
        } else {
            // need to compare priority with the lowest priority notified request to determine
            // whether to notify this request or add it to pending list.
            ensure_priorities_current();
            auto& threshold = m_notified[num_handles - 1];
            if (new_consumer_data < *threshold) {
                m_pending.emplace_back(std::make_unique<consumer_data>(id, &consumer, m_consumer_pressure[&consumer].first));
#if __TBB_DEBUG_RESOURCE_ACQUISITION
                TBB_DEBUG_LOG("[REQUEST] consumer=%p id=%llu -> PENDING (priority too low)\n",
                            (void*)&consumer, (unsigned long long)id.get_unique_integer());
#endif
            } else {
                // there may be more notifications than available resources, but that's ok.
                // they will compete for the resources when they attempt acquisition,
                // with the same priority criteria applied (using updated priorities)
                m_notified.emplace_back(std::make_unique<consumer_data>(std::move(new_consumer_data)));
                m_needs_recompute = true;
#if __TBB_DEBUG_RESOURCE_ACQUISITION
                TBB_DEBUG_LOG("[REQUEST] consumer=%p id=%llu -> NOTIFY (priority sufficient)\n",
                            (void*)&consumer, (unsigned long long)id.get_unique_integer());
#endif
                lock.release();
                consumer.notify(*this, id);
            }
        }
    }

    optional_type acquire(consumer_type& consumer, request_id id) override {
        tbb::spin_mutex::scoped_lock lock(m_mutex);
        consumer_data acquisition_data{id, &consumer, m_consumer_pressure[&consumer].first};

        size_t num_handles = m_resource_handles.size();

#if __TBB_DEBUG_RESOURCE_ACQUISITION
        std::size_t my_pressure = m_consumer_pressure[&consumer].first;
        TBB_DEBUG_LOG("[ACQUIRE] consumer=%p id=%llu pressure=%zu num_handles=%zu num_notified=%zu\n",
                    (void*)&consumer, (unsigned long long)id.get_unique_integer(), my_pressure,
                    num_handles, m_notified.size());
#endif

        if (num_handles == 0) {
            // no resource available right now
            remove_from_notified_list(acquisition_data);
#if __TBB_DEBUG_RESOURCE_ACQUISITION
            TBB_DEBUG_LOG("[ACQUIRE] consumer=%p id=%llu DENIED: no_handles\n",
                        (void*)&consumer, (unsigned long long)id.get_unique_integer());
#endif
            return optional_type{};
        }

        if (m_notified.size() < num_handles) {
            // there are more resources available than notified requests, so if this
            // represents a notified request, it can be given without comparing priorities
            if (remove_from_notified_list(acquisition_data)) {
                ResourceHandle handle = std::move(m_resource_handles.front());
                m_resource_handles.pop_front();
                decrement_ref_count_for_consumer(consumer);
#if __TBB_DEBUG_RESOURCE_ACQUISITION
                TBB_DEBUG_LOG("[ACQUIRE] consumer=%p id=%llu SUCCESS: no_priority_check\n",
                            (void*)&consumer, (unsigned long long)id.get_unique_integer());
#endif
                return {typename optional_type::in_place_t{}, std::move(handle)};
            } else {
                // not in notified list - should not happen
#if __TBB_DEBUG_RESOURCE_ACQUISITION
                TBB_DEBUG_LOG("[ACQUIRE] consumer=%p id=%llu ERROR: not_in_notified_list\n",
                            (void*)&consumer, (unsigned long long)id.get_unique_integer());
#endif
                return optional_type{};
            }
        }

        ensure_priorities_current();
        auto& threshold = m_notified[num_handles - 1];
        bool should_not_acquire = acquisition_data < *threshold;

#if __TBB_DEBUG_RESOURCE_ACQUISITION
        std::size_t threshold_pressure = *threshold->pressure;
        TBB_DEBUG_LOG("[ACQUIRE] consumer=%p id=%llu priority_check: my_pressure=%zu threshold_pressure=%zu should_deny=%d\n",
                    (void*)&consumer, (unsigned long long)id.get_unique_integer(), my_pressure,
                    threshold_pressure, should_not_acquire);
#endif

        // remove from notified list regardless of whether acquisition will be allowed or not,
        // the consumer must make a new request if it wants to try again
        if (remove_from_notified_list(acquisition_data)) {
            decrement_ref_count_for_consumer(consumer);
            if (should_not_acquire) {
                // priority too low - deny acquisition
#if __TBB_DEBUG_RESOURCE_ACQUISITION
                TBB_DEBUG_LOG("[ACQUIRE] consumer=%p id=%llu DENIED: priority_too_low\n",
                            (void*)&consumer, (unsigned long long)id.get_unique_integer());
#endif
                return optional_type{};
            }
            // priority high enough - allow acquisition
            ResourceHandle handle = std::move(m_resource_handles.front());
            m_resource_handles.pop_front();
#if __TBB_DEBUG_RESOURCE_ACQUISITION
            TBB_DEBUG_LOG("[ACQUIRE] consumer=%p id=%llu SUCCESS: priority_ok\n",
                        (void*)&consumer, (unsigned long long)id.get_unique_integer());
#endif
            return {typename optional_type::in_place_t{}, std::move(handle)};
        } else {
            // not in notified list - should not happen
#if __TBB_DEBUG_RESOURCE_ACQUISITION
            TBB_DEBUG_LOG("[ACQUIRE] consumer=%p id=%llu ERROR: not_in_notified_list_after_priority_check\n",
                        (void*)&consumer, (unsigned long long)id.get_unique_integer());
#endif
            return optional_type{};
        }
    }

    void release(consumer_type& consumer, request_id id, optional_type&& handle) override {
        __TBB_ASSERT(handle.has_value(), nullptr);
        tbb::spin_mutex::scoped_lock lock(m_mutex);
        m_resource_handles.emplace_front(std::move(handle.value()));
#if __TBB_DEBUG_RESOURCE_ACQUISITION
        TBB_DEBUG_LOG("[RELEASE] consumer=%p id=%llu num_handles=%zu num_pending=%zu\n",
                    (void*)&consumer, (unsigned long long)id.get_unique_integer(),
                    m_resource_handles.size(), m_pending.size());
#endif
        notify_pending(lock); // lock may be released
    }

#if __TBB_USE_PRESSURE
    void report_pressure(consumer_type& c, std::size_t pressure) override {
        tbb::spin_mutex::scoped_lock lock(m_mutex);
        m_consumer_pressure[&c].first = pressure;  // Only update pressure, keep ref count
    #if __TBB_USE_NOTIFY_ON_REPORT_PRESSURE
        notify_pending(lock); // lock may be released
    #endif // __TBB_USE_NOTIFY_ON_REPORT_PRESSURE
    }
#endif // __TBB_USE_PRESSURE

private:

    struct consumer_data {
        request_id id;
        resource_consumer_base<ResourceHandle>* consumer_ptr;
        const std::size_t *pressure;

        consumer_data(request_id id, resource_consumer_base<ResourceHandle>* consumer_ptr, const std::size_t& pressure_ref)
            : id(id), consumer_ptr(consumer_ptr), pressure(&pressure_ref)
        {}

        bool operator<(const consumer_data& rhs) const {
            // a consumer_data has less prioritiy if it has lesser pressure, or if
            // pressures are equal, it has less priority if it has a greater id (lower id is higher priority)
            if (*pressure == *rhs.pressure) {
                return rhs.id < id;  // Earlier id (smaller) has higher priority for FIFO
            }
            return *pressure < *rhs.pressure;
        }

        bool operator==(const consumer_data& rhs) const {
            // Equality based on identity (id and consumer_ptr), not pressure
            return id == rhs.id && consumer_ptr == rhs.consumer_ptr;
        }
    };


    template <typename Handle, typename... Handles>
    void emplace_handles(Handle&& handle, Handles&&... handles) {
        m_resource_handles.emplace_front(std::forward<Handle>(handle));
        emplace_handles(std::forward<Handles>(handles)...);
    }

    void emplace_handles() {}

    // called under the lock
    void notify_pending(tbb::spin_mutex::scoped_lock& lock) {
        size_t num_handles = m_resource_handles.size();
        size_t num_pending = m_pending.size();
        size_t num_notified = m_notified.size();

#if __TBB_DEBUG_RESOURCE_ACQUISITION
        TBB_DEBUG_LOG("[NOTIFY_PENDING] num_handles=%zu num_pending=%zu num_notified=%zu\n",
                    num_handles, num_pending, num_notified);
#endif

        if (m_pending.empty() || num_handles == 0) {
            // no resources or no pending requests, nothing to do
#if __TBB_DEBUG_RESOURCE_ACQUISITION
            TBB_DEBUG_LOG("[NOTIFY_PENDING] -> SKIP (pending=%zu handles=%zu)\n",
                        num_pending, num_handles);
#endif
            return;
        }

        // Sort pending by priority (highest first)
        std::sort(m_pending.begin(), m_pending.end(),
                  [](const std::unique_ptr<consumer_data>& a, const std::unique_ptr<consumer_data>& b) {
                      return *b < *a;
                  });

        // Collect items to notify while holding lock
        std::vector<std::pair<consumer_type*, request_id>> to_notify;
        auto it = m_pending.begin();

        // This just checks if there is room for more notifications
        if (m_notified.size() < num_handles) {
            // Fast path: more resources than notified, so notify up to num_handles
            while (it != m_pending.end() && m_notified.size() < num_handles) {
                to_notify.emplace_back((*it)->consumer_ptr, (*it)->id);
                m_notified.push_back(std::move(*it));
                it = m_pending.erase(it);
            }
        }

        // This checks if the remaining pending requests have higher priority than the notified
        // requests
        if (m_notified.size() >= num_handles && it != m_pending.end()) {
            // Sort notified to find threshold
            std::sort(m_notified.begin(), m_notified.end(),
                      [](const std::unique_ptr<consumer_data>& a, const std::unique_ptr<consumer_data>& b) {
                          return *b < *a;
                      });

            // Check remaining pending items against threshold
            while (it != m_pending.end() && !(**it < *m_notified[num_handles - 1])) {
                to_notify.emplace_back((*it)->consumer_ptr, (*it)->id);
                m_notified.push_back(std::move(*it));
                it = m_pending.erase(it);
            }
        }

        m_needs_recompute = false;  // Already sorted

#if __TBB_DEBUG_RESOURCE_ACQUISITION
        TBB_DEBUG_LOG("[NOTIFY_PENDING] -> Notifying %zu consumers\n", to_notify.size());
        for (auto& [consumer_ptr, id] : to_notify) {
            TBB_DEBUG_LOG("[NOTIFY_PENDING] -> Will notify consumer=%p id=%llu\n",
                        (void*)consumer_ptr, (unsigned long long)id.get_unique_integer());
        }
#endif

        // Release lock once, then notify all
        lock.release();
        for (auto& [consumer_ptr, id] : to_notify) {
#if __TBB_DEBUG_RESOURCE_ACQUISITION
            TBB_DEBUG_LOG("[NOTIFY_PENDING] -> Calling notify() for consumer=%p id=%llu\n",
                        (void*)consumer_ptr, (unsigned long long)id.get_unique_integer());
#endif
            consumer_ptr->notify(*this, id);
        }
    }

    // Must be called under lock
    void ensure_priorities_current() {
        if (m_needs_recompute) {
            size_t num_handles = m_resource_handles.size();
            // Only partition if we have more notified than handles
            if (m_notified.size() > num_handles) {
                std::nth_element(m_notified.begin(),
                                 m_notified.begin() + num_handles,
                                 m_notified.end(),
                                 [](const std::unique_ptr<consumer_data>& a, const std::unique_ptr<consumer_data>& b) {
                                     return *b < *a;  // Higher priority first
                                 });
            }
            m_needs_recompute = false;
        }
    }

    // this is called under the lock
    void increment_ref_count_for_consumer(consumer_type& consumer) {
        if (m_consumer_pressure.find(&consumer) == m_consumer_pressure.end()) {
            m_consumer_pressure[&consumer] = {0, 1};
        } else {
            m_consumer_pressure[&consumer].second++;
        }
    }

    // this is called under the lock
    void decrement_ref_count_for_consumer(consumer_type& consumer) {
        auto it = m_consumer_pressure.find(&consumer);
        __TBB_ASSERT(it != m_consumer_pressure.end(), "Consumer not found in pressure map");
        if (--(it->second).second == 0) {
            m_consumer_pressure.erase(it);
        }
    }

    // this is called under the lock
    bool remove_from_notified_list(consumer_data& consumer_dt) {
        auto it = std::find_if(m_notified.begin(), m_notified.end(),
                            [&](const std::unique_ptr<consumer_data>& data) { 
                                return *data == consumer_dt; 
                            });
        
        if (it != m_notified.end()) {
            m_notified.erase(it);
            return true;
        }
        return false;
    }

    tbb::spin_mutex m_mutex;
    bool m_needs_recompute = false;
    std::list<ResourceHandle> m_resource_handles;
    std::map<resource_consumer_base<ResourceHandle>*, std::pair<std::size_t, std::size_t>> m_consumer_pressure;
    std::vector<std::unique_ptr<consumer_data>> m_pending;
    std::vector<std::unique_ptr<consumer_data>> m_notified;
}; // class priority_aware_resource_limiter

template <typename Input, typename OutputPorts>
class resource_limited_body {
    graph& m_graph;
public:
    virtual void                   operator()(const Input& input, OutputPorts& ports) = 0;
    virtual void                   notify(request_id id) = 0;
    virtual resource_limited_body* clone() = 0;
    virtual void*                  get_body_ptr() = 0;
    virtual void                   update_input_ptr(resource_limited_input<Input, OutputPorts>* ptr) = 0;
    virtual ~resource_limited_body() = default;

    resource_limited_body(graph& g) : m_graph(g) {}

    virtual void note_try_put(const Input&) {}

    graph& graph_reference() { return m_graph; }
};

template <typename Input, typename OutputPorts, typename ResourceProvider>
class resource_consumer : public resource_consumer_base<typename ResourceProvider::resource_handle_type> {
public:
    using resource_handle_type = typename ResourceProvider::resource_handle_type;
    using resource_limited_body_type = resource_limited_body<Input, OutputPorts>;
    using optional_type = typename ResourceProvider::optional_type;

    resource_consumer(ResourceProvider& provider, resource_limited_body_type* body_ptr)
        : m_resource_provider(provider)
        , m_body_ptr(body_ptr)
    {}

    void request_from_provider(request_id id) {
#if __TBB_DEBUG_RESOURCE_ACQUISITION
        // Print consumer-to-body mapping on first request (id=1) for each consumer
        if (id.get_unique_integer() == 1) {
            TBB_DEBUG_LOG("[CONSUMER_TO_BODY_MAP] consumer=%p body=%p\n",
                        (void*)this, (void*)m_body_ptr);
        }
#endif
        m_resource_provider.request(*this, id);
    }


    optional_type acquire_from_provider(request_id id) {
        return m_resource_provider.acquire(*this, id);
    }

    void release_to_provider(request_id id, optional_type&& handle) {
        m_resource_provider.release(*this, id, std::move(handle));
    }

    void report_pressure_to_provider(std::size_t pressure) {
        m_resource_provider.report_pressure(*this, pressure);
    }

    void notify(resource_provider_base<resource_handle_type>& provider, request_id id) override {
        __TBB_ASSERT(&provider == &m_resource_provider, "Provider-consumer mismatch");
        m_body_ptr->notify(id);
        tbb::detail::suppress_unused_warning(provider);
    }

    void set_body_ptr(resource_limited_body_type* body_ptr) {
        m_body_ptr = body_ptr;
    }

private:
    ResourceProvider&           m_resource_provider;
    resource_limited_body_type* m_body_ptr;
};

template <typename Input, typename OutputPorts, typename HandlesTuple>
struct request_data {
    Input                    input_message;
    OutputPorts&             output_ports;
    std::atomic<std::size_t> notify_counter;
    HandlesTuple             handles;

    request_data(const Input& input, OutputPorts& ports)
        : input_message(input)
        , output_ports(ports)
        , notify_counter(std::tuple_size<HandlesTuple>::value + 1)
    {}
};

template <std::size_t Index, std::size_t MaxIndex>
struct request_resources_helper {
    template <typename ConsumerTuple>
    static void run(ConsumerTuple& consumers, request_id id) {
        std::get<Index>(consumers).request_from_provider(id);
        request_resources_helper<Index + 1, MaxIndex>::run(consumers, id);
    }
};

template <std::size_t MaxIndex>
struct request_resources_helper<MaxIndex, MaxIndex> {
    template <typename ConsumerTuple>
    static void run(ConsumerTuple&, request_id) {}
};

template <std::size_t Index>
struct release_resources_helper {
    template <typename ConsumerTuple, typename RequestData>
    static void run(ConsumerTuple& consumers, request_id id, RequestData& req_data) {
        std::get<Index - 1>(consumers).release_to_provider(id, std::move(std::get<Index - 1>(req_data.handles)));
        std::get<Index - 1>(req_data.handles) = {};
        release_resources_helper<Index - 1>::run(consumers, id, req_data);
    }
};

template <>
struct release_resources_helper<0> {
    template <typename ConsumerTuple, typename RequestData>
    static void run(ConsumerTuple&, request_id, RequestData&) {}
};

// Helper that releases acquired resources AND re-requests them (used when acquisition fails)
template <std::size_t Index>
struct release_and_rerequest_resources_helper {
    template <typename ConsumerTuple, typename RequestData>
    static void run(ConsumerTuple& consumers, request_id id, RequestData& req_data) {
        // Release the resource at Index-1
        std::get<Index - 1>(consumers).release_to_provider(id, std::move(std::get<Index - 1>(req_data.handles)));
        std::get<Index - 1>(req_data.handles) = {};
        // Increment notify_counter because we're about to re-request (expecting a new notification)
        ++req_data.notify_counter;
        // Re-request the resource at Index-1
        std::get<Index - 1>(consumers).request_from_provider(id);
        // Recursively handle lower indices
        release_and_rerequest_resources_helper<Index - 1>::run(consumers, id, req_data);
    }
};

template <>
struct release_and_rerequest_resources_helper<0> {
    template <typename ConsumerTuple, typename RequestData>
    static void run(ConsumerTuple&, request_id, RequestData&) {}
};

template <std::size_t Index>
struct set_body_ptr_helper {
    template <typename ConsumerTuple, typename Body>
    static void run(ConsumerTuple& consumers, Body* body_ptr) {
        std::get<Index - 1>(consumers).set_body_ptr(body_ptr);
        set_body_ptr_helper<Index - 1>::run(consumers, body_ptr);
    }
};

template <>
struct set_body_ptr_helper<0> {
    template <typename ConsumerTuple, typename Body>
    static void run(ConsumerTuple&, Body*) {}
};

template <std::size_t Index, std::size_t MaxIndex>
struct acquire_resources_helper {
    template <typename Body, typename ConsumerTuple, typename RequestData>
    static bool run(Body* body_ptr, ConsumerTuple& consumers, request_id id, RequestData& req_data) {
        __TBB_ASSERT(req_data.notify_counter == 1, "Incorrect notify counter");
        ++req_data.notify_counter; // Local counter in case the resource is denied
        auto handle_optional = std::get<Index>(consumers).acquire_from_provider(id);
        if (handle_optional.has_value()) {
            // Successfully acquired resource - save the handle and proceed to the next resource
            --req_data.notify_counter;
            std::get<Index>(req_data.handles) = std::move(handle_optional);
            return acquire_resources_helper<Index + 1, MaxIndex>::run(body_ptr, consumers, id, req_data);
        } else {
            __TBB_ASSERT(req_data.notify_counter >= 1, "Incorrect notify counter");
            // One of the resources denied the request at Index
            // Undo the optimistic increment from line 709 since acquisition failed
            --req_data.notify_counter;
            // Release previously acquired resources (0 through Index-1) and re-request them
            release_and_rerequest_resources_helper<Index>::run(consumers, id, req_data);
            // Increment notify_counter because we're about to re-request (expecting a new notification)
            ++req_data.notify_counter;
            // Re-request the resource that failed at Index
            std::get<Index>(consumers).request_from_provider(id);
            body_ptr->release_self_ref(id, req_data); // release the self-reference held at the beginning of resource acquisition
            return false;
        }
    }
};

template <std::size_t MaxIndex>
struct acquire_resources_helper<MaxIndex, MaxIndex> {
    template <typename Body, typename ConsumerTuple, typename RequestData>
    static bool run(Body*, ConsumerTuple&, request_id, RequestData& req_data) {
        --req_data.notify_counter;
        return true;
    }
};

template <std::size_t Index, std::size_t MaxIndex>
struct report_pressure_helper {
    template <typename ConsumerTuple>
    static void run(ConsumerTuple& consumers, std::size_t pressure) {
        std::get<Index>(consumers).report_pressure_to_provider(pressure);
        report_pressure_helper<Index + 1, MaxIndex>::run(consumers, pressure);
    }
};

template <std::size_t MaxIndex>
struct report_pressure_helper<MaxIndex, MaxIndex> {
    template <typename ConsumerTuple>
    static void run(ConsumerTuple&, std::size_t) {}
};

template <typename BodyLeaf, typename RequestDataType>
class try_acquire_resources_and_execute_task : public graph_task {
    BodyLeaf*        m_body;
    request_id       m_id;
    RequestDataType& m_request_data;

public:
    try_acquire_resources_and_execute_task(graph& g, d1::small_object_allocator& allocator, BodyLeaf* body_leaf,
                                           request_id id, RequestDataType& request_data)
        : graph_task(g, allocator, no_priority)
        , m_body(body_leaf)
        , m_id(id)
        , m_request_data(request_data)
    {
        __TBB_ASSERT(body_leaf != nullptr, nullptr);
    }

    d1::task* execute(d1::execution_data& ed) override {
#if __TBB_DEBUG_RESOURCE_ACQUISITION
        TBB_DEBUG_LOG("[TASK_EXECUTE_RUN] body=%p id=%llu - Executing try_acquire_resources_and_execute\n",
                    (void*)m_body, (unsigned long long)m_id.get_unique_integer());
#endif
        m_body->try_acquire_resources_and_execute(m_id, m_request_data);
#if __TBB_DEBUG_RESOURCE_ACQUISITION
        TBB_DEBUG_LOG("[TASK_EXECUTE_DONE] body=%p id=%llu - Finished try_acquire_resources_and_execute\n",
                    (void*)m_body, (unsigned long long)m_id.get_unique_integer());
#endif
        graph_task::template finalize<try_acquire_resources_and_execute_task>(ed);
        return nullptr;
    }

    d1::task* cancel(d1::execution_data& ed) override {
        m_body->remove_request(m_id);
        graph_task::template finalize<try_acquire_resources_and_execute_task>(ed);
        return nullptr;
    }
};

template <typename Input, typename OutputPorts, typename Body, typename... ResourceProviders>
class resource_limited_body_leaf
    : public resource_limited_body<Input, OutputPorts>
{
    using handles_tuple_type = std::tuple<typename ResourceProviders::optional_type...>;
    using consumers_tuple_type = std::tuple<resource_consumer<Input, OutputPorts, ResourceProviders>...>;
    using request_data_type = request_data<Input, OutputPorts, handles_tuple_type>;
    // TODO: should concurrent container be used instead?
    using requests_map_type = std::unordered_map<request_id, request_data_type, request_id::hash, request_id::equal>;

    tbb::spin_mutex      m_mutex;
    requests_map_type    m_requests;
    consumers_tuple_type m_consumers;
    Body                 m_body;
#if __TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID
    std::uint64_t        m_counter;
#endif
    std::atomic<std::size_t> m_pressure;
    resource_limited_input<Input, OutputPorts>* m_input_ptr;

    template <typename ConsumersTuple>
    resource_limited_body_leaf(graph& g, ConsumersTuple&& consumers_tuple, const Body& body,
                              resource_limited_input<Input, OutputPorts>* input_ptr)
        : resource_limited_body<Input, OutputPorts>(g)
        , m_consumers(std::forward<ConsumersTuple>(consumers_tuple))
        , m_body(body)
#if __TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID
        , m_counter(0)
#endif
        , m_input_ptr(input_ptr)
    {}

public:
    resource_limited_body_leaf(graph& g, std::tuple<ResourceProviders&...> resource_providers, const Body& body,
                              resource_limited_input<Input, OutputPorts>* input_ptr)
        : resource_limited_body_leaf(g, get_consumers_tuple(resource_providers), body, input_ptr)
    {}

    consumers_tuple_type get_consumers_tuple(std::tuple<ResourceProviders&...> resource_providers) {
        return get_consumers_tuple_impl(resource_providers, tbb::detail::make_index_sequence<sizeof...(ResourceProviders)>());
    }

    template <std::size_t... Idx>
    consumers_tuple_type get_consumers_tuple_impl(std::tuple<ResourceProviders&...> resource_providers,
                                                  tbb::detail::index_sequence<Idx...>)
    {
        return consumers_tuple_type({std::get<Idx>(resource_providers), this}...);
    }

    void operator()(const Input& input, OutputPorts& ports) override {
#if __TBB_DEBUG_RESOURCE_ACQUISITION
        TBB_DEBUG_LOG("[TASK_INITIAL] body=%p input=%d - Initial task spawned, forming request\n",
                    (void*)this, input);
#endif
        auto& res = form_request(input, ports);
        report_pressure(m_pressure.load(std::memory_order_relaxed));
#if __TBB_DEBUG_RESOURCE_ACQUISITION
        TBB_DEBUG_LOG("[TASK_INITIAL] body=%p input=%d id=%llu - Requesting resources\n",
                    (void*)this, input, (unsigned long long)res.first.get_unique_integer());
#endif
        request_resources(res.first);
        release_self_ref(res.first, res.second);
#if __TBB_DEBUG_RESOURCE_ACQUISITION
        TBB_DEBUG_LOG("[TASK_INITIAL] body=%p input=%d id=%llu - Initial task complete\n",
                    (void*)this, input, (unsigned long long)res.first.get_unique_integer());
#endif
    }

    typename requests_map_type::reference form_request(const Input& input, OutputPorts& ports) {
        tbb::spin_mutex::scoped_lock lock(m_mutex);
#if __TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID
        request_id id{++m_counter};
#else
        request_id id{++get_global_request_counter()};
#endif
        auto res = m_requests.emplace(std::piecewise_construct,
                                      std::forward_as_tuple(id),
                                      std::forward_as_tuple(input, ports));
        this->graph_reference().reserve_wait();
        __TBB_ASSERT(res.second, "Duplicated requests in the map");
        return *res.first;
    }

    void request_resources(request_id id) {
        request_resources_helper<0, sizeof...(ResourceProviders)>::run(m_consumers, id);
    }

    void release_self_ref(request_id id, request_data_type& req_data) {
        std::size_t prev_value = req_data.notify_counter--;
        __TBB_ASSERT(prev_value != 0, "Overflow detected");
        if (prev_value == 1) {
            try_acquire_resources_and_execute(id, req_data);
        }
    }

    bool try_acquire_resources(request_id id, request_data_type& req_data) {
        // Increment the counter to avoid another resource reacquisition by notify()
        // while current acquisition is in progress
        std::size_t prev_value = req_data.notify_counter++;
        __TBB_ASSERT(prev_value == 0, "Incorrect notify counter before acquisition");
        tbb::detail::suppress_unused_warning(prev_value);
        return acquire_resources_helper<0, sizeof...(ResourceProviders)>::run(this, m_consumers, id, req_data);
    }

    void try_acquire_resources_and_execute(request_id id, request_data_type& req_data) {
#if __TBB_DEBUG_RESOURCE_ACQUISITION
        TBB_DEBUG_LOG("[TRY_ACQUIRE] body=%p id=%llu - Attempting to acquire resources\n",
                    (void*)this, (unsigned long long)id.get_unique_integer());
#endif
        if (try_acquire_resources(id, req_data)) {
            // All resources acquired - decrement pressure and report immediately
            std::size_t prev_pressure = m_pressure.fetch_sub(1, std::memory_order_relaxed);
            report_pressure(prev_pressure - 1);
#if __TBB_DEBUG_RESOURCE_ACQUISITION
            TBB_DEBUG_LOG("[ACQUIRED_ALL] body=%p id=%llu - All resources acquired, pressure decremented to %zu, calling user body\n",
                        (void*)this, (unsigned long long)id.get_unique_integer(), prev_pressure - 1);
#endif
            // Access to all resources is granted
            try_call([&] {
                call_body(req_data.input_message, req_data.output_ports, req_data.handles);
            }).on_completion([&] {
#if __TBB_DEBUG_RESOURCE_ACQUISITION
                TBB_DEBUG_LOG("[BODY_COMPLETE] body=%p id=%llu - User body complete, releasing resources\n",
                            (void*)this, (unsigned long long)id.get_unique_integer());
#endif
                release_resources(id, req_data);
                // delay release of concurrency slot until after body completes
                release_concurrency_and_spawn_next(req_data.input_message);
                remove_request(id);
            });
        }
#if __TBB_DEBUG_RESOURCE_ACQUISITION
        else {
            TBB_DEBUG_LOG("[ACQUIRE_FAILED] body=%p id=%llu - Resource acquisition failed, will retry\n",
                        (void*)this, (unsigned long long)id.get_unique_integer());
        }
#endif
    } 

    void release_concurrency_and_spawn_next(const Input& input_msg) {
        if (m_input_ptr) {
            // Call back to the input layer to release concurrency slot and get next task
            // Only do this if concurrency is limited (not unlimited)
            graph_task* next_task = m_input_ptr->release_concurrency_slot(input_msg);
            if (next_task && next_task != SUCCESSFULLY_ENQUEUED) {
                spawn_in_graph_arena(this->graph_reference(), *next_task);
            }
        }
    }

    void release_resources(request_id id, request_data_type& req_data) {
        // Pressure was already decremented after successful acquisition
        release_resources_helper<sizeof...(ResourceProviders)>::run(m_consumers, id, req_data);
    }

    void remove_request(request_id id) {
        tbb::spin_mutex::scoped_lock lock(m_mutex);
        std::size_t num_removed = m_requests.erase(id);
        this->graph_reference().release_wait();
        __TBB_ASSERT(num_removed == 1, "Removing unregistered request");
        tbb::detail::suppress_unused_warning(num_removed);
    }

    void note_try_put(const Input& input) override {
        // always increment but only report pressure if there are pending requests
        auto v = m_pressure.fetch_add(1, std::memory_order_relaxed) + 1;
#if __TBB_DEBUG_RESOURCE_ACQUISITION
        TBB_DEBUG_LOG("[NOTE_TRY_PUT] consumer=%p input=%d pressure=%zu pending_requests=%zu\n",
                    (void*)this, input, (std::size_t)v, m_requests.size());
#endif
        tbb::spin_mutex::scoped_lock lock(m_mutex);
        if (!m_requests.empty()) {
            lock.release();
            report_pressure(v);
        }
    }

    void notify(request_id id) override {
        tbb::spin_mutex::scoped_lock lock(m_mutex);
        auto res = m_requests.find(id);
        __TBB_ASSERT(res != m_requests.end(), "Cannot find request for notification");
        request_data_type& data = res->second;
        lock.release();

        std::size_t prev_value = data.notify_counter--;
        __TBB_ASSERT(prev_value != 0, "Overflow detected");
#if __TBB_DEBUG_RESOURCE_ACQUISITION
        TBB_DEBUG_LOG("[NOTIFY] body=%p id=%llu notify_counter: %zu -> %zu\n",
                    (void*)this, (unsigned long long)id.get_unique_integer(),
                    prev_value, prev_value - 1);
#endif
        if (prev_value == 1) {
#if __TBB_DEBUG_RESOURCE_ACQUISITION
            TBB_DEBUG_LOG("[TASK_EXECUTE] body=%p id=%llu - Spawning try_acquire_resources_and_execute_task\n",
                        (void*)this, (unsigned long long)id.get_unique_integer());
#endif
            d1::small_object_allocator allocator;
            using task_type = try_acquire_resources_and_execute_task<resource_limited_body_leaf, request_data_type>;
            graph_task* t = allocator.new_object<task_type>(this->graph_reference(), allocator, this, id, data);
            spawn_in_graph_arena(this->graph_reference(), *t);
        }
    }

    void report_pressure(std::size_t pressure) {
        report_pressure_helper<0, sizeof...(ResourceProviders)>::run(m_consumers, pressure);
    }

    resource_limited_body_leaf* clone() override {
        resource_limited_body_leaf* new_body = new resource_limited_body_leaf(this->graph_reference(), m_consumers, this->m_body, m_input_ptr);
        set_body_ptr_helper<sizeof...(ResourceProviders)>::run(new_body->m_consumers, new_body);
        return new_body;
    }

    void* get_body_ptr() override { return &m_body; }

    void update_input_ptr(resource_limited_input<Input, OutputPorts>* ptr) override {
        m_input_ptr = ptr;
    }

    template <typename ResourceHandlesTuple>
    void call_body(const Input& input, OutputPorts& ports, ResourceHandlesTuple& tuple) {
        call_body_impl(input, ports, tuple,
                       tbb::detail::make_index_sequence<std::tuple_size<ResourceHandlesTuple>::value>());
    }

    template <typename ResourceHandlesTuple, std::size_t... Idx>
    void call_body_impl(const Input& input, OutputPorts& ports, ResourceHandlesTuple& tuple,
                        tbb::detail::index_sequence<Idx...>) {
        tbb::detail::invoke(m_body, input, ports, std::get<Idx>(tuple).value()...);
    }
};

template <typename Input, typename OutputPorts>
class resource_limited_input
    : public function_input_base<Input, queueing, cache_aligned_allocator<Input>,
                                 resource_limited_input<Input, OutputPorts>>
{
public:
    static constexpr int N = std::tuple_size<OutputPorts>::value;
    using input_type = Input;
    using output_ports_type = OutputPorts;
    using resource_limited_body_type = resource_limited_body<input_type, output_ports_type>;
    using class_type = resource_limited_input<input_type, output_ports_type>;
    using base_type = function_input_base<input_type, queueing, cache_aligned_allocator<input_type>, class_type>;
    using input_queue_type = function_input_queue<input_type, cache_aligned_allocator<input_type>>;

    template <typename Body, typename... ResourceProviders>
    resource_limited_input(graph& g, std::size_t max_concurrency,
                           std::tuple<ResourceProviders&...> resource_providers,
                           Body& body)
        : base_type(g, max_concurrency, no_priority, is_body_noexcept(body, resource_providers))
        , m_body(new resource_limited_body_leaf<input_type, output_ports_type, Body, ResourceProviders...>(g, resource_providers, body, this))
        , m_init_body(new resource_limited_body_leaf<input_type, output_ports_type, Body, ResourceProviders...>(g, resource_providers, body, this))
        , m_output_ports(init_output_ports<output_ports_type>::call(g, m_output_ports))
    {}

    resource_limited_input(const resource_limited_input& other)
        : base_type(other)
        , m_body(other.m_init_body->clone())
        , m_init_body(other.m_init_body->clone())
        , m_output_ports(init_output_ports<output_ports_type>::call(this->graph_reference(), m_output_ports))
    {
        m_body->update_input_ptr(this);
        m_init_body->update_input_ptr(this);
    }

    ~resource_limited_input() {
        delete m_body;
        delete m_init_body;
    }

    graph_task* try_put_task( const input_type& t) override {
        m_body->note_try_put(t);
        return base_type::try_put_task(t);
    }

    graph_task* apply_body_impl_bypass(const input_type& i
                                       __TBB_FLOW_GRAPH_METAINFO_ARG(const message_metainfo&))
    {
        // Call the body to initiate resource acquisition
        // NOTE: function_input_base has already acquired a concurrency slot for us
        // We must NOT release it until the body actually completes execution
        (*m_body)(i, m_output_ports);

        // DO NOT call try_get_postponed_task() here!
        // The slot will be released after async body execution completes
        return SUCCESSFULLY_ENQUEUED;
    }

    // Called by body_leaf after body execution completes to release concurrency slot
    graph_task* release_concurrency_slot(const input_type& i) {
        // Only release concurrency if limiting is enabled (not unlimited)
        if (base_type::my_max_concurrency != 0) {
            // Delegate to base class method which releases slot and dequeues next message
            return base_type::try_get_postponed_task(i);
        }
        return nullptr;
    }

    output_ports_type& output_ports() { return m_output_ports; }

    template <typename Body>
    Body copy_function_object() {
        return *static_cast<Body*>(m_body->get_body_ptr());
    }
protected:
    void reset(reset_flags f) {
        base_type::reset_function_input_base(f);
        if (f & rf_clear_edges) clear_element<N>::clear_this(m_output_ports);
        if (f & rf_reset_bodies) {
            resource_limited_body_type* tmp = m_init_body->clone();
            delete m_body;
            m_body = tmp;
        }
        __TBB_ASSERT(!(f & rf_clear_edges) || clear_element<N>::this_empty(m_output_ports), "resource_limited_node reset failed");
    }
private:
    template <typename Body, typename... ResourceProviders>
    bool is_body_noexcept(Body& body, std::tuple<ResourceProviders&...>) {
        return noexcept(tbb::detail::invoke(body, std::declval<input_type>(), m_output_ports,
                                            std::declval<typename ResourceProviders::resource_handle_type&>()...));
    }

    resource_limited_body_type* m_body;
    resource_limited_body_type* m_init_body;
    output_ports_type           m_output_ports;
};

template <typename Input, typename OutputTuple>
class resource_limited_node
    : public graph_node
    , public resource_limited_input<Input, typename wrap_tuple_elements<multifunction_output, OutputTuple>::type>
{
public:
    using input_type = Input;
    using output_type = null_type;
    using output_ports_type = typename wrap_tuple_elements<multifunction_output, OutputTuple>::type;
private:
    using input_impl_type = resource_limited_input<input_type, output_ports_type>;
    using input_impl_type::my_predecessors;
public:
    template <typename Body, typename ResourceProvider, typename... ResourceProviders>
    resource_limited_node(graph& g, std::size_t concurrency,
                          std::tuple<ResourceProvider&, ResourceProviders&...> resource_providers,
                          Body body)
        : graph_node(g)
        , input_impl_type(g, concurrency, resource_providers, body)
    {}

    resource_limited_node(const resource_limited_node& other)
        : graph_node(other.my_graph)
        , input_impl_type(other)
    {}

protected:
    void reset_node(reset_flags f) override { input_impl_type::reset(f); }
}; // class resource_limited_node

} // namespace d2
} // namespace detail
} // namespace tbb

#endif // __TBB__flow_graph_resource_limiting_H
