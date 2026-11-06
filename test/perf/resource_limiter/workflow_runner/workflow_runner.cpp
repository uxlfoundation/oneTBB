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

#include "workflow_runner.h"
#include <fstream>
#include <sstream>
#include <csignal>
#include <atomic>

// Signal handler for timeout - allows flushing trace on SIGTERM
#if USE_TRACE > 0
static std::atomic<TraceCollector*> g_trace_collector{nullptr};

void timeout_signal_handler(int signum) {
    std::fprintf(stderr, "\nReceived signal %d, flushing trace before exit...\n", signum);
    TraceCollector* tc = g_trace_collector.load();
    if (tc) {
        tc->flush();  // Only does work if LAZY mode
    }
    std::exit(128 + signum);
}
#else
void timeout_signal_handler(int signum) {
    std::fprintf(stderr, "\nReceived signal %d, exiting...\n", signum);
    std::exit(128 + signum);
}
#endif

// ============================================================================
// Simple JSON Parser for Workflow Files
// ============================================================================

// Simple JSON value class
class JsonValue {
public:
    enum class Type { Null, String, Number, Bool, Object, Array };

private:
    Type type_;
    std::string str_value_;
    double num_value_;
    bool bool_value_;
    std::map<std::string, JsonValue> obj_value_;
    std::vector<JsonValue> arr_value_;

public:
    JsonValue() : type_(Type::Null), num_value_(0), bool_value_(false) {}
    JsonValue(const std::string& s) : type_(Type::String), str_value_(s), num_value_(0), bool_value_(false) {}
    JsonValue(double d) : type_(Type::Number), num_value_(d), bool_value_(false) {}
    JsonValue(bool b) : type_(Type::Bool), num_value_(0), bool_value_(b) {}

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_string() const { return type_ == Type::String; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_object() const { return type_ == Type::Object; }
    bool is_array() const { return type_ == Type::Array; }

    std::string as_string() const { return str_value_; }
    double as_number() const { return num_value_; }
    bool as_bool() const { return bool_value_; }

    void set_object() { type_ = Type::Object; }
    void set_array() { type_ = Type::Array; }

    void add_member(const std::string& key, const JsonValue& val) {
        obj_value_[key] = val;
    }

    void add_element(const JsonValue& val) {
        arr_value_.push_back(val);
    }

    const JsonValue& operator[](const std::string& key) const {
        static JsonValue null_val;
        auto it = obj_value_.find(key);
        return it != obj_value_.end() ? it->second : null_val;
    }

    const JsonValue& operator[](size_t idx) const {
        static JsonValue null_val;
        return idx < arr_value_.size() ? arr_value_[idx] : null_val;
    }

    size_t size() const {
        if (is_array()) return arr_value_.size();
        if (is_object()) return obj_value_.size();
        return 0;
    }

    std::vector<std::string> keys() const {
        std::vector<std::string> result;
        for (const auto& [key, _] : obj_value_) {
            result.push_back(key);
        }
        return result;
    }

    const std::map<std::string, JsonValue>& items() const {
        return obj_value_;
    }

    const std::vector<JsonValue>& elements() const {
        return arr_value_;
    }
};

// Simple JSON parser
class JsonParser {
private:
    std::string json_;
    size_t pos_;

    void skip_whitespace() {
        while (pos_ < json_.size() && std::isspace(json_[pos_])) {
            ++pos_;
        }
    }

    JsonValue parse_value() {
        skip_whitespace();

        if (pos_ >= json_.size()) {
            return JsonValue();
        }

        char c = json_[pos_];

        if (c == '{') {
            return parse_object();
        } else if (c == '[') {
            return parse_array();
        } else if (c == '"') {
            return parse_string();
        } else if (c == 't' || c == 'f') {
            return parse_bool();
        } else if (c == 'n') {
            return parse_null();
        } else if (c == '-' || std::isdigit(c)) {
            return parse_number();
        }

        throw std::runtime_error("Unexpected character at position " + std::to_string(pos_));
    }

    JsonValue parse_object() {
        JsonValue obj;
        obj.set_object();

        ++pos_;  // Skip '{'
        skip_whitespace();

        if (pos_ < json_.size() && json_[pos_] == '}') {
            ++pos_;
            return obj;
        }

        while (pos_ < json_.size()) {
            skip_whitespace();

            // Parse key
            if (json_[pos_] != '"') {
                throw std::runtime_error("Expected string key");
            }
            std::string key = parse_string().as_string();

            skip_whitespace();
            if (pos_ >= json_.size() || json_[pos_] != ':') {
                throw std::runtime_error("Expected ':'");
            }
            ++pos_;

            // Parse value
            JsonValue val = parse_value();
            obj.add_member(key, val);

            skip_whitespace();
            if (pos_ >= json_.size()) break;

            if (json_[pos_] == ',') {
                ++pos_;
            } else if (json_[pos_] == '}') {
                ++pos_;
                break;
            }
        }

        return obj;
    }

    JsonValue parse_array() {
        JsonValue arr;
        arr.set_array();

        ++pos_;  // Skip '['
        skip_whitespace();

        if (pos_ < json_.size() && json_[pos_] == ']') {
            ++pos_;
            return arr;
        }

        while (pos_ < json_.size()) {
            JsonValue val = parse_value();
            arr.add_element(val);

            skip_whitespace();
            if (pos_ >= json_.size()) break;

            if (json_[pos_] == ',') {
                ++pos_;
            } else if (json_[pos_] == ']') {
                ++pos_;
                break;
            }
        }

        return arr;
    }

    JsonValue parse_string() {
        ++pos_;  // Skip opening '"'
        std::string result;

        while (pos_ < json_.size() && json_[pos_] != '"') {
            if (json_[pos_] == '\\' && pos_ + 1 < json_.size()) {
                ++pos_;
                char next = json_[pos_];
                if (next == 'n') result += '\n';
                else if (next == 't') result += '\t';
                else if (next == 'r') result += '\r';
                else if (next == '\\') result += '\\';
                else if (next == '"') result += '"';
                else result += next;
            } else {
                result += json_[pos_];
            }
            ++pos_;
        }

        if (pos_ < json_.size()) ++pos_;  // Skip closing '"'

        return JsonValue(result);
    }

    JsonValue parse_number() {
        size_t start = pos_;

        if (json_[pos_] == '-') ++pos_;

        while (pos_ < json_.size() && (std::isdigit(json_[pos_]) || json_[pos_] == '.' ||
                                        json_[pos_] == 'e' || json_[pos_] == 'E' ||
                                        json_[pos_] == '+' || json_[pos_] == '-')) {
            ++pos_;
        }

        std::string num_str = json_.substr(start, pos_ - start);
        return JsonValue(std::stod(num_str));
    }

    JsonValue parse_bool() {
        if (json_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return JsonValue(true);
        } else if (json_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return JsonValue(false);
        }
        throw std::runtime_error("Invalid boolean");
    }

    JsonValue parse_null() {
        if (json_.substr(pos_, 4) == "null") {
            pos_ += 4;
            return JsonValue();
        }
        throw std::runtime_error("Invalid null");
    }

public:
    JsonParser(const std::string& json) : json_(json), pos_(0) {}

    JsonValue parse() {
        return parse_value();
    }
};

// ============================================================================
// WorkflowMetadata::load_from_json Implementation
// ============================================================================

WorkflowMetadata WorkflowMetadata::load_from_json(const std::string& path) {
    // Read file
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open JSON file: " + path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json_str = buffer.str();

    // Parse JSON
    JsonParser parser(json_str);
    JsonValue root = parser.parse();

    WorkflowMetadata meta;

    // Parse algorithms
    const JsonValue& algos = root["algorithms"];
    if (algos.is_object()) {
        for (const auto& [name, algo_json] : algos.items()) {
            AlgorithmInfo info;
            info.name = name;
            info.execution_time_seconds = parse_execution_time(
                algo_json["estimated_execution_time"].as_string()
            );

            // Parse produces array
            const JsonValue& produces = algo_json["produces"];
            if (produces.is_array()) {
                for (size_t i = 0; i < produces.size(); ++i) {
                    info.produces.push_back(produces[i].as_string());
                }
            }

            // Parse consumes array
            const JsonValue& consumes = algo_json["consumes"];
            if (consumes.is_array()) {
                for (size_t i = 0; i < consumes.size(); ++i) {
                    info.consumes.push_back(consumes[i].as_string());
                }
            }

            // Parse resource_usage array
            const JsonValue& resources = algo_json["resource_usage"];
            if (resources.is_array()) {
                for (size_t i = 0; i < resources.size(); ++i) {
                    info.resource_usage.push_back(resources[i].as_string());
                }
            }

            meta.algorithms[name] = info;
        }
    }

    // Parse data products
    const JsonValue& products = root["data_products"];
    if (products.is_object()) {
        for (const auto& [name, prod_json] : products.items()) {
            DataProductInfo info;
            info.name = name;
            info.type = prod_json["type"].as_string();
            info.instance = prod_json["instance"].is_null() ? "" : prod_json["instance"].as_string();
            info.memory_usage_bytes = parse_memory_size(
                prod_json["estimated_memory_usage"].as_string()
            );
            meta.data_products[name] = info;
        }
    }

    // Parse limited resources
    const JsonValue& resources = root["limited resources"];
    if (resources.is_object()) {
        for (const auto& [name, _] : resources.items()) {
            meta.limited_resources.push_back(name);
        }
    }

    // Analyze graph structure
    meta.analyze();

    std::cout << "Loaded workflow from " << path << ":\n";
    std::cout << "  Algorithms: " << meta.algorithms.size() << "\n";
    std::cout << "  Data products: " << meta.data_products.size() << "\n";
    std::cout << "  Resources: " << meta.limited_resources.size() << "\n";
    std::cout << "  External inputs: " << meta.external_inputs.size() << "\n";
    std::cout << "  Total workflow memory: "
              << (meta.total_workflow_memory / (1024.0 * 1024.0)) << " MB\n";

    return meta;
}

// ============================================================================
// Graph Construction
// ============================================================================

// Type aliases

// The message type received / generated by an algorithm node is a map from data 
// product names to shared pointers of DataProduct instances.
using AlgorithmOutput = std::map<std::string, std::shared_ptr<DataProduct>>;

// Forward declaration of input_generator type for DataProductDeleter
using InputGeneratorType = oneapi::tbb::flow::multifunction_node<int64_t, std::tuple<AlgorithmOutput>>;
static InputGeneratorType* g_input_generator = nullptr;

// The most resources one algorithm may declare.
// Set based on actual workflows to limit compile-time versions that need to be created.
// Can be increased if needed with matching changes create_algorithm_node specializations.
constexpr std::size_t MAX_RESOURCE_ARITY = 2;

// The most message sources one algorithm may have, i.e. the widest join.
// Set based on actual workflows to limit compile-time versions that need to be created.
// Can be increased if needed
constexpr std::size_t MAX_JOIN_ARITY = 4;

// The concurrency of every resource-limited algorithm node, in both variants.
//
// NOTE: All workflows have a single resource per limiter and so flow::serial
//       concurrency is sufficient and preferred to avoid unnecessary contention.
//       To change this, the workflow JSON file would need to specify a different
//       concurrency level and have limiters that also support more than 1 resource.
//
// It must stay bounded: validate_workflow_wiring's refusal of an inert rejecting
// jn_impl build relies on it.
constexpr std::size_t RESOURCE_NODE_CONCURRENCY = oneapi::tbb::flow::serial;

template <std::size_t /*I*/>
using handle_t = counting_resource*;
template <std::size_t /*I*/>
using message_t = AlgorithmOutput;

template <std::size_t... Is>
std::tuple<AlgorithmOutput, handle_t<Is>...>
resource_tuple_of(std::index_sequence<Is...>);
template <std::size_t NRes>
using resource_tuple =
    decltype(resource_tuple_of(std::make_index_sequence<NRes>{}));

template <std::size_t... Is>
std::tuple<message_t<Is>...> join_tuple_of(std::index_sequence<Is...>);
template <std::size_t N>
using join_tuple = decltype(join_tuple_of(std::make_index_sequence<N>{}));

static std::atomic<std::uint64_t> g_resource_use_total{0};

#if USE_LIMITER_NODE == 0
using LimiterMap =
    std::map<std::string, oneapi::tbb::flow::buffer_node<counting_resource*>*>;
#else
using LimiterMap = std::map<std::string, limiter_type<counting_resource*>*>;
#endif

// A mode- and arity-independent view of one algorithm node's ports.
struct AlgoPorts {
    // Where a message enters this algorithm, and where its output leaves.
    oneapi::tbb::flow::receiver<AlgorithmOutput>* msg_in = nullptr;
    oneapi::tbb::flow::sender<AlgorithmOutput>* msg_out = nullptr;

#if USE_LIMITER_NODE == 0
    std::vector<oneapi::tbb::flow::receiver<counting_resource*>*> handle_in;
    std::vector<oneapi::tbb::flow::sender<counting_resource*>*> handle_out;
#endif
};

// Everything an algorithm body needs that is not its input message.
struct AlgoBodyContext {
    int algo_tid;
    double exec_time;
    const AlgorithmInfo& algo_info;
    const WorkflowMetadata& metadata;
#if USE_TRACE > 0
    TraceCollector* trace_collector;
    TraceLevel trace_level;
#endif
};

// The one place an algorithm's work is done and its outputs are constructed.
template <typename Publish, typename... Handles>
void execute_algorithm(const AlgoBodyContext& ctx, const AlgorithmOutput& input,
                             Publish publish, Handles*... handles) {
    // Extract message_id from first input product (all should have same message_id)
    const int message_id = input.empty() ? 0 : input.begin()->second->message_id;

#if USE_TRACE > 0
    // Create scoped trace event for algorithm execution
    std::unique_ptr<ScopedTraceEvent> trace;
    if (ctx.trace_collector) {
        std::string event_name = "input_" + std::to_string(message_id);
        trace = std::make_unique<ScopedTraceEvent>(*ctx.trace_collector, event_name,
                                                   ctx.algo_tid);
    }
#endif

    // Use the resources.
    (handles->use(), ...);

    // Simulate algorithm execution.
    busy_wait_us(ctx.exec_time * 1.0e6);

    // Create output data products
    AlgorithmOutput outputs;
    for (const auto& product_name : ctx.algo_info.produces) {
        const auto& product_info = ctx.metadata.data_products.at(product_name);

        outputs[product_name] = std::shared_ptr<DataProduct>(
            new DataProduct(
                product_name,
                product_info.type,
                product_info.instance,
                product_info.memory_usage_bytes,
                message_id,
#if USE_TRACE > 0
                ctx.algo_tid,  // Algorithm-specific thread ID
                ctx.trace_collector,
                ctx.trace_level
#else
                ctx.algo_tid,
                nullptr,
                TraceLevel::OFF
#endif
            ),
            DataProductDeleter(g_input_generator)
        );
    }

    publish(outputs);
}

// ============================================================================
// Algorithm node factories
// ============================================================================

#if USE_LIMITER_NODE == 0

template <std::size_t... Is>
AlgoPorts make_algorithm_resource_node(std::index_sequence<Is...>,
                                       oneapi::tbb::flow::graph& g,
                                       const AlgoBodyContext& ctx,
                                       const std::vector<LimiterMap::mapped_type>& limiters) {
    using tuple_type = resource_tuple<sizeof...(Is)>;
    using node_type = jn_composite_node<tuple_type>;

    (void)limiters;

    auto composite_body = [ctx](const tuple_type& input_tuple,
                                typename node_type::body_ports_type& ports) {
        execute_algorithm(
            ctx, /*the input data product*/ std::get<0>(input_tuple),
            [&](AlgorithmOutput& outputs) {
                // put the output data products to the output port
                std::get<0>(ports).try_put(outputs);
                // put the resource handles to the buffers:
                (std::get<Is + 1>(ports).try_put(std::get<Is + 1>(input_tuple)), ...);
            },
            /* the resource handles */ std::get<Is + 1>(input_tuple)...);
    };

    auto* node = new node_type(g, RESOURCE_NODE_CONCURRENCY, composite_body);

    AlgoPorts ports;
    ports.msg_in = &std::get<0>(node->input_ports());
    ports.msg_out = &std::get<0>(node->output_ports());
    (ports.handle_in.push_back(&std::get<Is + 1>(node->input_ports())), ...);
    (ports.handle_out.push_back(&std::get<Is + 1>(node->output_ports())), ...);
    return ports;
}

#else  // limiter_impl

// limiter_impl: one node type for every arity.
template <std::size_t... Is>
AlgoPorts make_algorithm_resource_node(std::index_sequence<Is...>,
                                       oneapi::tbb::flow::graph& g,
                                       const AlgoBodyContext& ctx,
                                       const std::vector<LimiterMap::mapped_type>& limiters) {
    using node_type =
        resource_limited_node<AlgorithmOutput, std::tuple<AlgorithmOutput>>;

    auto limited_body = [ctx](const AlgorithmOutput& input,
                              typename node_type::output_ports_type& ports,
                              handle_t<Is>... handles) {
        execute_algorithm(
            ctx, input,
            [&](AlgorithmOutput& outputs) { std::get<0>(ports).try_put(outputs); },
            handles...);
    };

    auto* node = new node_type(g, RESOURCE_NODE_CONCURRENCY,
                               std::tie(*limiters[Is]...), limited_body);

    AlgoPorts ports;
    ports.msg_in = node;
    ports.msg_out = &std::get<0>(node->output_ports());
    return ports;
}

#endif

// Algorithm node factory.
//
// Returns the node's ports rather than the node. Nothing owns the node: it is
// leaked.  TBD: fix leak if necessary.
AlgoPorts
create_algorithm_node(
    oneapi::tbb::flow::graph& g,
    const std::string& algo_name,
    const AlgorithmInfo& algo_info,
    const WorkflowMetadata& metadata,
    const WorkflowConfig& config,
    const LimiterMap& resource_limiters
#if USE_TRACE > 0
    , TraceCollector* trace_collector
#endif
) {
    using namespace oneapi::tbb::flow;

    const AlgoBodyContext ctx{
        metadata.algorithm_to_tid.at(algo_name),
        algo_info.execution_time_seconds,
        algo_info,
        metadata
#if USE_TRACE > 0
        , trace_collector,
        config.trace_level
#endif
    };
#if USE_TRACE == 0
    (void)config;
#endif

    if (algo_info.resource_usage.empty()) {
        // No resources - use function_node with unlimited concurrency
        // All non resource constrained algorithms have unlimited concurrency
        // To change this the workflow JSON file would need to specify
        // concurrency levels for non resource constrained algorithms.
        auto* node = new function_node<AlgorithmOutput, AlgorithmOutput>(
            g, unlimited, [ctx](const AlgorithmOutput& input) -> AlgorithmOutput {
                AlgorithmOutput result;
                execute_algorithm(ctx, input, [&result](AlgorithmOutput& outputs) {
                    result = std::move(outputs);
                });
                return result;
            });

        AlgoPorts ports;
        ports.msg_in = node;
        ports.msg_out = node;
        return ports;
    }

    std::vector<LimiterMap::mapped_type> limiters;
    for (const auto& resource_name : algo_info.resource_usage) {
        auto it = resource_limiters.find(resource_name);
        if (it == resource_limiters.end()) {
            throw std::runtime_error("Algorithm " + algo_name +
                                     " declares unknown resource " + resource_name);
        }
        limiters.push_back(it->second);
    }

    // NOTE: Every permitted arity is instantiated, if the MAX_RESOURCE_ARITY
    // is updated, the switch statement below must be extended accordingly.
    static_assert(MAX_RESOURCE_ARITY == 2,
                  "add a case below for each newly permitted resource arity");
    switch (limiters.size()) {
    case 1:
        return make_algorithm_resource_node(std::make_index_sequence<1>{}, g, ctx, limiters);
    case 2:
        return make_algorithm_resource_node(std::make_index_sequence<2>{}, g, ctx, limiters);
    default:
        // validate_workflow_wiring has already refused this workflow by name, before
        // any graph was built. This is the backstop, not the diagnostic.
        bench_die("algorithm " + algo_name + " declares " +
                  std::to_string(limiters.size()) + " resources; the graph builder "
                  "instantiates arities 1.." +
                  std::to_string(MAX_RESOURCE_ARITY) +
                  " (MAX_RESOURCE_ARITY)");
    }
}

// ============================================================================
// Message sources: the mixed-input fix
// ============================================================================

// Where an algorithm's input message comes from.
struct MsgSource {
    // Empty for the input_generator; an algorithm name otherwise.
    std::string producer;
    // Which external products to forward. Empty means "the whole bundle", which is
    // what a generator-only algorithm gets
    std::vector<std::string> external_filter;

    bool from_generator() const { return producer.empty(); }
};

// An algorithm's message sources, in port order.
//
// Internal producers first, sorted by name. The generator goes LAST
// when it appears alongside producers.
std::vector<MsgSource> message_sources(const WorkflowMetadata& metadata,
                                       const AlgorithmInfo& algo_info) {
    std::set<std::string> producers;
    std::set<std::string> externals;
    for (const auto& consumed : algo_info.consumes) {
        auto it = metadata.product_to_producer.find(consumed);
        if (it != metadata.product_to_producer.end()) {
            producers.insert(it->second);
        } else {
            externals.insert(consumed);
        }
    }

    std::vector<MsgSource> sources;
    for (const auto& producer : producers) {
        sources.push_back(MsgSource{producer, {}});
    }

    if (producers.empty()) {
        // Generator-only, which includes an algorithm that consumes nothing at all.
        // The whole external bundle, unfiltered, exactly as before.
        sources.push_back(MsgSource{{}, {}});
    } else if (!externals.empty()) {
        // Mixed: the generator becomes the last join port, carrying only this
        // algorithm's declared externals.
        sources.push_back(
            MsgSource{{}, std::vector<std::string>(externals.begin(), externals.end())});
    }
    return sources;
}

// Refuse a workflow the graph builder cannot wire, before it builds any of it.
void validate_workflow_wiring(const WorkflowMetadata& metadata) {
    std::vector<std::string> over_resource;
    std::vector<std::string> over_join;
    std::size_t max_resource_arity = 0;
    std::size_t max_message_sources = 0;
    std::size_t mixed_algorithms = 0;
    std::size_t mixed_edges = 0;

    for (const auto& [algo_name, algo_info] : metadata.algorithms) {
        const std::size_t arity = algo_info.resource_usage.size();
        max_resource_arity = std::max(max_resource_arity, arity);
        if (arity > MAX_RESOURCE_ARITY) {
            over_resource.push_back(algo_name + " (" + std::to_string(arity) + ")");
        }

        const auto sources = message_sources(metadata, algo_info);
        max_message_sources = std::max(max_message_sources, sources.size());
        if (sources.size() > MAX_JOIN_ARITY) {
            over_join.push_back(algo_name + " (" + std::to_string(sources.size()) + ")");
        }
        if (sources.size() > 1 && sources.back().from_generator()) {
            ++mixed_algorithms;
            mixed_edges += sources.back().external_filter.size();
        }
    }

    bench_record().set("max_resource_arity", static_cast<int>(max_resource_arity));
    bench_record().set("max_message_sources", static_cast<int>(max_message_sources));
    bench_record().set("mixed_input_algorithms", static_cast<int>(mixed_algorithms));
    bench_record().set("mixed_input_edges", static_cast<int>(mixed_edges));

#if USE_LIMITER_NODE == 0
    // Refuse an inert rejecting build.
    //
    // `rejecting` is only consulted when a body has a bounded concurrency:
    // function_input_base::try_put_task_impl short-circuits at max_concurrency == 0
    // before the policy is ever read. jn_body_policy reaches only the composite
    // nodes of resource-limited algorithms, all built at the bounded
    // RESOURCE_NODE_CONCURRENCY, so the policy has an effect exactly when at least
    // one algorithm declares a resource. Without one, a rejecting run would be a
    // byte-identical rerun of the queueing build, recorded as a different one.
    static_assert(RESOURCE_NODE_CONCURRENCY != oneapi::tbb::flow::unlimited,
                  "rejecting is a no-op on an unlimited body; see the check below");
    if constexpr (std::is_same_v<jn_body_policy, oneapi::tbb::flow::rejecting>) {
        if (max_resource_arity == 0) {
            bench_die("jn_impl was built with JN_BODY_POLICY=rejecting, but no "
                      "algorithm in this workflow declares a resource, so there is no "
                      "body for the policy to apply to. Measure this workflow with the "
                      "default (queueing) build.");
        }
    }
#endif

    if (over_resource.empty() && over_join.empty()) return;

    std::string message = "this workflow needs graph shapes the builder does not "
                          "instantiate.";
    if (!over_resource.empty()) {
        message += " Resource arity > " + std::to_string(MAX_RESOURCE_ARITY) +
                   " (MAX_RESOURCE_ARITY), which is a switch case in "
                   "create_algorithm_node, for:";
        for (const auto& who : over_resource) message += " " + who;
        message += ".";
    }
    if (!over_join.empty()) {
        message += " Message sources > " + std::to_string(MAX_JOIN_ARITY) +
                   " (MAX_JOIN_ARITY), which is a case in "
                   "with_join_arity, for:";
        for (const auto& who : over_join) message += " " + who;
        message += ".";
    }
    bench_die(message);
}

// The key joins match on.
struct message_key {
    int operator()(const AlgorithmOutput& output) const {
        return output.empty() ? -1 : output.begin()->second->message_id;
    }
};

// N keyed message sources in, one merged AlgorithmOutput out: a key_matching join
// of arity N and the merge node that flattens its tuple, packaged as one node.
template <std::size_t N>
class keyed_merge_node
    : public oneapi::tbb::flow::composite_node<join_tuple<N>, std::tuple<AlgorithmOutput>> {
    using base_type =
        oneapi::tbb::flow::composite_node<join_tuple<N>, std::tuple<AlgorithmOutput>>;
    using join_type =
        oneapi::tbb::flow::join_node<join_tuple<N>, oneapi::tbb::flow::key_matching<int>>;
    using merge_type = oneapi::tbb::flow::function_node<join_tuple<N>, AlgorithmOutput,
                                                        oneapi::tbb::flow::lightweight>;

    join_type my_join;
    merge_type my_merge;

    template <std::size_t... Is>
    static join_type make_join(oneapi::tbb::flow::graph& g, std::index_sequence<Is...>) {
        // N copies of the same key function, one per port.
        return join_type(g, (void(Is), message_key{})...);
    }

    // Built port by port rather than passed join.input_ports() whole: before C++23,
    // std::tuple cannot convert a tuple of the join's port objects into the
    // composite's tuple of receiver references.
    template <std::size_t... Is>
    void set_ports(std::index_sequence<Is...>) {
        typename base_type::input_ports_type inputs(
            oneapi::tbb::flow::input_port<Is>(my_join)...);
        typename base_type::output_ports_type outputs(my_merge);
        base_type::set_external_ports(inputs, outputs);
    }

public:
    explicit keyed_merge_node(oneapi::tbb::flow::graph& g)
        : base_type(g),
          my_join(make_join(g, std::make_index_sequence<N>{})),
          my_merge(g, oneapi::tbb::flow::unlimited, [](const join_tuple<N>& tuple_in) {
              AlgorithmOutput merged;
              std::apply(
                  [&merged](const auto&... part) {
                      (merged.insert(part.begin(), part.end()), ...);
                  },
                  tuple_in);
              return merged;
          }) {
        oneapi::tbb::flow::make_edge(my_join, my_merge);
        set_ports(std::make_index_sequence<N>{});
        base_type::add_nodes(my_join, my_merge);
    }

    // One receiver per join port, in port order.
    std::vector<oneapi::tbb::flow::receiver<AlgorithmOutput>*> inputs() {
        return std::apply(
            [](auto&... port) {
                return std::vector<oneapi::tbb::flow::receiver<AlgorithmOutput>*>{&port...};
            },
            base_type::input_ports());
    }
};

// Runtime arity -> compile-time arity
template <typename F>
auto with_join_arity(std::size_t arity, F&& f) {
    static_assert(MAX_JOIN_ARITY == 4,
                  "add a case below for each newly permitted join arity");
    switch (arity) {
    case 2: return f(std::integral_constant<std::size_t, 2>{});
    case 3: return f(std::integral_constant<std::size_t, 3>{});
    case 4: return f(std::integral_constant<std::size_t, 4>{});
    default:
        // validate_workflow_wiring has already refused this workflow, by name and
        // before any graph was built. This is the backstop, not the diagnostic.
        bench_die("an algorithm has " + std::to_string(arity) +
                  " message sources; joins of arity 2.." +
                  std::to_string(MAX_JOIN_ARITY) +
                  " are instantiated (MAX_JOIN_ARITY)");
    }
}

// Run workflow trials
std::tuple<std::chrono::duration<double>, std::chrono::duration<double>, SteadyWindow>
run_workflow(
    const WorkflowMetadata& metadata,
    const WorkflowConfig& config,
    uint64_t total_memory_bytes,
    int admission_window_c
#if USE_TRACE > 0
    , TraceCollector* trace_collector
#endif
) {
    using namespace oneapi::tbb::flow;

    auto start_construction = std::chrono::high_resolution_clock::now();

    // State for memory accounting (serial concurrency - no atomics needed)
    uint64_t available_memory = 0;
    int messages_sent = 0;
    const int max_messages = config.num_inputs;

    // Create graph
    graph g;

    // Create input generator as serial multifunction_node
    // Note: Using int64_t for compatibility with try_put (TBB nodes use int-sized messages)
    using input_gen_output = std::tuple<AlgorithmOutput>;
    multifunction_node<int64_t, input_gen_output> input_generator(
        g,
        serial,  // Single invocation at a time
        [&, max_messages, total_wf_mem = metadata.total_workflow_memory]
        (int64_t freed_bytes, multifunction_node<int64_t, input_gen_output>::output_ports_type& ports) {
            // Add freed memory to available pool
            if (freed_bytes > 0) {
                available_memory += static_cast<uint64_t>(freed_bytes);
            }

            // Generate as many messages as we can right now
            while (messages_sent < max_messages &&
                   available_memory >= total_wf_mem) {

                // Reserve memory for this message
                available_memory -= total_wf_mem;

                // Create message ID and external inputs map
                int message_id = messages_sent++;

                // THE STAMP SITE. The input_generator is `serial`, so this runs on
                // one thread at a time and needs no more ordering than stamp()
                // already has. Placed before the products are constructed, because
                // the interval being measured is the lifetime of this message's
                // memory: from the moment the budget is committed to it here, to
                // the moment its last product hands the bytes back.
                //
                // Note what is NOT measured: the wait to be admitted. A message
                // that cannot fit in the remaining budget does not exist yet -- it
                // has no id and no arrival time, because injection is an
                // admission window rather than an arrival process. Section 7 named
                // that window (C, checked in main) and left the queue in front of it
                // unmeasured on purpose: there is no queue, only a budget, and the
                // work not yet admitted has no arrival time to be late relative to.
                bench_latency().stamp(message_id);

                // THE ADMISSION TIME on an absolute clock. Same instant, different
                // shape: stamp() above starts an interval, this records a point.
                // Together over the steady set they give the admission rate, and the
                // admission rate divided by the completion rate is the direct evidence
                // that the window bound -- a dynamic companion to the static C >=
                // num_inputs check, which can only look at the configuration.
                MessageClock::on_admit(message_id);

                AlgorithmOutput external_inputs;

#if USE_TRACE > 0
                // Record instant event for latency calculation (in both MINIMAL and FULL)
                if (trace_collector) {
                    trace_collector->record_instant_event(
                        "latency_start_input_" + std::to_string(message_id),
                        0  // tid=0 for source events
                    );
                }
#endif

                // Populate external inputs (products with no producer)
                for (const auto& product_name : metadata.external_inputs) {
                    const auto& prod_info = metadata.data_products.at(product_name);

#if USE_TRACE > 0
                    DataProduct* dp = new DataProduct(
                        product_name,
                        prod_info.type,
                        prod_info.instance,
                        prod_info.memory_usage_bytes,
                        message_id,
                        0,  // tid=0 for input_generator-created products
                        trace_collector,
                        config.trace_level  // Pass trace level for DataProduct events
                    );
#else
                    DataProduct* dp = new DataProduct(
                        product_name,
                        prod_info.type,
                        prod_info.instance,
                        prod_info.memory_usage_bytes,
                        message_id,
                        0,
                        nullptr,
                        TraceLevel::OFF
                    );
#endif

                    external_inputs[product_name] = std::shared_ptr<DataProduct>(
                        dp,
                        DataProductDeleter(g_input_generator)
                    );
                }

                // Send external inputs into the workflow
                std::get<0>(ports).try_put(external_inputs);
            }
        }
    );

    // Set global pointer for DataProductDeleter callbacks
    g_input_generator = &input_generator;

    // Create counting_resource instances for each resource
    std::vector<counting_resource> resources(metadata.limited_resources.size());

#if USE_LIMITER_NODE == 0
    LimiterMap resource_limiters;
    for (size_t i = 0; i < metadata.limited_resources.size(); ++i) {
        auto* buffer = new buffer_node<counting_resource*>(g);
        buffer->try_put(&resources[i]);
        resource_limiters[metadata.limited_resources[i]] = buffer;
    }
#else  // limiter_impl
    // Create resource_limiter for each resource.
    LimiterMap resource_limiters;
    for (size_t i = 0; i < metadata.limited_resources.size(); ++i) {
        auto* limiter = new limiter_type<counting_resource*>{&resources[i]};
        resource_limiters[metadata.limited_resources[i]] = limiter;
    }
#endif

    // Create algorithm nodes in topological order.
    std::map<std::string, AlgoPorts> algorithm_nodes;

    for (const auto& algo_name : metadata.topological_order) {
        const auto& algo_info = metadata.algorithms.at(algo_name);

        algorithm_nodes[algo_name] = create_algorithm_node(
            g, algo_name, algo_info, metadata, config, resource_limiters
#if USE_TRACE > 0
            , trace_collector
#endif
        );
    }

    // Where each algorithm's input message comes from, in port order.
    std::map<std::string, std::vector<MsgSource>> algo_sources;

    for (const auto& algo_name : metadata.topological_order) {
        const auto& algo_info = metadata.algorithms.at(algo_name);
        auto sources = message_sources(metadata, algo_info);

        if (sources.size() > 1) {
            std::cout << "  Algorithm " << algo_name << " needs join from "
                      << sources.size() << " sources: ";
            for (const auto& source : sources) {
                std::cout << (source.from_generator() ? "(generator)" : source.producer)
                          << " ";
            }
            std::cout << "\n";
        }

        algo_sources[algo_name] = std::move(sources);
    }

    // Where each algorithm's messages enter: one receiver per message source, in
    // port order.
    std::map<std::string, std::vector<receiver<AlgorithmOutput>*>> msg_inputs;

    for (const auto& entry : algo_sources) {
        const std::string& algo_name = entry.first;
        receiver<AlgorithmOutput>& algo_in = *algorithm_nodes.at(algo_name).msg_in;
        if (entry.second.size() < 2) {
            msg_inputs[algo_name] = {&algo_in};
            continue;
        }
        with_join_arity(entry.second.size(), [&](auto arity) {
            auto* joined = new keyed_merge_node<decltype(arity)::value>(g);
            make_edge(output_port<0>(*joined), algo_in);
            msg_inputs[algo_name] = joined->inputs();
        });
    }

    std::vector<function_node<AlgorithmOutput, AlgorithmOutput>*> filter_nodes;
    sender<AlgorithmOutput>* generator_out = &output_port<0>(input_generator);

    for (const auto& algo_name : metadata.topological_order) {
        const auto& sources = algo_sources.at(algo_name);

        for (std::size_t port = 0; port < sources.size(); ++port) {
            const MsgSource& source = sources[port];
            if (!source.from_generator()) continue;

            sender<AlgorithmOutput>* src = generator_out;

            if (!source.external_filter.empty()) {
                for (const auto& product_name : source.external_filter) {
                    if (!metadata.external_inputs.count(product_name)) {
                        throw std::runtime_error(
                            "Algorithm " + algo_name + " consumes " + product_name +
                            ", which has no producer and is not an external input"
                        );
                    }
                }

                // NOTE: this is unlimited and not lightweight, to avoid running it in the
                // generator's serial body.
                auto* filter = new function_node<AlgorithmOutput, AlgorithmOutput>(
                    g, unlimited,
                    [filter_list = source.external_filter](const AlgorithmOutput& bundle) {
                        AlgorithmOutput selected;
                        for (const auto& product_name : filter_list) {
                            auto it = bundle.find(product_name);
                            if (it != bundle.end()) selected.insert(*it);
                        }
                        return selected;
                    });
                filter_nodes.push_back(filter);
                make_edge(*generator_out, *filter);
                src = filter;
            }

            make_edge(*src, *msg_inputs.at(algo_name).at(port));
        }
    }

    if (!filter_nodes.empty()) {
        std::cout << "  Created " << filter_nodes.size()
                  << " external-input filter nodes for mixed-input algorithms\n";
    }

    // Step 2: Connect algorithm outputs to consumers
    for (const auto& producer_name : metadata.topological_order) {
        const auto& producer_info = metadata.algorithms.at(producer_name);
        sender<AlgorithmOutput>* producer_out = algorithm_nodes.at(producer_name).msg_out;

        // Collect unique consumers for this producer
        std::set<std::string> unique_consumers;
        for (const auto& product_name : producer_info.produces) {
            auto consumers_it = metadata.product_to_consumers.find(product_name);
            if (consumers_it != metadata.product_to_consumers.end()) {
                for (const auto& consumer_name : consumers_it->second) {
                    unique_consumers.insert(consumer_name);
                }
            }
        }

        // Create edges to each unique consumer
        for (const auto& consumer_name : unique_consumers) {
            const auto& sources = algo_sources.at(consumer_name);

            // This producer's join port is its position in the consumer's source
            // list. Internal producers come first there, in the same name order the
            // old code took from its std::set, so no algorithm's ports move.
            std::size_t port = sources.size();
            for (std::size_t i = 0; i < sources.size(); ++i) {
                if (sources[i].producer == producer_name) {
                    port = i;
                    break;
                }
            }
            if (port == sources.size()) {
                throw std::runtime_error(
                    "Algorithm " + consumer_name + " consumes a product of " +
                    producer_name + " but has no message source for it"
                );
            }

            make_edge(*producer_out, *msg_inputs.at(consumer_name).at(port));
        }
        // Note: If no consumers (leaf nodes), outputs are simply discarded by function_node
    }

#if USE_LIMITER_NODE == 0
    // Step 3: Connect resource limiters to resource composite nodes.
    //
    // One (buffer -> in, out -> buffer) pair per declared resource
    for (const auto& algo_name : metadata.topological_order) {
        const auto& algo_info = metadata.algorithms.at(algo_name);
        if (algo_info.resource_usage.empty()) continue;

        const AlgoPorts& ports = algorithm_nodes.at(algo_name);
        for (std::size_t i = 0; i < algo_info.resource_usage.size(); ++i) {
            auto* buffer = resource_limiters.at(algo_info.resource_usage[i]);
            make_edge(*buffer, *ports.handle_in[i]);
            make_edge(*ports.handle_out[i], *buffer);
        }
    }
#endif

    auto end_construction = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> construction_time = end_construction - start_construction;

    std::cout << "Graph constructed in " << construction_time.count() << " seconds\n";

    std::cout << "Starting execution...\n";

    MessageClock::begin_trial();

    // Execute
    auto start_execution = std::chrono::high_resolution_clock::now();

    // Kickstart the input generator with initial memory budget
    input_generator.try_put(static_cast<int64_t>(total_memory_bytes));

    // Wait for completion
    g.wait_for_all();

    auto end_execution = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> execution_time = end_execution - start_execution;

    std::cout << "Execution completed in " << execution_time.count() << " seconds\n";
    std::cout << "Messages sent: " << messages_sent << "\n";

    // Harvest the handle-use counters before `resources` goes out of scope.
    for (auto& resource : resources) {
        g_resource_use_total.fetch_add(resource.counter.load(std::memory_order_relaxed),
                                       std::memory_order_relaxed);
    }

    // Reduce this trial's message clock while the trial is still the current one.
    const SteadyWindow steady =
        reduce_steady_window(config.num_inputs);
    std::cout << "Messages resident: " << steady.in_flight_max << " at most, against C = "
              << admission_window_c << " (the window bounds reserved BYTES, not "
                 "messages -- products refund as they die)\n";
    if (steady.skip_reason) {
        std::cout << "Steady-state window not measured: " << steady.skip_reason
                  << " [run " << steady.execution_s << " s, mean residence "
                  << steady.residence_mean_s << " s, occupancy drift "
                  << steady.occupancy_drift << ", rate drift " << steady.rate_drift
                  << "]\n";
    } else {
        std::cout << "Steady-state window: " << steady.completions << " messages in "
                  << steady.span_s << " s (trimmed " << steady.residence_mean_s
                  << " s from each end), mean resident " << steady.in_flight_mean
                  << ", occupancy drift " << steady.occupancy_drift << ", rate drift "
                  << steady.rate_drift << "\n";
    }

    return {construction_time, execution_time, steady};
}

// ============================================================================
// Main Function
// ============================================================================

int main(int argc, char* argv[]) {
    BenchWatchdog watchdog;

    try {
        WorkflowConfig config = WorkflowConfig::parse_args(argc, argv);
        print_variant_banner("trace_runner");
        const int resolved_threads = bench_resolved_threads(config.num_threads);
        print_run_model(config.num_threads);

        std::cout << "Workflow Benchmark Configuration:\n";
        std::cout << "  JSON file: " << config.json_path << "\n";
        std::cout << "  Threads: " << config.num_threads << "\n";
        std::cout << "  GB per thread: " << config.gb_per_thread << "\n";
        std::cout << "  Num inputs: " << config.num_inputs << "\n";
        std::cout << "  Num trials: " << config.num_trials << "\n";
        std::cout << "  Trace: ";
        if (config.trace_level == TraceLevel::OFF) {
            std::cout << "off";
        } else if (config.tracing_mode == TracingMode::OFF) {
            std::cout << (config.trace_level == TraceLevel::MINIMAL ? "minimal" : "full")
                      << " (suppressed by --tracing-mode=off)";
        } else if (config.trace_level == TraceLevel::MINIMAL) {
            std::cout << "minimal (" << (config.tracing_mode == TracingMode::EAGER ? "EAGER" : "LAZY") << ")";
        } else {
            std::cout << "full (" << (config.tracing_mode == TracingMode::EAGER ? "EAGER" : "LAZY") << ")";
        }
        std::cout << "\n";
        std::cout << "  Variant: " << VARIANT_NAME << "\n";

        WorkflowMetadata metadata = WorkflowMetadata::load_from_json(config.json_path);
        validate_workflow_wiring(metadata);

        const std::string workflow_stem = [&] {
            std::string name = config.json_path.substr(
                config.json_path.find_last_of("/\\") + 1);
            const std::size_t dot = name.find(".json");
            if (dot != std::string::npos) name = name.substr(0, dot);
            return name;
        }();

        std::unique_ptr<oneapi::tbb::global_control> thread_limit =
            bench_thread_limit(config.num_threads);

        uint64_t total_memory_bytes = static_cast<uint64_t>(
            config.gb_per_thread * resolved_threads * 1024.0 * 1024.0 * 1024.0
        );

        if (metadata.total_workflow_memory == 0) {
            bench_die("workflow " + workflow_stem +
                      " declares 0 bytes of data products, so the admission window is "
                      "undefined (it is budget / bytes-per-message)");
        }
        const std::uint64_t admission_window_c =
            total_memory_bytes / metadata.total_workflow_memory;

        std::cout << "\nMemory budget: " << (total_memory_bytes / (1024.0 * 1024.0 * 1024.0))
                  << " GB (" << total_memory_bytes << " bytes)\n";

        std::cout << "Messages can fit concurrently: " << admission_window_c << "\n";

        if (admission_window_c == 0) {
            bench_die("admission window C = 0: a budget of " +
                      std::to_string(total_memory_bytes) +
                      " bytes cannot hold one message of " +
                      std::to_string(metadata.total_workflow_memory) +
                      " bytes, so no message can ever be admitted (raise "
                      "--gb_per_thread or --num_threads)");
        }
        if (admission_window_c >= static_cast<std::uint64_t>(
                                     config.num_inputs > 0 ? config.num_inputs : 0)) {
            const std::string detail =
                "admission window C = " + std::to_string(admission_window_c) +
                " with --num_inputs=" + std::to_string(config.num_inputs) +
                ": the window cannot bind, so this run measures an unthrottled graph "
                "and its memory fractions are num_inputs/C artefacts";
            if (!config.allow_unbound_window) {
                bench_die(detail +
                          ". Raise --num_inputs above C, lower --gb_per_thread, or pass "
                          "--allow_unbound_window=1 to say you meant it");
            }
            std::cerr << "Warning: " << detail
                      << " (--allow_unbound_window=1 was passed)" << std::endl;
        }

        std::cout << "Topological order: ";
        for (const auto& algo : metadata.topological_order) {
            std::cout << algo << " ";
        }
        std::cout << "\n\n";

        // Dump YAML/JSON-compatible metadata for the workflow
        bench_record().set("workflow_json", workflow_stem);
        bench_record().set("num_algorithms", static_cast<int>(metadata.algorithms.size()));
        bench_record().set("num_data_products", static_cast<int>(metadata.data_products.size()));
        bench_record().set("num_resources", static_cast<int>(metadata.limited_resources.size()));
        bench_record().set("gb_per_thread", config.gb_per_thread);
        bench_record().set("memory_budget_bytes", total_memory_bytes);
        bench_record().set("workflow_memory_bytes", metadata.total_workflow_memory);
        bench_record().set("admission_window_messages",
                           static_cast<double>(total_memory_bytes) /
                               static_cast<double>(metadata.total_workflow_memory));
        bench_record().set("admission_window_c", admission_window_c);
        bench_record().set("admission_window_binds",
                           admission_window_c < static_cast<std::uint64_t>(
                                                    config.num_inputs > 0
                                                        ? config.num_inputs : 0));
        bench_record().set("tracing_mode", trace_mode_name(config.tracing_mode));
        bench_record().set("critical_path_ms", metadata.critical_path_seconds * 1000.0);

        // Latency: The unit is one message's END-TO-END lifetime:
        // from the input_generator committing budget to it, to its last DataProduct
        // handing the bytes back. See the stamp site in the generator and the
        // completion site in DataProductDeleter::operator().
        //
        // ids = num_inputs: message ids are [0, num_inputs) and are reused by each
        // trial, exactly as the other six reuse them across executions -- stamp()
        // zeroes the slot's arrival count, and a trial's products are all destroyed
        // inside its own wait_for_all(), so no id is ever live in two trials at once.
        //
        // fanin = products per message, derived above. samples = num_inputs *
        // num_trials: no untimed warmup execution to discard.
        const int products_per_message = ::products_per_message(metadata);
        bench_record().set("products_per_message", products_per_message);
        if (products_per_message != static_cast<int>(metadata.data_products.size())) {
            std::cerr << "Error: workflow declares " << metadata.data_products.size()
                      << " data products but one message constructs "
                      << products_per_message
                      << ". The input_generator reserves the declared total per "
                         "message and is only repaid by a product's destructor, so "
                         "any difference is a per-message leak out of the admission "
                         "budget." << std::endl;
        }
        bench_latency().configure(config.num_inputs, products_per_message,
                                  static_cast<std::size_t>(config.num_inputs) *
                                      static_cast<std::size_t>(config.num_trials));


        // Sized by num_inputs and reset per trial, exactly as BenchLatency's id table
        // is, so the two always agree about which ids exist.
        MessageClock::configure(config.num_inputs);

        // Assign thread IDs to algorithms for tracing
        int tid = 1;
        for (const auto& algo_name : metadata.topological_order) {
            metadata.algorithm_to_tid[algo_name] = tid++;
        }

        // Setup tracing
#if USE_TRACE > 0
        std::unique_ptr<TraceCollector> trace_collector;

        struct trace_collector_global_reset {
            ~trace_collector_global_reset() {
                TraceCollector* tc = g_trace_collector.exchange(nullptr);
                if (tc) tc->flush();
            }
        } trace_collector_global_reset_guard;

        if (config.trace_level != TraceLevel::OFF &&
            config.tracing_mode != TracingMode::OFF) {
            // Extract workflow name from JSON path
            std::string workflow_name = config.json_path.substr(
                config.json_path.find_last_of("/\\") + 1);
            size_t dot = workflow_name.find(".json");
            if (dot != std::string::npos) {
                workflow_name = workflow_name.substr(0, dot);
            }

            // output file names captures most of the relevant configuration parameters for the run
            std::ostringstream filename;
            filename << workflow_name << "_" << VARIANT_NAME;
#if USE_LIMITER_NODE == 0
            // A rejecting build is still jn_impl, but its traces must not overwrite
            // the queueing build's.
            if constexpr (std::is_same_v<jn_body_policy, oneapi::tbb::flow::rejecting>) {
                filename << "_rejecting";
            }
#endif
            filename << "_exec" << config.num_trials
                     << "_input" << config.num_inputs;
            filename << "_p" << resolved_threads;
            filename << "_gb" << std::lround(config.gb_per_thread * 1000);
            filename << "_aw" << admission_window_c;
            filename << (config.tracing_mode == TracingMode::EAGER ? "_eager" : "_lazy");
            filename << (config.trace_level == TraceLevel::MINIMAL ? "_minimal" : "_full");
            filename << ".json";

            // Determine write mode from runtime config
            TraceCollector::WriteMode mode = (config.tracing_mode == TracingMode::EAGER)
                ? TraceCollector::WriteMode::EAGER
                : TraceCollector::WriteMode::LAZY;

            trace_collector = std::make_unique<TraceCollector>(
                filename.str(),
                config.num_trials,
                "workflow_bench",
                mode
            );

            // Register thread names for algorithms
            for (const auto& [algo_name, algo_tid] : metadata.algorithm_to_tid) {
                trace_collector->add_thread_name(algo_tid, algo_name);
            }

            // Register tid=0 for input_generator and source events
            trace_collector->add_thread_name(0, "input_generator");

            // The config event: the admission point this trace was taken at, written
            // into the trace itself rather than left to be read back out of the
            // filename.
            trace_collector->record_instant_event(
                "config:gb_per_thread=" + bench_num(config.gb_per_thread) +
                    ":num_inputs=" + std::to_string(config.num_inputs) +
                    ":p=" + std::to_string(resolved_threads) +
                    ":admission_window_c=" + std::to_string(admission_window_c) +
                    ":workflow_memory_bytes=" +
                    std::to_string(metadata.total_workflow_memory),
                0);

            // Setup signal handler for LAZY mode flush (only if LAZY selected)
            if (config.tracing_mode == TracingMode::LAZY) {
                g_trace_collector.store(trace_collector.get());
                std::atexit([]{
                    TraceCollector* tc = g_trace_collector.load();
                    if (tc) tc->flush();
                });
            }

            const char* write_mode_str = (config.tracing_mode == TracingMode::EAGER) ? "EAGER" : "LAZY";
            const char* detail_level_str = (config.trace_level == TraceLevel::MINIMAL) ? "minimal" : "full";

            std::cout << "Tracing enabled: " << write_mode_str << " write mode, "
                      << detail_level_str << " detail level\n"
                      << "Output: " << filename.str() << "\n";
        } else {
            std::cout << "Tracing disabled\n";
        }
#endif

        std::signal(SIGTERM, timeout_signal_handler);
        std::signal(SIGINT, timeout_signal_handler);

        // 8. Run trials
        std::chrono::duration<double> total_construction_time{0};
        std::chrono::duration<double> total_execution_time{0};

        double steady_span_s = 0.0;
        long long steady_completions = 0;
        double steady_in_flight_mean_weighted = 0.0;
        double steady_drift_weighted = 0.0;
        double steady_rate_drift_weighted = 0.0;
        double steady_residence_sum_s = 0.0;
        int steady_in_flight_max = 0;
        const char* steady_skip_reason = nullptr;

        for (int trial = 0; trial < config.num_trials; ++trial) {
            if (config.num_trials > 1) {
                std::cout << "\n=== Trial " << (trial + 1) << "/" << config.num_trials << " ===\n";
            }

            auto [construction_time, execution_time, steady] = run_workflow(
                metadata,
                config,
                total_memory_bytes,
                static_cast<int>(admission_window_c)
#if USE_TRACE > 0
                , trace_collector.get()
#endif
            );

            total_construction_time += construction_time;
            total_execution_time += execution_time;

            steady_residence_sum_s += steady.residence_mean_s;
            steady_in_flight_max = std::max(steady_in_flight_max, steady.in_flight_max);
            if (steady.skip_reason) {
                if (!steady_skip_reason) steady_skip_reason = steady.skip_reason;
            } else {
                steady_span_s += steady.span_s;
                steady_completions += steady.completions;
                steady_in_flight_mean_weighted += steady.in_flight_mean * steady.span_s;
                steady_drift_weighted += steady.occupancy_drift * steady.span_s;
                steady_rate_drift_weighted += steady.rate_drift * steady.span_s;
            }
        }

        // 8. Print results
        std::cout << "\n=== Summary ===\n";
        std::cout << "Average construction time: "
                  << (total_construction_time.count() / config.num_trials) << " s\n";
        std::cout << "Average execution time: "
                  << (total_execution_time.count() / config.num_trials) << " s\n";
        std::cout << "Average total time: "
                  << ((total_construction_time.count() + total_execution_time.count()) / config.num_trials)
                  << " s\n";

        validate_latency(static_cast<std::size_t>(config.num_inputs) *
                             static_cast<std::size_t>(config.num_trials),
                         "workflow_bench");

        const double exec_s = total_execution_time.count();
        const double messages = static_cast<double>(config.num_trials) *
                                static_cast<double>(config.num_inputs);
        bench_record().set("bench_name", "workflow_bench");
        bench_record().set("num_executions", config.num_trials);
        bench_record().set("num_inputs", config.num_inputs);
        bench_record().set("construction_s", total_construction_time.count());
        bench_record().set("execution_s", exec_s);
        const double end_to_end_throughput =
            exec_s > 0.0 ? messages / exec_s
                         : std::numeric_limits<double>::quiet_NaN();
        bench_record().set("throughput_msg_per_s", end_to_end_throughput);

        const bool steady_ok = (steady_skip_reason == nullptr) &&
                               steady_completions > 1 && steady_span_s > 0.0;
        bench_record().set("steady_state_status",
                           steady_skip_reason ? steady_skip_reason
                                              : (steady_ok ? "ok" : "no positive span"));
        bench_record().set("steady_state_messages", steady_completions);
        bench_record().set("steady_state_trim_s",
                           steady_residence_sum_s / config.num_trials);
        bench_record().set("steady_state_span_s",
                           steady_ok ? steady_span_s
                                     : std::numeric_limits<double>::quiet_NaN());
        const double steady_throughput =
            steady_ok ? static_cast<double>(steady_completions) / steady_span_s
                      : std::numeric_limits<double>::quiet_NaN();
        bench_record().set("steady_state_throughput_msg_per_s", steady_throughput);
        bench_record().set("throughput_fill_drain_bias",
                           (steady_ok && steady_throughput > 0.0)
                               ? 1.0 - end_to_end_throughput / steady_throughput
                               : std::numeric_limits<double>::quiet_NaN());
        bench_record().set("max_in_flight_messages",
                           static_cast<long long>(steady_in_flight_max));
        bench_record().set("mean_in_flight_messages",
                           steady_ok ? steady_in_flight_mean_weighted / steady_span_s
                                     : std::numeric_limits<double>::quiet_NaN());
        bench_record().set("steady_state_rate_drift",
                           steady_ok ? steady_rate_drift_weighted / steady_span_s
                                     : std::numeric_limits<double>::quiet_NaN());
        bench_record().set("steady_state_occupancy_drift",
                           steady_ok ? steady_drift_weighted / steady_span_s
                                     : std::numeric_limits<double>::quiet_NaN());
        bench_record().set("max_in_flight_over_window_c",
                           admission_window_c > 0
                               ? static_cast<double>(steady_in_flight_max) /
                                     static_cast<double>(admission_window_c)
                               : std::numeric_limits<double>::quiet_NaN());
        bench_record().set("max_in_flight_fraction_of_num_inputs",
                           config.num_inputs > 0
                               ? static_cast<double>(steady_in_flight_max) /
                                     static_cast<double>(config.num_inputs)
                               : std::numeric_limits<double>::quiet_NaN());
        bench_record().set("total_graph_time_ms",
                           std::numeric_limits<double>::quiet_NaN());
        double work_per_message_ms = 0.0;
        for (const auto& [_, algo_info] : metadata.algorithms) {
            work_per_message_ms += algo_info.execution_time_seconds * 1000.0;
        }
        bench_record().set("work_per_message_ms", work_per_message_ms);
        const double critical_path_ms = metadata.critical_path_seconds * 1000.0;
        const std::vector<double> latency_ms = bench_latency().sorted_ms();
        bench_record().set(
            "lat_p50_over_critical_path",
            critical_path_ms > 0.0 ? bench_quantile_ms(latency_ms, 0.50) / critical_path_ms
                                   : std::numeric_limits<double>::quiet_NaN());
        bench_record().set(
            "lat_p95_over_critical_path",
            critical_path_ms > 0.0 ? bench_quantile_ms(latency_ms, 0.95) / critical_path_ms
                                   : std::numeric_limits<double>::quiet_NaN());

        std::size_t uses_per_message = 0;
        for (const auto& [_, algo_info] : metadata.algorithms) {
            uses_per_message += algo_info.resource_usage.size();
        }
        const std::uint64_t uses_expected =
            static_cast<std::uint64_t>(uses_per_message) *
            static_cast<std::uint64_t>(config.num_inputs) *
            static_cast<std::uint64_t>(config.num_trials);
        const std::uint64_t uses_actual = g_resource_use_total.load();
        bench_record().set("resource_uses_per_message",
                           static_cast<int>(uses_per_message));
        bench_record().set("resource_use_total", uses_actual);
        bench_record().set("resource_use_expected", uses_expected);
        if (uses_actual != uses_expected) {
            std::cerr << "Error: resources were used " << uses_actual
                      << " times but the workflow declares " << uses_per_message
                      << " uses per message x " << config.num_inputs << " messages x "
                      << config.num_trials << " trials = " << uses_expected
                      << ". Every declared resource must be held by every body call "
                         "that declared it." << std::endl;
        }

        const std::uint64_t peak_live = MemoryGauge::peak_bytes.load();
        const std::uint64_t byte_us = MemoryGauge::byte_us.load();
        const std::uint64_t constructed = MemoryGauge::constructed.load();
        const std::uint64_t destroyed = MemoryGauge::destroyed.load();
        const std::uint64_t live_final = MemoryGauge::live_bytes.load();

        bench_record().set("peak_live_bytes", peak_live);
        bench_record().set("mean_live_bytes",
                           exec_s > 0.0
                               ? static_cast<double>(byte_us) / (exec_s * 1.0e6)
                               : std::numeric_limits<double>::quiet_NaN());
        bench_record().set("live_byte_us", byte_us);
        bench_record().set("products_constructed", constructed);
        bench_record().set("products_destroyed", destroyed);
        bench_record().set("live_bytes_final", live_final);
        bench_record().set("peak_live_fraction_of_budget",
                           total_memory_bytes > 0
                               ? static_cast<double>(peak_live) /
                                     static_cast<double>(total_memory_bytes)
                               : std::numeric_limits<double>::quiet_NaN());

        bench_record().set("mean_occupancy_of_budget",
                           (exec_s > 0.0 && total_memory_bytes > 0)
                               ? static_cast<double>(byte_us) /
                                     (exec_s * 1.0e6 *
                                      static_cast<double>(total_memory_bytes))
                               : std::numeric_limits<double>::quiet_NaN());

        const std::uint64_t reserved_messages =
            std::min<std::uint64_t>(static_cast<std::uint64_t>(config.num_inputs > 0
                                                                   ? config.num_inputs : 0),
                                    admission_window_c);
        const std::uint64_t peak_reserved =
            reserved_messages * metadata.total_workflow_memory;
        bench_record().set("peak_reserved_bytes", peak_reserved);
        bench_record().set("peak_reserved_fraction_of_budget",
                           total_memory_bytes > 0
                               ? static_cast<double>(peak_reserved) /
                                     static_cast<double>(total_memory_bytes)
                               : std::numeric_limits<double>::quiet_NaN());

        std::cout << "Peak live product memory: " << peak_live << " bytes ("
                  << (static_cast<double>(peak_live) / (1024.0 * 1024.0)) << " MB)\n";

        std::cout << "Mean occupancy of budget: "
                  << ((exec_s > 0.0 && total_memory_bytes > 0)
                          ? (static_cast<double>(byte_us) /
                             (exec_s * 1.0e6 * static_cast<double>(total_memory_bytes)))
                          : std::numeric_limits<double>::quiet_NaN())
                  << " (peak reserved / budget: "
                  << (total_memory_bytes > 0
                          ? static_cast<double>(peak_reserved) /
                                static_cast<double>(total_memory_bytes)
                          : std::numeric_limits<double>::quiet_NaN())
                  << ", config-determined)\n";
        std::cout << "Throughput: " << end_to_end_throughput
                  << " msg/s end-to-end, " << steady_throughput
                  << " msg/s steady-state (C = " << admission_window_c << ")\n";

        if (constructed != destroyed || live_final != 0) {
            std::cerr << "Error: " << constructed << " data products constructed but "
                      << destroyed << " destroyed, leaving " << live_final
                      << " bytes live at exit. The input_generator is only repaid by "
                         "a product's destructor, so a surviving product is a "
                         "permanent hole in the admission budget." << std::endl;
        }

        bench_emit_record();
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        std::cerr << "\nUsage: workflow_bench --json=<path> [options]\n";
        std::cerr << "Options:\n";
        std::cerr << "  --json=<path>        Path to workflow JSON file (required)\n";
        std::cerr << "  --num_threads=<n>    Number of worker threads, 0 = all cores (default: 4)\n";
        std::cerr << "  --gb_per_thread=<n>  GB of memory per thread (default: 2.0)\n";
        std::cerr << "  --num_inputs=<n>     Number of input messages (default: 100)\n";
        std::cerr << "  --num_trials=<n>     Number of trial executions (default: 1)\n";
        // Documented with what it costs, not just what it permits: a row that passes it
        // is opting out of the one check that says the run measured its own subject.
        std::cerr << "  --allow_unbound_window=<0|1>  Permit C >= num_inputs, i.e. an\n";
        std::cerr << "                       admission window that never binds. Fatal by\n";
        std::cerr << "                       default; such a run measures an unthrottled\n";
        std::cerr << "                       graph and its memory fractions are\n";
        std::cerr << "                       num_inputs/C artefacts (default: 0)\n";
        // Was "--trace=<on|off>", and `on` has never been accepted -- the parser
        // takes off|minimal|full and exits on anything else. --tracing-mode was
        // undocumented here entirely.
        std::cerr << "  --trace=<off|minimal|full>     Trace detail level (default: off)\n";
        std::cerr << "  --tracing-mode=<off|eager|lazy>  When to write the trace (default: lazy)\n";
        std::cerr << "                       off suppresses the trace even if --trace= asks for one\n";
        return 1;
    }
}
