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

#include <chrono>
#include <unordered_map>
#include <forward_list>
#include <functional>
#include <utility>
#include <atomic>
#include <tuple>

namespace tbb {
namespace detail {
namespace d2 {

template <typename ResourceHandle>
class resource_consumer_base;

class request_id {
    using clock_type = std::chrono::high_resolution_clock;
    using time_point = clock_type::time_point;

    time_point m_time_point;
public:
    request_id()
        : m_time_point(clock_type::now()) // TODO: use something else? Only time point is not sufficient. Should it be input address that is unique?
    {}

    struct hash {
        std::hash<time_point::duration::rep> m_hash;

        std::size_t operator()(request_id id) const {
            return m_hash(id.m_time_point.time_since_epoch().count());
        }
    };

    struct equal {
        std::equal_to<time_point> m_equal;

        bool operator()(request_id lhs, request_id rhs) const {
            return m_equal(lhs.m_time_point, rhs.m_time_point);
        }
    };
}; // class request_id

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

    resource_handle_optional(resource_handle_optional&& other)
        : m_has_value(other.m_has_value)
    {
        if (m_has_value) construct(std::move(other.m_resource_handle));
    }

    resource_handle_optional& operator=(resource_handle_optional&& other) {
        if (this != &other) {
            if (m_has_value) m_resource_handle.~ResourceHandle();
            m_has_value = other.m_has_value;
            if (other.m_has_value) {
                ::new(&m_resource_handle) ResourceHandle(std::move(other.m_resource_handle));
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
};

template <typename ResourceHandle>
class resource_provider_base {
public:
    using consumer_type = resource_consumer_base<ResourceHandle>;
    using optional_type = resource_handle_optional<ResourceHandle>;

    virtual void          request(consumer_type&, request_id) = 0;
    virtual optional_type acquire(consumer_type&, request_id) = 0;
    virtual void          release(consumer_type&, request_id, optional_type&&) = 0;
};

template <typename ResourceHandle>
class resource_consumer_base {
public:
    using provider_type = resource_provider_base<ResourceHandle>;
    virtual void notify(provider_type&, request_id) = 0;
};

// TODO: use actual fair implementation with starvation avoidance
template <typename ResourceHandle>
class resource_provider : public resource_provider_base<ResourceHandle> {
public:
    using resource_handle_type = ResourceHandle;
    using consumer_type = typename resource_provider_base<ResourceHandle>::consumer_type;
    using optional_type = typename resource_provider_base<ResourceHandle>::optional_type;

    template <typename Handle, typename... Handles>
    resource_provider(Handle&& handle, Handles&&... handles) {
        emplace_handles(std::forward<Handle>(handle), std::forward<Handles>(handles)...);
    }

    template <typename Handle, typename... Handles>
    void emplace_handles(Handle&& handle, Handles&&... handles) {
        m_resource_handles.emplace_front(std::forward<Handle>(handle));
        emplace_handles(std::forward<Handles>(handles)...);
    }

    void emplace_handles() {}

    void request(consumer_type& consumer, request_id id) override {
        tbb::spin_mutex::scoped_lock lock(m_mutex);

        if (m_resource_handles.empty()) {
            m_consumers.emplace_front(std::piecewise_construct, std::forward_as_tuple(id), std::forward_as_tuple(&consumer));
        } else {
            // Resource available immediately
            lock.release();
            consumer.notify(*this, id);
        }
    }

    optional_type acquire(consumer_type& consumer, request_id id) override {
        tbb::spin_mutex::scoped_lock lock(m_mutex);

        if (m_resource_handles.empty()) {
            m_consumers.emplace_front(std::piecewise_construct, std::forward_as_tuple(id), std::forward_as_tuple(&consumer));
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
        
        auto consumers = std::move(m_consumers);
        m_consumers.clear();

        lock.release();
        for (auto consumer_dt : consumers) {
            consumer_dt.second->notify(*this, consumer_dt.first);
        }
    }

    using consumer_data = std::pair<request_id, resource_consumer_base<ResourceHandle>*>;

    tbb::spin_mutex m_mutex;
    std::forward_list<ResourceHandle> m_resource_handles;
    std::forward_list<consumer_data>  m_consumers;
}; // class resource_provider

template <typename Input, typename OutputPorts>
class resource_limited_body {
    graph& m_graph;
public:
    virtual void operator()(const Input& input, OutputPorts& ports) = 0;
    virtual void notify(request_id id) = 0;
    virtual resource_limited_body* clone() = 0;
    virtual ~resource_limited_body() = default;

    resource_limited_body(graph& g) : m_graph(g) {}

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
        m_resource_provider.request(*this, id);
    }


    optional_type acquire_from_provider(request_id id) {
        return m_resource_provider.acquire(*this, id);
    }

    void release_to_provider(request_id id, optional_type&& handle) {
        m_resource_provider.release(*this, id, std::move(handle));
    }

    void notify(resource_provider_base<resource_handle_type>& provider, request_id id) override {
        __TBB_ASSERT(&provider == &m_resource_provider, "Provider-consumer mismatch");
        m_body_ptr->notify(id);
        tbb::detail::suppress_unused_warning(provider);
    }

    ResourceProvider&           m_resource_provider;
    resource_limited_body_type* m_body_ptr;
};

template <typename Input, typename OutputPorts, typename HandlesTuple>
struct request_data {
    Input                    input_message;
    OutputPorts&             output_ports;
    std::atomic<std::size_t> notify_counter;
    HandlesTuple             handles;
    bool                     ready;

    request_data(const Input& input, OutputPorts& ports)
        : input_message(input)
        , output_ports(ports)
        , notify_counter(std::tuple_size<HandlesTuple>::value + 1)
        , ready(false)
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

template <std::size_t Index>
struct request_resources_helper<Index, Index> {
    template <typename ConsumerTuple>
    static void run(ConsumerTuple&, request_id) {}
};

template <std::size_t Index>
struct release_resources_helper {
    template <typename ConsumerTuple, typename RequestData>
    static void run(ConsumerTuple& consumers, request_id id, RequestData& req_data) {
        std::get<Index - 1>(consumers).release_to_provider(id, std::move(std::get<Index - 1>(req_data.handles)));
        release_resources_helper<Index - 1>::run(consumers, id, req_data);
    }
};

template <>
struct release_resources_helper<0> {
    template <typename ConsumerTuple, typename RequestData>
    static void run(ConsumerTuple&, request_id, RequestData&) {}
};

template <std::size_t Index, std::size_t MaxIndex>
struct acquire_resources_helper {
    template <typename Body, typename ConsumerTuple, typename RequestData>
    static void run(Body* body_ptr, ConsumerTuple& consumers, request_id id, RequestData& req_data) {
        auto handle_optional = std::get<Index>(consumers).acquire_from_provider(id);
        if (handle_optional.has_value()) {
            // Successfully acquired resource - save the handle and proceed to the next resource
            std::get<Index>(req_data.handles) = std::move(handle_optional);
            acquire_resources_helper<Index + 1, MaxIndex>::run(body_ptr, consumers, id, req_data);
        } else {
            // One of the resources denied the request
            req_data.notify_counter += 2; // One reference for the denied resource and one self reference for resource releaser
            release_resources_helper<Index>::run(consumers, id, req_data);
            body_ptr->release_self_ref(id, req_data);
        }
    }
};

template <std::size_t Index>
struct acquire_resources_helper<Index, Index> {
    template <typename Body, typename ConsumerTuple, typename RequestData>
    static void run(Body*, ConsumerTuple&, request_id, RequestData& req_data) {
        req_data.ready = true;
    }
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
        m_body->try_acquire_resources_and_execute(m_id, m_request_data);
        graph_task::template finalize<try_acquire_resources_and_execute_task>(ed);
        return nullptr;
    }

    d1::task* cancel(d1::execution_data& ed) override {
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

    template <typename ConsumersTuple>
    resource_limited_body_leaf(graph& g, ConsumersTuple&& consumers_tuple, const Body& body)
        : resource_limited_body<Input, OutputPorts>(g)
        , m_consumers(std::forward<ConsumersTuple>(consumers_tuple))
        , m_body(body)
    {}

public:
    resource_limited_body_leaf(graph& g, std::tuple<ResourceProviders&...> resource_providers, const Body& body)
        : resource_limited_body_leaf(g, get_consumers_tuple(resource_providers), body)
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

    void operator()(const Input& input, OutputPorts& ports) noexcept override {
        auto& res = form_request(input, ports);
        request_resources(res.first);
        release_self_ref(res.first, res.second);
    }

    typename requests_map_type::reference form_request(const Input& input, OutputPorts& ports) {
        request_id id;
        
        tbb::spin_mutex::scoped_lock lock(m_mutex);
        auto res = m_requests.emplace(std::piecewise_construct,
                                      std::forward_as_tuple(id),
                                      std::forward_as_tuple(input, ports));
        this->graph_reference().reserve_wait(); // TODO: is it needed?
        __TBB_ASSERT(res.second, "Duplicated requests in the map");
        return *res.first;
    }

    void request_resources(request_id id) {
        request_resources_helper<0, sizeof...(ResourceProviders)>::run(m_consumers, id);
    }

    void release_self_ref(request_id id, request_data_type& req_data) {
        if (--req_data.notify_counter == 0) {
            // Notifications from all resources received
            try_acquire_resources_and_execute(id, req_data);
        }
    }

    void try_acquire_resources(request_id id, request_data_type& req_data) {
        acquire_resources_helper<0, sizeof...(ResourceProviders)>::run(this, m_consumers, id, req_data);
    }

    void try_acquire_resources_and_execute(request_id id, request_data_type& req_data) {
        try_acquire_resources(id, req_data);

        if (req_data.ready) {
            // Access to all resources is granted
            call_body(req_data.input_message, req_data.output_ports, req_data.handles);
            release_resources(id, req_data);
            remove_request(id);
        }
    }

    void release_resources(request_id id, request_data_type& req_data) {
        release_resources_helper<sizeof...(ResourceProviders)>::run(m_consumers, id, req_data);
    }

    void remove_request(request_id id) {
        tbb::spin_mutex::scoped_lock lock(m_mutex);
        std::size_t num_removed = m_requests.erase(id);
        this->graph_reference().release_wait(); // TODO: is it needed
        __TBB_ASSERT(num_removed == 1, "Removing unregistered request");
        tbb::detail::suppress_unused_warning(num_removed);
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

    void notify(request_id id) {
        
        tbb::spin_mutex::scoped_lock lock(m_mutex);
        auto res = m_requests.find(id);
        __TBB_ASSERT(res != m_requests.end(), "Cannot find request for notification");
        request_data_type& data = res->second;
        lock.release();

        if ((--data.notify_counter) == 0) {
            // Spawn acquire task
            d1::small_object_allocator allocator;
            using task_type = try_acquire_resources_and_execute_task<resource_limited_body_leaf, request_data_type>;
            graph_task* t = allocator.new_object<task_type>(this->graph_reference(), allocator, this, id, data);
            spawn_in_graph_arena(this->graph_reference(), *t);
        }
    }

    resource_limited_body_leaf* clone() override {
        return new resource_limited_body_leaf(this->graph_reference(), m_consumers, m_body);
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
        : base_type(g, max_concurrency, no_priority, true) // TODO: fill noexcept correctly
        , m_body(new resource_limited_body_leaf<input_type, output_ports_type, Body, ResourceProviders...>(g, resource_providers, body))
        , m_init_body(new resource_limited_body_leaf<input_type, output_ports_type, Body, ResourceProviders...>(g, resource_providers, body))
        , m_output_ports(init_output_ports<output_ports_type>::call(g, m_output_ports))
    {}

    resource_limited_input(const resource_limited_input& other)
        : base_type(other)
        , m_body(other.m_init_body->clone())
        , m_init_body(other.m_init_body->clone())
        , m_output_ports(init_output_ports<output_ports_type>::call(this->graph_reference(), m_output_ports))
    {}

    ~resource_limited_input() {
        delete m_body;
        delete m_init_body;
    }

    graph_task* apply_body_impl_bypass(const input_type& i
                                       __TBB_FLOW_GRAPH_METAINFO_ARG(const message_metainfo&))
    {
        (*m_body)(i, m_output_ports);
        graph_task* ttask = nullptr;
        if (base_type::my_max_concurrency != 0) {
            ttask = base_type::try_get_postponed_task(i);
        }
        return ttask ? ttask : SUCCESSFULLY_ENQUEUED;
    }

    output_ports_type& output_ports() { return m_output_ports; }
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
        : graph_node(other.m_graph)
        , input_impl_type(other)
    {}
protected:
    void reset_node(reset_flags f) override { input_impl_type::reset(f); }
}; // class resource_limited_node

} // namespace d2
} // namespace detail
} // namespace tbb

#endif // __TBB__flow_graph_resource_limiting_H
