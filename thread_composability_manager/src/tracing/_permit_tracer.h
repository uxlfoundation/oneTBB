/*
   Copyright (c) 2024 Intel Corporation
   Copyright (c) 2026 UXL Foundation Contributors

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#ifndef __TCM_PERMIT_TRACER_HEADER
#define __TCM_PERMIT_TRACER_HEADER

#if __TCM_ENABLE_PERMIT_TRACER

#include "tcm/types.h"
#include "_tracing_utils.h"
#include "../utils.h"

#include "../tcm_permit_rep.h"

#include <algorithm>
#include <deque>
#include <iostream>
#include <map>
#include <numeric>
#include <string>
#include <vector>
#include <thread>

namespace tcm {
namespace internal {
namespace profiling {
namespace permit {

using common::fit_column_widths;
using common::determine_column_widths;
using common::thread_logger;
using common::tracer;
using common::tm_point;
using common::current_time_point;
using common::print_table_row;

/**
 * The information dumped at every cutoff.
 */
struct permits_info {
    tm_point time_point;
    const char* event = nullptr;

    std::thread::id thread_id;

    // TODO: Consider re-using instead of repeating permit's data here

    // Permit snashot
    tcm_permit_handle_t permit_handle = nullptr;
    tcm_permit_state_t state;
    tcm_permit_flags_t flags;
    bool is_nested = false;
    std::vector<uint32_t> concurrencies;

    // Request snapshot
    tcm_permit_request_t request;
    void* callback_arg = nullptr;
};

using table_t = std::vector<std::vector<std::string>>;

/**
 * Maps Thread ID to a consecutive number starting from one.
 *
 * @param tid Thread ID to map to a number.
 *
 * @param tid_to_num Map of Thread IDs to their corresponding ordinal numbers. Updated with the \c
 * tid if it was not present in it before the function invocation.
 *
 * @return a number associated with the given \c tid.
 */
inline unsigned map_to_ordinal_number(const std::thread::id& tid,
                                      std::map<std::thread::id, unsigned>& tid_to_num)
{
    const unsigned size = unsigned(tid_to_num.size());
    const auto node = tid_to_num.try_emplace(tid, size + 1);
    return node.first->second; // returns either inserted or existing associated number
}

/**
 * Makes a table of entries, where each table cell is represented as a string.
 *
 * @param data The data from which to make a table.
 *
 * @return A table represented as a two-dimensional array of strings.
 */
inline table_t get_user_presentation(const std::vector<permits_info const*>& data) {
    table_t result(data.size());
    std::map<std::thread::id, unsigned> tid_to_num;
    extern const tm_point start_time;
    tm_point last_time_point = start_time;
    for (unsigned i = 0; i < data.size(); ++i) {
        std::vector<std::string> row; row.reserve(14);

        permits_info const& info = *data[i];

        row.emplace_back(info.event);

        auto const duration = info.time_point - last_time_point;
        last_time_point = info.time_point;
        row.emplace_back("+" + std::to_string(duration.count()));

        row.emplace_back(std::to_string(map_to_ordinal_number(info.thread_id, tid_to_num)));
        row.emplace_back(to_string(info.permit_handle));
        row.emplace_back(to_string(info.state));
        row.emplace_back(to_string(info.flags));
        row.emplace_back(info.is_nested ? "yes" : "no");
        row.emplace_back(to_string(info.concurrencies.data(), uint32_t(info.concurrencies.size())));

        tcm_permit_request_t const& req = info.request;
        row.emplace_back("[" + std::to_string(req.min_sw_threads) +
                         ", " + std::to_string(req.max_sw_threads) + "]");
        row.emplace_back(to_string(req.flags));
        row.emplace_back(to_string(req.cpu_constraints));
        row.emplace_back(std::to_string(req.constraints_size));
        row.emplace_back(to_string(req.priority));
        row.emplace_back(::to_string(info.callback_arg));

        result[i] = std::move(row);
    }
    return result;
}

/**
 * Sends gathered data to the output stream using the table representation.
 *
 * @param out An output stream where to send the data.
 *
 * @param data The data to present.
 */
inline void print(std::ostream& out, const std::vector<permits_info const*>& data) {
    out << "\nPermits data in chronological order:\n";

    std::vector<std::string> const table_header = {
        "Event", "Time Diff (ns)", "Thr #", "Permit Handle", "State", "Flags", "Nested", "Grant",
        "Request", "Flags", "Constraints", "Size", "Priority", "Callback Arg"
    };
    auto columns_widths = determine_column_widths(table_header,
                                                  /*common width start column*/
                                                  unsigned(table_header.size()));
    table_t table = get_user_presentation(data);
    for (auto const& row : table)
        fit_column_widths(columns_widths, row);

    std::string const intercolumn_space = " ";
    unsigned const table_width = std::accumulate(columns_widths.cbegin(), columns_widths.cend(), 0)
                                 + unsigned((columns_widths.size() - 1) * intercolumn_space.size());
    std::string const table_row_separator = std::string(table_width, '-');
    print_table_row(std::cout, columns_widths, intercolumn_space, table_header);
    out << table_row_separator << std::endl;
    for (auto const& row: table)
        print_table_row(std::cout, columns_widths, intercolumn_space, row);
}

/**
 * Returns a one-dimenstional container with entries pointing to the original two-dimensional one.
 *
 * @param trace Initial two-dimensional container
 *
 * @return One-dimensional container with pointers
 */
inline std::vector<permits_info const*>
flatten_data(const std::deque<thread_logger<permits_info>>& trace) {
    unsigned num_total_entries = std::accumulate(trace.cbegin(), trace.cend(), /*init value*/0,
                                                 [](auto const& acc, auto const& thread_log) {
                                                     auto const& logs = thread_log.get_logs();
                                                     return acc + unsigned(logs.size());
                                                 });

    std::vector<permits_info const*> flattened_data(num_total_entries);
    unsigned i = 0;
    for (auto const& thread_log : trace) {
        for (auto const& entry : thread_log.get_logs()) {
            flattened_data[i++] = &entry;
        }
    }
    return flattened_data;
}

/**
 * Analyzes the gathered data and presents it to the user.
 *
 * @param trace Gathered data
 */
inline void post_process(const std::deque<thread_logger<permits_info>>& trace)
{
    std::vector<permits_info const*> flattened = flatten_data(trace);

    std::sort(flattened.begin(), flattened.end(), [](permits_info const* l, permits_info const* r) {
        return l->time_point < r->time_point; // Sort in chronological order
    });

    print(std::cout, flattened);
}


// The initial time point against which eveything else is compared
inline const tm_point start_time = current_time_point();

// The global object that gathers data for analysis and presenting to the user
inline tracer<permits_info> g_permits_log(post_process);


/**
 * Captures permit information along with a supportive message
 *
 * @param permit_handle Handle of the permit whose information is captured
 *
 * @param event Supportive message that helps distinguish permit request events
 */
inline void dump_permit_data(tcm_permit_handle_t permit_handle, const char* event) {
    auto const time_point = current_time_point(); // Capture time as soon as possible

    // TODO: reuse get_permit_data
    std::vector<uint32_t> concurrencies(permit_handle->data.size);
    for (unsigned i = 0; i < concurrencies.size(); ++i) {
        concurrencies[i] = permit_handle->data.concurrency[i].load(std::memory_order_relaxed);
    }
    tcm_permit_state_t state = permit_handle->data.state;
    tcm_permit_flags_t flags = permit_handle->data.flags;
    bool is_nested = permit_handle->data.is_nested.load(std::memory_order_relaxed);
    void* callback_arg = permit_handle->callback_arg;
    tcm_permit_request_t request = permit_handle->request; // Note shallow copy

    g_permits_log.get_thread_logger().log(time_point, event, std::this_thread::get_id(),
                                          permit_handle, state, flags, is_nested, concurrencies,
                                          request, callback_arg);
}

#define __TCM_PROFILE_PERMIT(permit_handle, msg)                               \
  ::tcm::internal::profiling::permit::dump_permit_data(permit_handle, msg)

} // permit
} // profiling
} // internal
} // tcm

#else  // __TCM_ENABLE_PERMIT_TRACER
#define __TCM_PROFILE_PERMIT(permit_handle, msg)
#endif // __TCM_ENABLE_PERMIT_TRACER

#endif // __TCM_PERMIT_TRACER_HEADER
