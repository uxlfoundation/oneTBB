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

#include <cstddef>
#include <tuple>

namespace retail {
    using OrderInformation = std::size_t;
    
    struct OrderInput {
        std::size_t customer_id;
        OrderInformation info;
    };
} // namespace retail

/*begin_type_specified_message_keys_example*/
#define TBB_PREVIEW_FLOW_GRAPH_FEATURES 1
#include <oneapi/tbb/flow_graph.h>

struct CustomerProfile {
    std::size_t id;
    std::size_t tier;
    std::size_t preferred_store_id;

    std::size_t key() const {
        return id;
    }
};

namespace retail {
    struct PurchaseData {
        std::size_t order_id;
        std::size_t customer_id;
        std::size_t amount;
    };

    template <typename Key>
    Key key_from_message(const PurchaseData& data) {
        return data.customer_id;
    }

    // Body of the node that fills the purchase data for the given order
    PurchaseData get_purchase_data(OrderInput order);
} // namespace retail

// Body of the node that gets or creates the customer profile by id
// For example, may read the associated customer database instance
CustomerProfile& get_customer_profile(std::size_t customer_id);

// Body of the final node that places the given order for a given customer
// For example, may write to the associated order database instance
void place_one_order(std::tuple<CustomerProfile&, retail::PurchaseData>);

struct SharedGraph {
    using customer_node_type = tbb::flow::function_node<std::size_t, CustomerProfile&>;
    using retail_node_type = tbb::flow::function_node<retail::OrderInput, retail::PurchaseData>;

    using join_tuple = std::tuple<CustomerProfile&, retail::PurchaseData>;
    using join_node_type = tbb::flow::join_node<join_tuple, tbb::flow::key_matching<std::size_t>>;

    using order_node_type = tbb::flow::function_node<join_tuple>;

    tbb::flow::graph   g;
    customer_node_type customer_node;
    retail_node_type   retail_node;
    join_node_type     join_node;
    order_node_type    order_node;

    SharedGraph()
        : customer_node(g, tbb::flow::unlimited, get_customer_profile)
        , retail_node(g, tbb::flow::unlimited, retail::get_purchase_data)
        , join_node(g)
        , order_node(g, tbb::flow::unlimited, place_one_order)
    {
        tbb::flow::make_edge(customer_node, tbb::flow::input_port<0>(join_node));
        tbb::flow::make_edge(retail_node, tbb::flow::input_port<1>(join_node));
        tbb::flow::make_edge(join_node, order_node);
    }
};

SharedGraph& get_shared_graph() {
    static SharedGraph shared_graph;
    return shared_graph;
}

void place_order(std::size_t customer_id, const retail::OrderInformation& order_info) {
    SharedGraph& graph = get_shared_graph();

    // Fetch customer information and purchase data in parallel
    graph.customer_node.try_put(customer_id);
    graph.retail_node.try_put(retail::OrderInput{customer_id, order_info});

    // join_node will use the default key_from_message to get customer id from the CustomerProfile
    //                and the custom retail::key_from_message to get customer id from the PurchaseData
}
/*end_type_specified_message_keys_example*/

#include <oneapi/tbb/concurrent_unordered_map.h>

namespace retail {
    PurchaseData get_purchase_data(OrderInput input) {
        return {/*id = */123, input.customer_id, input.info};
    }
}

CustomerProfile& get_customer_profile(std::size_t customer_id) {
    static tbb::concurrent_unordered_map<std::size_t, CustomerProfile> customer_profiles;
    auto it = customer_profiles.find(customer_id);
    if (it == customer_profiles.end()) {
        CustomerProfile profile{customer_id, 0, 0};
        it = customer_profiles.emplace(profile).first;
    }
    return it->second;
}

void place_one_order(std::tuple<CustomerProfile&, retail::PurchaseData>) { }

int main() {
    for (std::size_t customer_id = 0; customer_id < 5; ++customer_id) {
        for (std::size_t order = 0; order < 5; ++order) {
            place_order(customer_id, retail::OrderInformation{order});
        }
    }

    get_shared_graph().g.wait_for_all();
}
