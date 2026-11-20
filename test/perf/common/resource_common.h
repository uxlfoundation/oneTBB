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


#ifndef TBB_test_perf_resource_common_H
#define TBB_test_perf_resource_common_H


// Building blocks for the resource-limiting benchmarks: the busy-wait work
// model, a trivial counting resource, and jn_composite_node, the join-based
// implementation of a resource-limited node. Nothing here depends on
// USE_LIMITER_NODE, so a single benchmark can use jn_composite_node and
// resource_limiter side by side. A benchmark that uses resource_limiter must
// define TBB_PREVIEW_FLOW_GRAPH_RESOURCE_LIMITING before including this header.
//
// workflow_common.h includes this header and adds the USE_LIMITER_NODE variant
// switch and the rest of the harness that workflow_runner is built on.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "oneapi/tbb/flow_graph.h"
using namespace oneapi::tbb::flow;

// ============================================================================
// Work model -- busy-wait, not sleep_for
// ============================================================================

#if !(defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)) || \
    defined(_M_ARM64EC)
#  error "resource_common.h supports x86 (32- or 64-bit) only"
#endif

#if defined(_MSC_VER)
#  include <intrin.h>
#endif

inline void bench_pause() {
#if defined(_MSC_VER) && !defined(__clang__)
    _mm_pause();
#else
    // clang-cl defines _MSC_VER but does have the builtin, so it lands here.
    __builtin_ia32_pause();
#endif
}

// Spin until `us` microseconds of *monotonic* time have elapsed.
inline void busy_wait_us(double us) {
    if (!(us > 0.0)) return;
    using clock = std::chrono::steady_clock;
    const clock::time_point start = clock::now();
    const std::chrono::duration<double, std::micro> target(us);
    while (std::chrono::duration<double, std::micro>(clock::now() - start) < target) {
        bench_pause();
    }
}

// a trivial counting resource for testing purposes
struct counting_resource {
    std::atomic<std::size_t> counter;
    counting_resource() : counter(0) {}
    void use() {
        ++counter;
    }
};

// ----------------------------------------------------------------------------
// JN_BODY_POLICY -- the buffering policy of the jn_impl body node.
// This is a slightly misleading name, since this policy is for the 
// multifunction_node inside of the composite_node used for the join based
// implementation. The join_node itself in the composite_node implementation 
// is always reserving. But the multifunction_node body can be queueing or
// rejecting. When it's rejecting, it may avoid the eager resource consumption 
// issue. So being able to test either policy lets us evaluate the impact of
// the multifunction_node's buffering strategy on resource consumption.
// ----------------------------------------------------------------------------
#ifndef JN_BODY_POLICY
#define JN_BODY_POLICY queueing
#endif
using jn_body_policy = oneapi::tbb::flow::JN_BODY_POLICY;
static_assert(std::is_same_v<jn_body_policy, oneapi::tbb::flow::queueing> ||
                  std::is_same_v<jn_body_policy, oneapi::tbb::flow::rejecting>,
              "JN_BODY_POLICY must be queueing or rejecting");

// ============================================================================
// jn_impl: the join-based implementation
// ============================================================================

// buffer_node -> reserving join_node -> multifunction_node, presented as a single
// node with one message port and (tuple_size - 1) resource ports.
template <typename InputTuple, typename BodyPolicy = oneapi::tbb::flow::queueing>
class resource_composite_node
    : public oneapi::tbb::flow::composite_node<InputTuple, InputTuple> {
    using base_type = oneapi::tbb::flow::composite_node<InputTuple, InputTuple>;
    using mfn_type = multifunction_node<InputTuple, InputTuple, BodyPolicy>;

    static constexpr std::size_t tuple_size = std::tuple_size<InputTuple>::value;
    static_assert(tuple_size >= 2,
                  "resource_composite_node needs element 0 (the message) plus at "
                  "least one resource handle");

    // a buffer for the first type in the tuple
    buffer_node<std::tuple_element_t<0, InputTuple>> input_buffer;
    join_node<InputTuple, reserving> input_join;
    mfn_type body_node;

    // External port 0 is the message buffer; external ports 1..N-1 are the join's
    // resource inputs. Outputs map one-to-one onto the body's ports.
    template <std::size_t... Is>
    void wire_external_ports(std::index_sequence<Is...>) {
        base_type::set_external_ports(
            std::tie(input_buffer, input_port<Is + 1>(input_join)...),
            std::tie(output_port<0>(body_node), output_port<Is + 1>(body_node)...));
    }

public:
    using input_type = InputTuple;
    using input_ports_type = typename base_type::input_ports_type;
    // The body's ports, which is what a body lambda's second parameter is. Named
    // body_ports_type rather than output_ports_type because the base class
    // already means something different by that name (std::tuple<sender<T>&...>),
    // and shadowing it made `node.output_ports()` and `node_t::output_ports_type`
    // disagree.
    using body_ports_type = typename mfn_type::output_ports_type;
    using body_policy_type = BodyPolicy;

    // we assume that the body does the work of sending back the resources
    // we don't do this automatically
    template <typename BodyFunc>
    resource_composite_node(oneapi::tbb::flow::graph& g, size_t concurrency_limit,
                            BodyFunc body_func)
        : base_type(g),
          input_buffer(g),
          input_join(g),
          body_node(g, concurrency_limit, body_func) {
        // Connect the internal nodes: buffer -> join -> body
        make_edge(input_buffer, input_port<0>(input_join));
        make_edge(input_join, body_node);
        wire_external_ports(std::make_index_sequence<tuple_size - 1>{});
    }
};

template <typename InputTuple>
using jn_composite_node = resource_composite_node<InputTuple, jn_body_policy>;

#endif // TBB_test_perf_resource_common_H
