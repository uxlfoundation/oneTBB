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

#define TBB_PREVIEW_FLOW_GRAPH_RESOURCE_LIMITING 1
#include "common/config.h"
#include "common/test.h"
#include "common/utils.h"
#include "common/graph_utils.h"
#include "common/test_invoke.h"

#include "conformance/conformance_flowgraph.h"

#include "tbb/flow_graph.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <vector>

//! \file test_resource_limited_node.cpp
//! \brief Test for [preview] functionality

using input_msg = conformance::message</*default_ctor = */true, /*copy_ctor = */true, /*copy_assign = */false>;
using output_msg = conformance::message</*default_ctor = */false, /*copy_ctor = */false, /*copy_assign = */false>;

template <typename Input, typename OutputTuple>
void test_inheritance() {
    using namespace oneapi::tbb::flow;

    using node_type = resource_limited_node<Input, OutputTuple>;
    CHECK_MESSAGE((std::is_base_of<graph_node, node_type>::value), "graph_node is not base of resource_limited_node");
    CHECK_MESSAGE((std::is_base_of<receiver<Input>, node_type>::value), "receiver is not base of resource_limited_node");
}

void test_single_resource() {
    using namespace oneapi::tbb::flow;

    using node_type = resource_limited_node<int, std::tuple<>>;
    using ports_type = typename node_type::output_ports_type;

    int resource_value = 100;
    int resource = resource_value;
    resource_limiter<int*> limiter{&resource};
    
    graph g;
    broadcast_node<int> start(g);

    const std::size_t num_nodes = 10;
    std::vector<node_type> nodes;
    nodes.reserve(num_nodes);

    int input_message = 0;
    std::atomic<std::size_t> counter(0);
    std::size_t num_body_runs = 0;
    auto node_body = [&](int input, ports_type&, int* resource_handle) {
        CHECK_MESSAGE(input == input_message, "Incorrect input");
        CHECK_MESSAGE(*resource_handle == resource_value, "Incorrect resource value");
        ++counter;
        for (std::size_t i = 0; i < 1000; ++i) {
            CHECK_MESSAGE(counter == 1, "Single resource was given to someone else");
        }
        ++num_body_runs;
        --counter;
    };

    for (std::size_t i = 0; i < num_nodes; ++i) {
        nodes.emplace_back(g, unlimited, std::tie(limiter), node_body);
        make_edge(start, nodes.back());
    }

    start.try_put(input_message);
    g.wait_for_all();

    CHECK_MESSAGE(counter == 0, "Incorrect counter value");
    CHECK_MESSAGE(num_body_runs == num_nodes, "Incorrect number of bodies executed");
    CHECK_MESSAGE(resource == resource_value, "Incorrect resource value");
}

#if __TBB_RESUMABLE_TASKS
// Test that ten resources are granted simultaneously to ten different nodes
void test_several_resources() {
    using namespace oneapi::tbb::flow;

    resource_limiter<int> limiter{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    using node_type = resource_limited_node<int, std::tuple<int>>;
    using ports_type = typename node_type::output_ports_type;

    graph g;
    broadcast_node<int> start(g);

    const std::size_t num_nodes = 10;
    std::vector<node_type> nodes;
    nodes.reserve(num_nodes);

    buffer_node<int> output(g);

    std::size_t counter = 0;
    std::mutex body_mutex;
    std::vector<oneapi::tbb::task::suspend_point> suspend_points;
    suspend_points.reserve(num_nodes);

    auto node_body = [&](int input, ports_type& ports, int resource_copy) {
        std::unique_lock<std::mutex> lock(body_mutex);

        if (++counter == num_nodes) {
            auto points = std::move(suspend_points);
            lock.unlock();
            for (auto sp : points) {
                oneapi::tbb::task::resume(sp);
            }
        } else {
            oneapi::tbb::task::suspend([&](oneapi::tbb::task::suspend_point sp) {
                suspend_points.emplace_back(sp);
                lock.unlock();
            });
        }
        std::get<0>(ports).try_put(input + resource_copy);
    };

    for (std::size_t i = 0; i < num_nodes; ++i) {
        nodes.emplace_back(g, unlimited, std::tie(limiter), node_body);
        make_edge(start, nodes.back());
        make_edge(output_port<0>(nodes.back()), output);
    }

    start.try_put(100);
    g.wait_for_all();

    std::unordered_set<int> validation_set;
    for (int i = 0; i < 10; ++i) {
        validation_set.emplace(100 + i);
    }

    for (std::size_t i = 0; i < 10; ++i) {
        int buffered_output = -1;
        CHECK_MESSAGE(output.try_get(buffered_output), "Desired output not received");
        CHECK_MESSAGE(validation_set.erase(buffered_output) == 1, "Incorrect output");
    }
    CHECK(validation_set.empty());
}
#endif // __TBB_RESUMABLE_TASKS

struct strict_resource_handle {
    int underlying_resource;

    strict_resource_handle(int value) : underlying_resource(value) {}
    friend struct strict_resource_handle_provider;
public:
    strict_resource_handle(strict_resource_handle&&) = default;
    strict_resource_handle(const strict_resource_handle&) = default;

    strict_resource_handle& operator=(strict_resource_handle&&) = default;

    strict_resource_handle() = delete;
    strict_resource_handle& operator=(const strict_resource_handle&) = delete;

    int& get_underlying_resource() { return underlying_resource; }
};

struct strict_resource_handle_provider {
    static strict_resource_handle construct(int value) {
        return strict_resource_handle(value);
    }
};

void test_strict_resource_handle() {
    using namespace tbb::flow;

    int handle_value = 42;
    resource_limiter<strict_resource_handle> limiter(std::piecewise_construct,
                                                     std::forward_as_tuple(strict_resource_handle_provider::construct(handle_value)));

    using node_type = resource_limited_node<int, std::tuple<>>;
    using ports_type = typename node_type::output_ports_type;

    int input_message = 100;

    graph g;
    node_type node(g, unlimited, std::tie(limiter),
        [&](int input, ports_type&, strict_resource_handle& resource_handle) {
            CHECK_MESSAGE(input == input_message, "Incorrect input message");
            CHECK_MESSAGE(resource_handle.get_underlying_resource() == handle_value, "Incorrect resource value");
        });

    node.try_put(input_message);
    g.wait_for_all();
}

struct counting_resource {
    std::atomic<std::size_t> counter;

    counting_resource() : counter(0) {}

    void use() {
        std::size_t value = ++counter;
        CHECK_MESSAGE(value == 1, "Resource in use by someone else");

        for (std::size_t i = 0; i < 10000; ++i) {
            CHECK_MESSAGE(counter.load() == 1, "Resource in use by someone else");
        }

        --counter;
    }
};

void test_root_genie() {
    counting_resource root_resource;
    counting_resource genie_resource;

    using namespace oneapi::tbb::flow;
    using node_type = resource_limited_node<int, std::tuple<int>>;
    using ports_type = typename node_type::output_ports_type;

    resource_limiter<counting_resource*> root_limiter{&root_resource};
    resource_limiter<counting_resource*> genie_limiter{&genie_resource};

    graph g;

    broadcast_node<int> start(g);

    // Records the order in which the three nodes are served, so that the node needing both
    // resources can be checked for starvation against the two nodes needing only one.
    std::mutex order_mutex;
    std::vector<int> service_order;
    auto note_service = [&](int node_index) {
        std::lock_guard<std::mutex> lock(order_mutex);
        service_order.push_back(node_index);
    };

    node_type root_node(g, unlimited, std::tie(root_limiter),
        [&](int input, ports_type& ports, counting_resource* root) {
            CHECK(root == &root_resource);
            root->use();
            note_service(0);
            std::get<0>(ports).try_put(input);
        });

    node_type genie_node(g, unlimited, std::tie(genie_limiter),
        [&](int input, ports_type& ports, counting_resource* genie) {
            CHECK(genie == &genie_resource);
            genie->use();
            note_service(1);
            std::get<0>(ports).try_put(input);
        });

    node_type root_genie_node(g, unlimited, std::tie(root_limiter, genie_limiter),
        [&](int input, ports_type& ports, counting_resource* root, counting_resource* genie) {
            CHECK(root == &root_resource);
            CHECK(genie == &genie_resource);
            root->use();
            genie->use();
            note_service(2);
            std::get<0>(ports).try_put(input);
        });

    buffer_node<int> root_inputs(g);
    buffer_node<int> genie_inputs(g);
    buffer_node<int> root_genie_inputs(g);

    make_edge(start, root_node);
    make_edge(start, root_genie_node);
    make_edge(start, genie_node);
    make_edge(output_port<0>(root_node), root_inputs);
    make_edge(output_port<0>(genie_node), genie_inputs);
    make_edge(output_port<0>(root_genie_node), root_genie_inputs);

    std::unordered_multiset<int> inputs;

    int num_inputs = 100;
    for (int i = 0; i < num_inputs; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            inputs.emplace(i);
        }

        start.try_put(i);
    }

    g.wait_for_all();

    for (int i = 0; i < num_inputs; ++i) {
        int root_input = 0;
        int genie_input = 0;
        int root_genie_input = 0;
        CHECK_MESSAGE(root_inputs.try_get(root_input), "No input processed by root node");
        CHECK_MESSAGE(genie_inputs.try_get(genie_input), "No input processed by genie node");
        CHECK_MESSAGE(root_genie_inputs.try_get(root_genie_input), "No input processed by root-genie node");
        
        auto it = inputs.find(root_input);
        CHECK_MESSAGE(it != inputs.end(), "Root node did not process the required input");
        inputs.erase(it);

        it = inputs.find(genie_input);
        CHECK_MESSAGE(it != inputs.end(), "Genie node did not process the required input");
        inputs.erase(it);

        it = inputs.find(root_genie_input);
        CHECK_MESSAGE(it != inputs.end(), "Root-genie node did not process the required input");
        inputs.erase(it);
    }
    CHECK(inputs.empty());

    CHECK_MESSAGE(service_order.size() == std::size_t(3 * num_inputs),
                  "Not every body run was recorded");
    CHECK_MESSAGE(std::count(service_order.begin(), service_order.end(), 2) == num_inputs,
                  "The node needing both resources was not served for every message");
    CHECK_MESSAGE(root_resource.counter.load() == 0, "The root resource was left in use");
    CHECK_MESSAGE(genie_resource.counter.load() == 0, "The genie resource was left in use");
}

// Verifies the arbitration policy directly against the provider, without a graph in the way.
// The consumer records the order it is notified in, so the arbitration order is observable
// exactly rather than statistically.
class recording_consumer : public tbb::detail::d2::resource_consumer_base<int> {
    using base_type = tbb::detail::d2::resource_consumer_base<int>;
public:
    using request_id = tbb::detail::d2::request_id;

    recording_consumer(tbb::flow::resource_limiter<int>& limiter, int name)
        : m_limiter(limiter), m_name(name)
    {}

    void notify(typename base_type::provider_type& provider, request_id id) override {
        CHECK(&provider == &m_limiter);
        m_notifications.push_back(id);
    }

    request_id request(std::uint64_t counter_value) {
        request_id id{counter_value};
        m_limiter.request(*this, id);
        return id;
    }

    tbb::detail::d2::resource_handle_optional<int> acquire(request_id id) {
        return m_limiter.acquire(*this, id);
    }

    void release(request_id id, tbb::detail::d2::resource_handle_optional<int>&& handle) {
        m_limiter.release(*this, id, std::move(handle));
    }

    void withdraw(request_id id) { m_limiter.withdraw(*this, id); }

    void report_pressure(std::size_t pressure) { m_limiter.report_pressure(*this, pressure); }

    std::size_t num_notifications() const { return m_notifications.size(); }
    bool notified(request_id id) const {
        return std::find(m_notifications.begin(), m_notifications.end(), id) != m_notifications.end();
    }
    void clear_notifications() { m_notifications.clear(); }

    int name() const { return m_name; }

private:
    tbb::flow::resource_limiter<int>&      m_limiter;
    int                                    m_name;
    std::vector<request_id>                m_notifications;
};

void test_request_order() {
    tbb::flow::resource_limiter<int> limiter{42};

    // The two consumers are given disjoint counter ranges so that the expected order is the
    // same whether or not the requests land on the same clock tick: a request_id orders by
    // timestamp and then by counter value, and the observed clock granularity is coarse
    // mostly likely have the same timestamp.
    recording_consumer early(limiter, 0);
    recording_consumer late(limiter, 1);

    // The single handle goes to the first request, which is notified immediately.
    auto early_first = early.request(1);
    CHECK_MESSAGE(early.num_notifications() == 1, "The first request was not notified");
    CHECK_MESSAGE(early.notified(early_first), "The wrong request was notified");

    // While it holds the handle, three more requests queue up behind it.
    auto handle = early.acquire(early_first);
    CHECK_MESSAGE(handle.has_value(), "The only request did not get the only handle");

    auto early_second = early.request(2);
    auto early_third = early.request(3);
    auto late_first = late.request(10);
    CHECK_MESSAGE(early.num_notifications() == 1, "A request was notified with no handle available");
    CHECK_MESSAGE(late.num_notifications() == 0, "A request was notified with no handle available");

    // Each release notifies exactly one request, the earliest of those still waiting.
    early.release(early_first, std::move(handle));
    CHECK_MESSAGE(early.num_notifications() == 2, "The next request in order was not notified");
    CHECK_MESSAGE(early.notified(early_second), "Requests were not served in request order");
    CHECK_MESSAGE(late.num_notifications() == 0,
                  "A later request was notified while an earlier one was pending");

    handle = early.acquire(early_second);
    CHECK_MESSAGE(handle.has_value(), "The earliest request was denied the handle");
    early.release(early_second, std::move(handle));

    CHECK_MESSAGE(early.num_notifications() == 3, "The next request in order was not notified");
    CHECK_MESSAGE(early.notified(early_third), "Requests were not served in request order");
    CHECK_MESSAGE(late.num_notifications() == 0,
                  "A later request was notified while an earlier one was pending");

    handle = early.acquire(early_third);
    CHECK_MESSAGE(handle.has_value(), "The notified request was denied the handle");
    early.release(early_third, std::move(handle));

    // Only once the earlier consumer is drained does the later one get its turn.
    CHECK_MESSAGE(late.num_notifications() == 1, "The last request was not notified");
    CHECK_MESSAGE(late.notified(late_first), "The last request was not the one notified");
    handle = late.acquire(late_first);
    CHECK_MESSAGE(handle.has_value(), "The notified request was denied the handle");
    late.release(late_first, std::move(handle));
}

// Pressure reporting is plumbed through to the providers, but a resource_limiter deliberately
// ignores it for now.
void test_pressure_does_not_affect_order() {
    tbb::flow::resource_limiter<int> limiter{42};

    recording_consumer early(limiter, 0);
    recording_consumer late(limiter, 1);
    recording_consumer idle(limiter, 2);

    // Reporting for a consumer that has no outstanding request must be a harmless no-op.
    idle.report_pressure(std::size_t{1000});
    CHECK_MESSAGE(idle.num_notifications() == 0, "Reporting pressure notified an idle consumer");

    auto early_first = early.request(1);
    auto handle = early.acquire(early_first);
    CHECK_MESSAGE(handle.has_value(), "The first request did not get the only handle");

    auto early_second = early.request(2);
    auto late_first = late.request(10);

    // The later request claims a large backlog and the earlier one claims none. An ordering that
    // used the pressure would invert the two here; under request-id ordering nothing changes.
    late.report_pressure(std::size_t{1000});
    early.report_pressure(std::size_t{0});
    CHECK_MESSAGE(early.num_notifications() == 1, "Reporting pressure notified a waiting request");
    CHECK_MESSAGE(late.num_notifications() == 0, "Reporting pressure notified a waiting request");

    early.release(early_first, std::move(handle));
    CHECK_MESSAGE(early.notified(early_second),
                  "The earlier request lost its place after a pressure report");
    CHECK_MESSAGE(late.num_notifications() == 0,
                  "A later request was notified ahead of an earlier one after reporting pressure");

    // Drain, reporting again along the way, and confirm the order still holds.
    handle = early.acquire(early_second);
    CHECK_MESSAGE(handle.has_value(), "The earliest request was denied the handle");
    late.report_pressure(std::size_t{2000});
    early.release(early_second, std::move(handle));

    CHECK_MESSAGE(late.num_notifications() == 1, "The last request was not notified once it was earliest");
    CHECK_MESSAGE(late.notified(late_first), "The last request was not the one notified");
    handle = late.acquire(late_first);
    CHECK_MESSAGE(handle.has_value(), "The notified request was denied the handle");
    late.release(late_first, std::move(handle));
}

void test_withdraw() {
    tbb::flow::resource_limiter<int> limiter{42};

    recording_consumer consumer(limiter, 0);

    // Withdrawing a notified request must free up the notification slot it occupied, or the
    // requests behind it never get notified.
    auto notified = consumer.request(1);
    CHECK_MESSAGE(consumer.num_notifications() == 1, "The first request was not notified");

    auto pending = consumer.request(2);
    CHECK_MESSAGE(consumer.num_notifications() == 1, "Both requests were notified for one handle");

    consumer.withdraw(notified);
    CHECK_MESSAGE(consumer.num_notifications() == 2,
                  "Withdrawing a notified request did not release its notification slot");
    CHECK_MESSAGE(consumer.notified(pending), "The pending request was not the one notified");

    auto handle = consumer.acquire(pending);
    CHECK_MESSAGE(handle.has_value(), "The notified request was denied the handle");
    consumer.release(pending, std::move(handle));

    // Withdrawing a pending request drops it, and withdrawing an unknown request is a no-op.
    consumer.clear_notifications();
    auto held = consumer.request(3);
    handle = consumer.acquire(held);
    CHECK_MESSAGE(handle.has_value(), "The only request did not get the only handle");

    auto to_withdraw = consumer.request(4);
    consumer.withdraw(to_withdraw);
    consumer.withdraw(to_withdraw); // already withdrawn, must not disturb anything

    consumer.release(held, std::move(handle));
    CHECK_MESSAGE(consumer.num_notifications() == 1,
                  "A withdrawn request was notified after the handle was released");
}

// Drives requests at two providers and records which provider notified it, so that the
// cross-provider agreement the protocol depends on is observable directly.
class two_provider_consumer : public tbb::detail::d2::resource_consumer_base<int> {
    using base_type = tbb::detail::d2::resource_consumer_base<int>;
public:
    using request_id = tbb::detail::d2::request_id;
    using limiter_type = tbb::flow::resource_limiter<int>;
    using handle_type = tbb::detail::d2::resource_handle_optional<int>;

    two_provider_consumer(limiter_type& first, limiter_type& second)
        : m_limiters{&first, &second}
        , m_notifications{0, 0}
    {}

    void notify(typename base_type::provider_type& provider, request_id) override {
        for (std::size_t i = 0; i < 2; ++i) {
            if (&provider == m_limiters[i]) {
                ++m_notifications[i];
                return;
            }
        }
        CHECK_MESSAGE(false, "Notification from an unknown provider");
    }

    request_id request(std::size_t which, std::uint64_t counter_value) {
        request_id id{counter_value};
        m_limiters[which]->request(*this, id);
        return id;
    }

    // Requests the same id at another provider, as a request needing several resources does
    void request_again(std::size_t which, request_id id) { m_limiters[which]->request(*this, id); }

    handle_type acquire(std::size_t which, request_id id) {
        return m_limiters[which]->acquire(*this, id);
    }

    void release(std::size_t which, request_id id, handle_type&& handle) {
        m_limiters[which]->release(*this, id, std::move(handle));
    }

    std::size_t num_notifications(std::size_t which) const { return m_notifications[which]; }

private:
    limiter_type* m_limiters[2];
    std::size_t   m_notifications[2];
};

// The minimal shape that deadlocks if the providers can disagree about which request
// outranks the other.
void test_cross_provider_agreement() {
    tbb::flow::resource_limiter<int> first{1};
    tbb::flow::resource_limiter<int> second{2};

    two_provider_consumer earlier(first, second);
    two_provider_consumer later(first, second);

    // earlier_id requests at first, and later_id requests at second
    auto earlier_id = earlier.request(0, 1);
    CHECK_MESSAGE(earlier.num_notifications(0) == 1, "The first request was not notified");

    auto later_id = later.request(1, 10);
    CHECK_MESSAGE(later.num_notifications(1) == 1, "The first request was not notified");

    // Now request the same id at the other provider, as a request needing several resources does.
    earlier.request_again(1, earlier_id);
    CHECK_MESSAGE(earlier.num_notifications(1) == 1,
                  "The higher priority lost to a lower priority one");

    later.request_again(0, later_id);
    CHECK_MESSAGE(later.num_notifications(0) == 0,
                  "A lower priority request displaced a higher priority notification");

    // Notified at both providers, the earlier consumer can acquire both handles.
    auto first_handle = earlier.acquire(0, earlier_id);
    auto second_handle = earlier.acquire(1, earlier_id);
    CHECK_MESSAGE(first_handle.has_value(),
                  "The highest priority request was denied the first handle");
    CHECK_MESSAGE(second_handle.has_value(),
                  "The highest priority request was denied the second handle");

    earlier.release(0, earlier_id, std::move(first_handle));
    CHECK_MESSAGE(later.num_notifications(0) == 1,
                  "The waiting request was not notified once the handle was released");
    earlier.release(1, earlier_id, std::move(second_handle));
    CHECK_MESSAGE(later.num_notifications(1) == 1, "The waiting request was not notified once the handle was released");

    // And the later consumer now finishes too, so nothing was left blocked.
    auto later_first = later.acquire(0, later_id);
    auto later_second = later.acquire(1, later_id);
    CHECK_MESSAGE(later_first.has_value(), "The remaining request was denied the first handle");
    CHECK_MESSAGE(later_second.has_value(), "The remaining request was denied the second handle");
    later.release(0, later_id, std::move(later_first));
    later.release(1, later_id, std::move(later_second));
}

// A ring of N nodes, each needing the two resources it shares with its neighbours, so that
// every limiter is contended by two nodes and every node contends for two limiters.
void test_dining_ring() {
    using namespace oneapi::tbb::flow;

    constexpr std::size_t num_philosophers = 5;
    constexpr int num_meals = 20;

    using node_type = resource_limited_node<int, std::tuple<int>>;
    using ports_type = typename node_type::output_ports_type;

    std::vector<counting_resource> chopsticks(num_philosophers);
    std::vector<std::unique_ptr<resource_limiter<counting_resource*>>> limiters;
    limiters.reserve(num_philosophers);
    for (std::size_t i = 0; i < num_philosophers; ++i) {
        limiters.emplace_back(new resource_limiter<counting_resource*>({&chopsticks[i]}));
    }

    graph g;

    std::vector<std::atomic<int>> meals_eaten(num_philosophers);
    for (std::size_t i = 0; i < num_philosophers; ++i) {
        meals_eaten[i].store(0);
    }

    // think_nodes hold no resources; eat_nodes hold the left and right chopstick. Each meal
    // loops back through the think node, so a philosopher only ever has one message in
    // flight and the ring cannot be satisfied by buffering.
    std::vector<std::unique_ptr<function_node<int, int>>> think_nodes;
    std::vector<std::unique_ptr<node_type>> eat_nodes;
    think_nodes.reserve(num_philosophers);
    eat_nodes.reserve(num_philosophers);

    for (std::size_t i = 0; i < num_philosophers; ++i) {
        think_nodes.emplace_back(new function_node<int, int>(g, unlimited,
            [](int meal) { return meal; }));

        std::size_t left = i;
        std::size_t right = (i + 1) % num_philosophers;

        eat_nodes.emplace_back(new node_type(g, unlimited,
            std::tie(*limiters[left], *limiters[right]),
            [&, i](int meal, ports_type& ports,
                              counting_resource* left_chopstick, counting_resource* right_chopstick) {
                left_chopstick->use();
                right_chopstick->use();
                ++meals_eaten[i];
                if (meal + 1 < num_meals) {
                    std::get<0>(ports).try_put(meal + 1);
                }
            }));
    }

    for (std::size_t i = 0; i < num_philosophers; ++i) {
        make_edge(*think_nodes[i], *eat_nodes[i]);
        make_edge(output_port<0>(*eat_nodes[i]), *think_nodes[i]);
    }

    for (std::size_t i = 0; i < num_philosophers; ++i) {
        think_nodes[i]->try_put(0);
    }

    g.wait_for_all();

    for (std::size_t i = 0; i < num_philosophers; ++i) {
        CHECK_MESSAGE(meals_eaten[i].load() == num_meals,
                      "Philosopher " << i << " ate " << meals_eaten[i].load()
                      << " of " << num_meals << " meals");
    }
}

void test_cancellation_with_active_requests(bool same_graph, bool exception) {
    using namespace tbb::flow;

    int resource_value = 1;
    int input_value = 2;
    resource_limiter<int> limiter{resource_value};

    using node_type = resource_limited_node<int, std::tuple<>>;
    using ports_type = typename node_type::output_ports_type;
    
#if TBB_USE_EXCEPTIONS
    struct body_exception {};
#endif
    
    tbb::task_group_context g2_context(tbb::task_group_context::isolated);
    graph g1;
    graph g2(g2_context);

    graph* cancel_node_graph_ptr = same_graph ? &g2 : &g1;

    const std::size_t n_submissions = 100;
    std::atomic<std::size_t> g2_node_body_counter{0};

    node_type keep_using_node(g2, unlimited, std::tie(limiter),
        [&](int input, ports_type&, int resource) {
            CHECK_MESSAGE(input == input_value, "Incorrect input");
            CHECK_MESSAGE(resource == resource_value, "Incorrect resource");

            ++g2_node_body_counter;
        });

    node_type cancel_node(*cancel_node_graph_ptr, unlimited, std::tie(limiter),
        [&](int input, ports_type&, int resource) {
            CHECK_MESSAGE(input == input_value, "Incorrect input");
            CHECK_MESSAGE(resource == resource_value, "Incorrect resource");

            for (std::size_t i = 0; i < n_submissions; ++i) {
                keep_using_node.try_put(input);
            }

            if (exception) {
#if TBB_USE_EXCEPTIONS
                throw body_exception{};
#else
                CHECK_MESSAGE(false, "exception test was called when exceptions are not supported");
#endif
            } else {
                cancel_node_graph_ptr->cancel();
            }

            for (std::size_t i = 0; i < n_submissions; ++i) {
                keep_using_node.try_put(input);
            }
        });

    cancel_node.try_put(input_value);

#if TBB_USE_EXCEPTIONS
    bool caught_exception = false;
    try {
        cancel_node_graph_ptr->wait_for_all();
    } catch (body_exception) {
        caught_exception = true;
    }

    CHECK_MESSAGE(exception == caught_exception, "Expected exception was not caught");
#else
    cancel_node_graph_ptr->wait_for_all();
#endif

    g2.wait_for_all();

    if (same_graph) {
        CHECK_MESSAGE(g2_node_body_counter <= n_submissions, "Incorrect number of g2 node body calls");
    } else {
        std::size_t expected_g2_body_calls = exception ? n_submissions : 2 * n_submissions;
        CHECK_MESSAGE(g2_node_body_counter == expected_g2_body_calls,
                      "Incorrect number of g2 node body calls");
    }
}

template <typename ArrayType, typename... ConstructorArgs>
void test_resource_limiter_constructor(const ArrayType& resource_values, ConstructorArgs&&... constructor_args) {
    using namespace oneapi::tbb::flow;
    using resource_type = typename ArrayType::value_type;

    resource_limiter<resource_type> limiter(std::forward<ConstructorArgs>(constructor_args)...);

    graph g;

    using node_type = resource_limited_node<int, std::tuple<>>;
    using ports_type = typename node_type::output_ports_type;
    std::atomic<std::size_t> counter(0);

    auto node_body = [&](int, ports_type&, resource_type resource) {
        auto is_equal = [=](const resource_type& value) { return value == resource; };
        CHECK_MESSAGE(std::any_of(resource_values.begin(), resource_values.end(), is_equal),
                      "Unexpected resource");
                      
        ++counter;
        for (std::size_t i = 0; i < 1000; ++i) {
            CHECK_MESSAGE(counter <= resource_values.size(), "Detected more resources than expected");
        }
        --counter;
    };

    node_type node(g, unlimited, std::tie(limiter), node_body);

    for (int i = 0; i < 1000; ++i) {
        node.try_put(0);
    }
    g.wait_for_all();
    CHECK(counter == 0);
}

template <std::size_t... Idx, typename ArrayType>
void test_resource_limiter_handles_constructor(ArrayType& resources,
                                               tbb::detail::index_sequence<Idx...>) {
    CHECK(sizeof...(Idx) == resources.size());
    test_resource_limiter_constructor(/*resource_values = */resources,
                                      /*args = */std::piecewise_construct, std::forward_as_tuple(resources[Idx])...);
}

template <std::size_t... Idx, typename ArrayType>
void test_resource_limiter_initializer_list_constructor(ArrayType& resources,
                                                        tbb::detail::index_sequence<Idx...>) {
    CHECK(sizeof...(Idx) == resources.size());
    using value_type = typename ArrayType::value_type;
    std::initializer_list<value_type> init = {resources[Idx]...};
    test_resource_limiter_constructor(/*resource_values = */resources, /*args = */init);
} 

template <typename T, std::size_t N>
void test_resource_limiter_constructors(std::array<T, N>& resources) {
    auto index_sequence = tbb::detail::make_index_sequence<N>();

    // Test resource_limiter(std::piecewise_construct_t, Tuple&& tuple, Tuples&&... tuples)
    test_resource_limiter_handles_constructor(resources, index_sequence);

    // Test resource_limiter(std::initializer_list<ResourceHandle> init)
    test_resource_limiter_initializer_list_constructor(resources, index_sequence);

    // Test resource_limiter(InputIterator first, InputIterator last)
    test_resource_limiter_constructor(/*resource_values = */resources, /*args = */resources.begin(), resources.end());

    // Test resource_limiter(ContainerBasedSequence&& sequence)
    test_resource_limiter_constructor(/*resource_values = */resources, /*args = */resources);
}

//! \brief \ref interface
TEST_CASE("Feature test macro") {
    CHECK_MESSAGE(TBB_HAS_FLOW_GRAPH_RESOURCE_LIMITING == 202608, "Incorrect feature test macro");
}

//! \brief \ref interface
TEST_CASE("bases of resource_limited_node") {
    test_inheritance<int, std::tuple<>>();
    test_inheritance<int, std::tuple<int>>();
    test_inheritance<void*, std::tuple<float>>();
    test_inheritance<input_msg, std::tuple<output_msg>>();
}

//! \brief \ref requirement
TEST_CASE("test resource acquisition") {
    test_single_resource();
#if __TBB_RESUMABLE_TASKS
    test_several_resources();
#endif
}

template <typename Handle>
using limiter_unique_ptr = std::unique_ptr<oneapi::tbb::flow::resource_limiter<Handle>>;

template <std::size_t... Idx>
limiter_unique_ptr<std::size_t> get_limiter_impl(tbb::detail::index_sequence<Idx...>) {
    return limiter_unique_ptr<std::size_t>(new oneapi::tbb::flow::resource_limiter<std::size_t>({Idx...}));
}

template <std::size_t NumResources>
limiter_unique_ptr<std::size_t> get_limiter() {
    return get_limiter_impl(tbb::detail::make_index_sequence<NumResources>());
}

//! \brief \ref interface \ref requirement
TEST_CASE("resource_limited_node concurrency") {
    // For correct test behavior number of resources should be greater than number of threads in arena
    constexpr int num_threads = 50;
    auto limiter_ptr = get_limiter<num_threads + 1>();
    oneapi::tbb::task_arena arena(num_threads);

    arena.execute([&] {
        conformance::test_concurrency<oneapi::tbb::flow::resource_limited_node<int, std::tuple<int>>>(std::tie(*limiter_ptr));
    });
}

//! \brief \ref interface
TEST_CASE("resource_limited_node copy_body") {
    auto limiter_ptr = get_limiter<10>();
    using node_type = oneapi::tbb::flow::resource_limited_node<int, std::tuple<int>>;
    using body_type = conformance::copy_counting_object<int>;
    conformance::test_copy_body_function<node_type, body_type>(oneapi::tbb::flow::unlimited, std::tie(*limiter_ptr));
}

//! \brief \ref requirement
TEST_CASE("resource_limiter and resource_limited_node with strict_resource_handle") {
    test_strict_resource_handle();
}

//! \brief \ref interface \ref requirement
TEST_CASE("resource_limited_node copy constructor") {
    auto limiter_ptr = get_limiter<10>();
    using node_type = oneapi::tbb::flow::resource_limited_node<int, std::tuple<int>>;
    conformance::test_copy_ctor<node_type>(std::tie(*limiter_ptr));
}

//! \brief \ref requirement
TEST_CASE("resource_limited_node broadcast") {
    conformance::counting_functor<int> fun(conformance::expected);
    auto limiter_ptr = get_limiter<10>();
    using node_type = oneapi::tbb::flow::resource_limited_node<int, std::tuple<int>>;
    conformance::test_forwarding<node_type, input_msg, int>(1, oneapi::tbb::flow::unlimited, std::tie(*limiter_ptr), fun);
}

//! \brief \ref error_guessing
TEST_CASE("root-genie test for resource_limited_node") {
    test_root_genie();
}

//! \brief \ref requirement
TEST_CASE("resource_limiter request order") {
    test_request_order();
}

//! \brief \ref requirement
TEST_CASE("resource_limiter pressure does not affect order") {
    test_pressure_does_not_affect_order();
}

//! \brief \ref error_guessing
TEST_CASE("resource_limiter withdraw") {
    test_withdraw();
}

//! \brief \ref requirement
TEST_CASE("resource_limiter cross-provider agreement") {
    test_cross_provider_agreement();
}

//! \brief \ref error_guessing
TEST_CASE("resource_limited_node in a cycle contending for shared resources") {
    test_dining_ring();
}

#if __TBB_CPP17_INVOKE_PRESENT
//! \brief \ref interface \ref requirement
TEST_CASE("resource_limited_node and std::invoke") {
    using namespace oneapi::tbb::flow;

    using output_type1 = test_invoke::SmartID<std::size_t>;
    using input_type = test_invoke::SmartID<output_type1>;

    using output_tuple1 = std::tuple<output_type1, output_type1>;
    using output_tuple2 = std::tuple<std::size_t>;

    using first_rl_node_type = resource_limited_node<input_type, output_tuple1>;
    using second_rl_node_type = resource_limited_node<output_type1, output_tuple2>;

    using first_ports_type = typename first_rl_node_type::output_ports_type;
    using second_ports_type = typename second_rl_node_type::output_ports_type;

    graph g;
    auto first_body = &input_type::template send_id<first_ports_type, std::size_t&>;
    auto second_body = &output_type1::template send_id<second_ports_type, std::size_t&>;

    auto limiter_ptr = get_limiter<10>();

    first_rl_node_type rl1(g, unlimited, std::tie(*limiter_ptr), first_body);
    second_rl_node_type rl21(g, unlimited, std::tie(*limiter_ptr), second_body);
    second_rl_node_type rl22(g, unlimited, std::tie(*limiter_ptr), second_body);

    buffer_node<std::size_t> buf(g);

    make_edge(output_port<0>(rl1), rl21);
    make_edge(output_port<1>(rl1), rl21);

    make_edge(output_port<0>(rl21), buf);
    make_edge(output_port<0>(rl22), buf);

    rl1.try_put(input_type{output_type1{1}});

    g.wait_for_all();

    std::size_t buf_size = 0;
    std::size_t tmp = 0;
    while (buf.try_get(tmp)) {
        ++buf_size;
        CHECK(tmp == 1);
    }
    CHECK(buf_size == 2);
}
#endif // __TBB_CPP17_INVOKE_PRESENT

//! \brief \ref error_guessing
TEST_CASE("resource_limited_node cancellation with active requests") {
    test_cancellation_with_active_requests(/*same_graph = */false, /*exception =*/false);
    test_cancellation_with_active_requests(/*same_graph = */true, /*exception =*/false);
#if TBB_USE_EXCEPTIONS
    test_cancellation_with_active_requests(/*same_graph = */false, /*exception =*/true);
    test_cancellation_with_active_requests(/*same_graph = */true, /*exception =*/true);
#endif
}

//! \brief \ref requirement
TEST_CASE("concurrency limit with multiple resources") {
    using namespace oneapi::tbb::flow;

    // Number of resources exceeds concurrency limit
    resource_limiter<int> limiter{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    using node_type = resource_limited_node<int, std::tuple<>>;
    using ports_type = typename node_type::output_ports_type;

    const std::size_t max_concurrency = 2;
    std::atomic<std::size_t> concurrent_count{0};
    std::atomic<std::size_t> max_observed{0};

    auto node_body = [&](int /*input*/, ports_type&, int /*resource*/) {
        std::size_t current = ++concurrent_count;
        std::size_t prev_max = max_observed.load(std::memory_order_relaxed);
        while (current > prev_max &&
               !max_observed.compare_exchange_weak(prev_max, current,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed));

        // Busy wait for a bit
        for (std::size_t i = 0; i < 10000; ++i) {
            std::size_t count = concurrent_count.load(std::memory_order_relaxed);
            CHECK_MESSAGE(count <= max_concurrency,
                         "Too many concurrent executions: " << count << " > " << max_concurrency);
        }

        --concurrent_count;
    };

    graph g;
    node_type node(g, max_concurrency, std::tie(limiter), node_body);

    const int num_messages = 50;
    for (int i = 0; i < num_messages; ++i) {
        node.try_put(i);
    }

    g.wait_for_all();

    CHECK_MESSAGE(concurrent_count.load() == 0, "Final concurrent count should be zero");
    CHECK_MESSAGE(max_observed.load() <= max_concurrency,
                 "Concurrency limit was violated: max_observed=" << max_observed.load()
                 << " > max_concurrency=" << max_concurrency);

    CHECK_MESSAGE((max_observed.load() >= 2 || tbb::this_task_arena::max_concurrency() < 2),
                 "Expected to observe concurrent execution but max_observed=" << max_observed.load());
}

//! \brief \ref interface \ref requirement
TEST_CASE("resource_limiter constructors") {
    std::array<int, 1> one_resource = {1};
    test_resource_limiter_constructors(one_resource);

    std::array<int, 2> two_resources = {1, 2};
    test_resource_limiter_constructors(two_resources);

    std::array<int, 3> three_resources = {1, 2, 3};
    test_resource_limiter_constructors(three_resources);
}
