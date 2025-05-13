/*
    Copyright (c) 2005-2024 Intel Corporation

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

#ifndef __TBB_flow_graph_nodes_deduction_H
#define __TBB_flow_graph_nodes_deduction_H

#if __TBB_CPP17_DEDUCTION_GUIDES_PRESENT

namespace tbb {
namespace detail {
namespace d2 {

template <typename Input>
struct declare_input_type {
    using input_type = Input;
};

template <>
struct declare_input_type<d1::flow_control> {};

template <typename Input, typename Output>
struct body_types : declare_input_type<std::decay_t<Input>> {
    using output_type = std::decay_t<Output>;
};

template <typename P>
struct extract_member_function_types;

template <typename Body, typename Input, typename Output>
struct extract_member_function_types<Output (Body::*)(Input)> : body_types<Input, Output> {};

template <typename Body, typename Input, typename Output>
struct extract_member_function_types<Output (Body::*)(Input) const> : body_types<Input, Output> {};

// Body is represented as a callable object - extract types from the pointer to operator()
template <typename Body>
struct extract_body_types : extract_member_function_types<decltype(&Body::operator())> {};

// Body is represented as a pointer to function
template <typename Input, typename Output>
struct extract_body_types<Output (*)(Input)> : body_types<Input, Output> {};

// Body is represented as a pointer to member function
template <typename Input, typename Output>
struct extract_body_types<Output (Input::*)()> : body_types<Input, Output> {};

// Body is represented as a pointer to member object
template <typename Input, typename Output>
struct extract_body_types<Output Input::*> : body_types<Input, Output> {};

template <typename Body>
using input_type = typename extract_body_types<Body>::input_type;

template <typename Body>
using output_type = typename extract_body_types<Body>::output_type;

// Deduction guides for Flow Graph nodes

template <typename GraphOrSet, typename Body>
input_node(GraphOrSet&&, Body)
->input_node<output_type<Body>>;

#if __TBB_PREVIEW_FLOW_GRAPH_NODE_SET

template <typename NodeSet>
struct decide_on_set;

template <typename Node, typename... Nodes>
struct decide_on_set<node_set<order::following, Node, Nodes...>> {
    using type = typename Node::output_type;
};

template <typename Node, typename... Nodes>
struct decide_on_set<node_set<order::preceding, Node, Nodes...>> {
    using type = typename Node::input_type;
};

template <typename NodeSet>
using decide_on_set_t = typename decide_on_set<std::decay_t<NodeSet>>::type;

template <typename NodeSet>
broadcast_node(const NodeSet&)
->broadcast_node<decide_on_set_t<NodeSet>>;

template <typename NodeSet>
buffer_node(const NodeSet&)
->buffer_node<decide_on_set_t<NodeSet>>;

template <typename NodeSet>
queue_node(const NodeSet&)
->queue_node<decide_on_set_t<NodeSet>>;
#endif // __TBB_PREVIEW_FLOW_GRAPH_NODE_SET

template <typename GraphOrProxy, typename Sequencer>
sequencer_node(GraphOrProxy&&, Sequencer)
->sequencer_node<input_type<Sequencer>>;

#if __TBB_PREVIEW_FLOW_GRAPH_NODE_SET
template <typename NodeSet, typename Compare>
priority_queue_node(const NodeSet&, const Compare&)
->priority_queue_node<decide_on_set_t<NodeSet>, Compare>;

template <typename NodeSet>
priority_queue_node(const NodeSet&)
->priority_queue_node<decide_on_set_t<NodeSet>, std::less<decide_on_set_t<NodeSet>>>;
#endif // __TBB_PREVIEW_FLOW_GRAPH_NODE_SET

template <typename Key>
struct join_key {
    using type = Key;
};

template <typename T>
struct join_key<const T&> {
    using type = T&;
};

template <typename Key>
using join_key_t = typename join_key<Key>::type;

#if __TBB_PREVIEW_FLOW_GRAPH_NODE_SET
template <typename Policy, typename... Predecessors>
join_node(const node_set<order::following, Predecessors...>&, Policy)
->join_node<std::tuple<typename Predecessors::output_type...>,
            Policy>;

template <typename Policy, typename Successor, typename... Successors>
join_node(const node_set<order::preceding, Successor, Successors...>&, Policy)
->join_node<typename Successor::input_type, Policy>;

template <typename... Predecessors>
join_node(const node_set<order::following, Predecessors...>)
->join_node<std::tuple<typename Predecessors::output_type...>,
            queueing>;

template <typename Successor, typename... Successors>
join_node(const node_set<order::preceding, Successor, Successors...>)
->join_node<typename Successor::input_type, queueing>;
#endif

template <typename GraphOrProxy, typename Body, typename... Bodies>
join_node(GraphOrProxy&&, Body, Bodies...)
->join_node<std::tuple<input_type<Body>, input_type<Bodies>...>,
            key_matching<join_key_t<output_type<Body>>>>;

#if __TBB_PREVIEW_FLOW_GRAPH_NODE_SET
template <typename... Predecessors>
indexer_node(const node_set<order::following, Predecessors...>&)
->indexer_node<typename Predecessors::output_type...>;

template <typename NodeSet>
limiter_node(const NodeSet&, size_t)
->limiter_node<decide_on_set_t<NodeSet>>;

template <typename Predecessor, typename... Predecessors>
split_node(const node_set<order::following, Predecessor, Predecessors...>&)
->split_node<typename Predecessor::output_type>;

template <typename... Successors>
split_node(const node_set<order::preceding, Successors...>&)
->split_node<std::tuple<typename Successors::input_type...>>;

#endif

template <typename GraphOrSet, typename Body, typename Policy>
function_node(GraphOrSet&&,
              size_t, Body,
              Policy, node_priority_t = no_priority)
->function_node<input_type<Body>, output_type<Body>, Policy>;

template <typename GraphOrSet, typename Body>
function_node(GraphOrSet&&, size_t,
              Body, node_priority_t = no_priority)
->function_node<input_type<Body>, output_type<Body>, queueing>;

template <typename Output>
struct continue_output {
    using type = Output;
};

template <>
struct continue_output<void> {
    using type = continue_msg;
};

template <typename T>
using continue_output_t = typename continue_output<T>::type;

template <typename GraphOrSet, typename Body, typename Policy>
continue_node(GraphOrSet&&, Body,
              Policy, node_priority_t = no_priority)
->continue_node<continue_output_t<std::invoke_result_t<Body, continue_msg>>,
                Policy>;

template <typename GraphOrSet, typename Body, typename Policy>
continue_node(GraphOrSet&&,
              int, Body,
              Policy, node_priority_t = no_priority)
->continue_node<continue_output_t<std::invoke_result_t<Body, continue_msg>>,
                Policy>;

template <typename GraphOrSet, typename Body>
continue_node(GraphOrSet&&,
              Body, node_priority_t = no_priority)
->continue_node<continue_output_t<std::invoke_result_t<Body, continue_msg>>, Policy<void>>;

template <typename GraphOrSet, typename Body>
continue_node(GraphOrSet&&, int,
              Body, node_priority_t = no_priority)
->continue_node<continue_output_t<std::invoke_result_t<Body, continue_msg>>,
                Policy<void>>;

#if __TBB_PREVIEW_FLOW_GRAPH_NODE_SET

template <typename NodeSet>
overwrite_node(const NodeSet&)
->overwrite_node<decide_on_set_t<NodeSet>>;

template <typename NodeSet>
write_once_node(const NodeSet&)
->write_once_node<decide_on_set_t<NodeSet>>;
#endif // __TBB_PREVIEW_FLOW_GRAPH_NODE_SET
} // namespace d2
} // namespace detail
} // namespace tbb

#endif // __TBB_CPP17_DEDUCTION_GUIDES_PRESENT

#endif // __TBB_flow_graph_nodes_deduction_H
