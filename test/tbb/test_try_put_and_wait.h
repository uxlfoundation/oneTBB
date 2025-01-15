/*
    Copyright (c) 2024-2025 Intel Corporation

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

#ifndef __TBB_test_tbb_buffering_try_put_and_wait_H
#define __TBB_test_tbb_buffering_try_put_and_wait_H

#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/flow_graph.h>

#include <vector>

#if __TBB_PREVIEW_FLOW_GRAPH_TRY_PUT_AND_WAIT

namespace test_try_put_and_wait {

template <typename BufferingNode, typename... Args>
std::size_t test_buffer_push(const std::vector<int>& start_work_items,
                             int wait_message,
                             const std::vector<int>& new_work_items,
                             std::vector<int>& processed_items,
                             Args... args)
{
    std::size_t after_try_put_and_wait_start_index = 0;
    tbb::task_arena arena(1);

    arena.execute([&] {
        tbb::flow::graph g;

        using function_node_type = tbb::flow::function_node<int, int>;

        BufferingNode buffer1(g, args...);

        function_node_type function(g, tbb::flow::serial,
            [&](int input) noexcept {
                if (input == wait_message) {
                    for (auto item : new_work_items) {
                        buffer1.try_put(item);
                    }
                }
                return input;
            });

        BufferingNode buffer2(g, args...);

        function_node_type writer(g, tbb::flow::unlimited,
            [&](int input) noexcept {
                processed_items.emplace_back(input);
                return 0;
            });

        tbb::flow::make_edge(buffer1, function);
        tbb::flow::make_edge(function, buffer2);
        tbb::flow::make_edge(buffer2, writer);

        for (auto item : start_work_items) {
            buffer1.try_put(item);
        }

        buffer1.try_put_and_wait(wait_message);

        after_try_put_and_wait_start_index = processed_items.size();

        g.wait_for_all();
    });

    return after_try_put_and_wait_start_index;
}

template <typename BufferingNode, typename... Args>
std::size_t test_buffer_pull(const std::vector<int>& start_work_items,
                             int wait_message,
                             int occupier,
                             const std::vector<int>& new_work_items,
                             std::vector<int>& processed_items,
                             Args... args)
{
    tbb::task_arena arena(1);
    std::size_t after_try_put_and_wait_start_index = 0;

    arena.execute([&] {
        tbb::flow::graph g;

        using function_node_type = tbb::flow::function_node<int, int, tbb::flow::rejecting>;

        BufferingNode buffer(g, args...);

        function_node_type function(g, tbb::flow::serial,
            [&](int input) noexcept {
                if (input == wait_message) {
                    for (auto item : new_work_items) {
                        buffer.try_put(item);
                    }
                }

                processed_items.emplace_back(input);
                return 0;
            });

        // Occupy the concurrency of function_node
        // This call spawns the task to process the occupier
        function.try_put(occupier);

        // Make edge between buffer and function after occupying the concurrency
        // To ensure that forward task of the buffer would be spawned after the occupier task
        // And the function_node would reject the items from the buffer
        // and process them later by calling try_get on the buffer
        tbb::flow::make_edge(buffer, function);

        for (auto item : start_work_items) {
            buffer.try_put(item);
        }

        buffer.try_put_and_wait(wait_message);

        after_try_put_and_wait_start_index = processed_items.size();

        g.wait_for_all();
    });

    return after_try_put_and_wait_start_index;
}

template <typename BufferingNode, typename... Args>
std::size_t test_buffer_reserve(std::size_t limiter_threshold,
                                const std::vector<int>& start_work_items,
                                int wait_message,
                                const std::vector<int>& new_work_items,
                                std::vector<int>& processed_items,
                                Args... args)
{
    tbb::task_arena arena(1);
    std::size_t after_try_put_and_wait_start_index = 0;

    arena.execute([&] {
        tbb::flow::graph g;

        BufferingNode buffer(g, args...);

        tbb::flow::limiter_node<int, int> limiter(g, limiter_threshold);
        tbb::flow::function_node<int, int, tbb::flow::rejecting> function(g, tbb::flow::serial,
            [&](int input) {
                if (input == wait_message) {
                    for (auto item : new_work_items) {
                        buffer.try_put(item);
                    }
                }
                // Explicitly put to the decrementer instead of making edge
                // to guarantee that the next task would be spawned and not returned
                // to the current thread as the next task
                // Otherwise, all elements would be processed during the try_put_and_wait
                limiter.decrementer().try_put(1);
                processed_items.emplace_back(input);
                return 0;
            });

        tbb::flow::make_edge(buffer, limiter);
        tbb::flow::make_edge(limiter, function);

        for (auto item : start_work_items) {
            buffer.try_put(item);
        }

        buffer.try_put_and_wait(wait_message);

        after_try_put_and_wait_start_index = processed_items.size();

        g.wait_for_all();
    });

    return after_try_put_and_wait_start_index;
}

template <typename NodeType>
struct ports_or_gateway;

template <typename... Args>
struct ports_or_gateway<tbb::flow::multifunction_node<Args...>> {
    using type = typename tbb::flow::multifunction_node<Args...>::output_ports_type;
};

template <typename... Args>
struct ports_or_gateway<tbb::flow::async_node<Args...>> {
    using type = typename tbb::flow::async_node<Args...>::gateway_type;
};

template <typename NodeType>
using ports_or_gateway_t = typename ports_or_gateway<NodeType>::type;

template <typename Gateway, typename T, typename... Tag>
void put_to_ports_or_gateway(Gateway& gateway, const T& item, Tag&&... tag) {
    gateway.try_put(item, std::forward<Tag>(tag)...);
}

template <typename... Types, typename T, typename... Tag>
void put_to_ports_or_gateway(std::tuple<Types...>& ports, const T& item, Tag&&... tag) {
    std::get<0>(ports).try_put(item, std::forward<Tag>(tag)...);
}

template <typename NodeType>
void test_multioutput_tag_type() {
    static_assert(std::is_same<typename NodeType::input_type, int>::value, "Unexpected input type");
    using second_arg_type = ports_or_gateway_t<NodeType>;
    using tag_type = typename NodeType::tag_type;

    int processed = 0;

    tbb::flow::graph g;
    NodeType node(g, tbb::flow::unlimited,
        [&](int input, second_arg_type&, tag_type&& tag) {
            processed = input;
            tag_type tag1;
            tag_type tag2(std::move(tag));

            tag1 = std::move(tag2);
            tag = std::move(tag1);
        });

    node.try_put_and_wait(1);
    CHECK_MESSAGE(processed == 1, "Body wait not called in try_put_and_wait call");
    g.wait_for_all();
}

// TODO: add description
template <typename NodeType>
void test_multioutput_simple_broadcast() {
    static_assert(std::is_same<typename NodeType::input_type, int>::value, "Unexpected input type");
    tbb::task_arena arena(1);

    using funcnode_type = tbb::flow::function_node<int, int, tbb::flow::lightweight>;
    using second_argument_type = ports_or_gateway_t<NodeType>;
    using tag_type = typename NodeType::tag_type;

    arena.execute([&] {
        tbb::flow::graph g;

        std::vector<int> processed_items;
        std::vector<int> new_work_items;

        int wait_message = 10;

        for (int i = 0; i < wait_message; ++i) {
            new_work_items.emplace_back(i);
        }

        NodeType* start_node = nullptr;

        NodeType node(g, tbb::flow::unlimited,
            [&](int input, second_argument_type& port, tag_type&& tag) {
                if (input == wait_message) {
                    for (int item : new_work_items) {
                        start_node->try_put(item);
                    }
                }
                
                // Each even body execution copy-consumes the tag
                // each odd execution - move-consumes
                static bool copy_consume = true;

                if (copy_consume) {
                    put_to_ports_or_gateway(port, input, tag);
                } else {
                    put_to_ports_or_gateway(port, input, std::move(tag));
                }
                
                copy_consume = !copy_consume;
            });

        start_node = &node;

        funcnode_type next_func(g, tbb::flow::unlimited,
            [&](int input) noexcept {
                processed_items.emplace_back(input);
                return 0;
            });

        tbb::flow::make_edge(node, next_func);

        bool result = node.try_put_and_wait(wait_message);
        CHECK_MESSAGE(result, "unexpected try_put_and_wait result");

        CHECK(processed_items.size() == 1);
        CHECK_MESSAGE(processed_items[0] == wait_message, "Only the wait message should be processed by try_put_and_wait");

        g.wait_for_all();

        CHECK(processed_items.size() == new_work_items.size() + 1);

        std::size_t check_index = 1;
        for (std::size_t i = new_work_items.size(); i != 0; --i) {
            CHECK_MESSAGE(processed_items[check_index++] == new_work_items[i - 1], "Unexpected items processing order");
        }
        CHECK(check_index == processed_items.size());
    });
}

// TODO: add description
template <typename NodeType>
void test_multioutput_no_broadcast() {
    using second_argument_type = ports_or_gateway_t<NodeType>;
    using tag_type = typename NodeType::tag_type;

    std::size_t num_items = 10;
    std::size_t num_additional_items = 10;

    std::atomic<std::size_t> num_processed_items = 0;
    std::atomic<std::size_t> num_processed_accumulators = 0;

    int accumulator_message = 1;
    int add_message = 2;

    tag_type global_tag;

    NodeType* this_node = nullptr;

    std::vector<int> postprocessed_items;

    tbb::flow::graph g;
    NodeType node(g, tbb::flow::unlimited,
        [&](int input, second_argument_type& port, tag_type&& local_tag) {
            if (num_processed_items++ == 0) {
                CHECK(input == accumulator_message);
                ++num_processed_accumulators;

                global_tag = std::move(local_tag);
                for (std::size_t i = 1; i < num_items; ++i) {
                    this_node->try_put(accumulator_message);
                }
                for (std::size_t i = 0; i < num_additional_items; ++i) {
                    this_node->try_put(add_message);
                }
            } else {
                if (input == accumulator_message) {
                    global_tag.merge(std::move(local_tag));
                    if (num_processed_accumulators++ == num_items - 1) {
                        // The last accumulator was received - "cancel" the operation
                        global_tag.reset();
                    }
                } else {
                    put_to_ports_or_gateway(port, input);
                }
            }
        });

    this_node = &node;

    tbb::flow::function_node<int, int> write_node(g, tbb::flow::serial,
        [&](int value) noexcept { postprocessed_items.emplace_back(value); return 0; });

    tbb::flow::make_edge(tbb::flow::output_port<0>(node), write_node);

    node.try_put_and_wait(accumulator_message);

    CHECK_MESSAGE(num_processed_accumulators == num_items, "Unexpected number of accumulators processed");

    g.wait_for_all();

    CHECK_MESSAGE(num_processed_items == num_items + num_additional_items, "Unexpected number of items processed");
    CHECK_MESSAGE(postprocessed_items.size() == num_additional_items, "Unexpected number of items written");
    for (auto item : postprocessed_items) {
        CHECK_MESSAGE(item == add_message, "Unexpected item written");
    }
}

// TODO: add test description
template <typename NodeType>
void test_multioutput_reduction() {
    tbb::task_arena arena(1);

    arena.execute([]{
        int num_items = 5;
        tbb::flow::graph g;

        using func_node_type = tbb::flow::function_node<int, int>;
        using second_argument_type = ports_or_gateway_t<NodeType>;
        using tag_type = typename NodeType::tag_type;

        func_node_type* start_node = nullptr;

        func_node_type start(g, tbb::flow::unlimited,
            [&](int i) {
                static bool extra_work_added = false;
                if (!extra_work_added) {
                    extra_work_added = true;
                    for (int j = i + 1; j < i + num_items; ++j) {
                        start_node->try_put(j);
                    }
                }
                return i;
            });

        start_node = &start;

        int num_accumulated = 0;
        int accumulated_result = 0;
        tag_type accumulated_hint;

        std::vector<int> processed_items;

        NodeType node(g, tbb::flow::unlimited,
            [&](int i, second_argument_type& ports, const tag_type& tag) {
                ++num_accumulated;
                accumulated_result += i;
                accumulated_hint.merge(tag);

                if (num_accumulated == num_items) {
                    put_to_ports_or_gateway(ports, accumulated_result, std::move(accumulated_hint));
                    num_accumulated = 0;
                }
            });

        tbb::flow::function_node<int, int> writer(g, tbb::flow::unlimited,
            [&](int res) {
                // Start extra reduction that should not be handled by try_put_and_wait
                static bool extra_loop_added = false;

                if (!extra_loop_added) {
                    extra_loop_added = true;
                    for (int i = 100; i < 100 + num_items; ++i) {
                        node.try_put(i);
                    }
                }

                processed_items.emplace_back(res);
                return 0;
            });

        tbb::flow::make_edge(start, node);
        tbb::flow::make_edge(node, writer);

        start.try_put_and_wait(1);

        auto first_reduction_result = accumulated_result;
        CHECK_MESSAGE(processed_items.size() == 1, "More than one reduction was processed");
        CHECK_MESSAGE(processed_items[0] == first_reduction_result, "Unexpected reduction result");

        g.wait_for_all();

        CHECK_MESSAGE(processed_items.size() == 2, "More than one reduction was processed");
        CHECK_MESSAGE(accumulated_result != first_reduction_result, "Unexpected reduction result");
        CHECK_MESSAGE(processed_items[1] == accumulated_result, "Unexpected reduction result");
    });
}

template <typename NodeType>
void test_multioutput() {
    test_multioutput_tag_type<NodeType>();
    test_multioutput_simple_broadcast<NodeType>();
    test_multioutput_no_broadcast<NodeType>();
    test_multioutput_reduction<NodeType>();
}

} // test_try_put_and_wait

#endif // __TBB_PREVIEW_FLOW_GRAPH_TRY_PUT_AND_WAIT
#endif // __TBB_test_tbb_buffering_try_put_and_wait_H
