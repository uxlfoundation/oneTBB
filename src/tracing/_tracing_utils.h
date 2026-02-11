/*
    Copyright (C) 2024-2026 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#ifndef __TCM_TRACING_UTILS_HEADER
#define __TCM_TRACING_UTILS_HEADER

#include "tcm/detail/_tcm_assert.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <vector>

namespace tcm {
namespace internal {
namespace profiling {
namespace common {

using tm_point = std::chrono::steady_clock::time_point;


/**
 * Updates widths of columns to fit the new data
 *
 * @param column_widths Widths of columns to update
 *
 * @param cell_values New values to fit into the columns
 */
inline void fit_column_widths(std::vector<unsigned>& column_widths,
                              std::vector<std::string> const& cell_values)
{
    for (unsigned i = 0; i < column_widths.size(); ++i)
        column_widths[i] = std::max(column_widths[i], unsigned(cell_values[i].size()));
}

/**
 * Calculates the widths of table columns given their textual representation.
 *
 * @param columns Textual representation of columns' values in a table row.
 *
 * @param common_width_start_column Index in \p columns starting from which to assign common
 *                                  width of subsequent columns.
 *
 * @return Widths of columns in resulting table representation.
 */
inline std::vector<unsigned> determine_column_widths(std::vector<std::string> const& columns,
                                                     unsigned const common_width_start_column)
{
    std::vector<unsigned> columns_widths(columns.size(), 0);
    for (unsigned i = 0; i < columns.size(); ++i) {
        columns_widths[i] = unsigned(columns[i].size());
    }

    if (common_width_start_column < columns.size()) {
        const unsigned common_width =
            *std::max_element(columns_widths.cbegin() + common_width_start_column,
                              columns_widths.cend());
        std::fill(columns_widths.begin() + common_width_start_column, columns_widths.end(),
                  common_width);
    }
    return columns_widths;
}

/**
 * Outputs a table row in accordance with formatting.
 *
 * @param out An output stream where to send the data.
 *
 * @param cell_widths Formatting for table cells.
 *
 * @param intercell_space Space to use between the cells in a row.
 *
 * @param cell_values Values to place in cells.
 */
inline void print_table_row(std::ostream& out, std::vector<unsigned> const& cell_widths,
                            std::string const& intercell_space,
                            std::vector<std::string> const& cell_values)
{
    __TCM_ASSERT(cell_widths.size() == cell_values.size(), "Mismatched sizes");
    out << std::left;
    out << std::setw(cell_widths[0]) << cell_values[0];
    out << std::right;
    for (unsigned i = 1; i < cell_values.size(); ++i) {
        out << intercell_space << std::setw(cell_widths[i]) << cell_values[i];
    }
    out << std::endl;
}

/**
 * Gets the current point in time.
 *
 * @return An entity of \c tm_point type having the value of current point in time.
 */
inline tm_point current_time_point() { return std::chrono::steady_clock::now(); }

/**
 * A thread-unsafe interface allowing logging of data.
 */
template <typename Record>
class thread_logger {
public:
    template <typename... Args>
    void log(Args&&... args) {
        // TODO: Determine why compiler produces error when only constructor args are specified
        // separated by commas and not using braced list.
        trace.emplace_back(Record{std::forward<Args>(args)...});
    }
    const std::deque<Record>& get_logs() const { return trace; }
private:
    std::deque<Record> trace;
};

/**
 * Per thread logger. Allows receiving the \c thread_logger interface. Calls specified post
 * processing function at the end.
 */
template <typename Record>
class tracer {
public:
    using post_processor_t = void (*)(std::deque<thread_logger<Record>> const&);

    tracer(post_processor_t post_process_func) : m_post_process(post_process_func) {}

    ~tracer() { m_post_process(trace); }

    thread_logger<Record>& get_thread_logger() {
        if (m_logger) {
            return *m_logger;
        }
        m_logger = allocate_logger();
        return *m_logger;
    }

private:
    thread_logger<Record>* allocate_logger() {
        std::lock_guard<std::mutex> lock_guard(mutex);
        trace.emplace_back();
        return &trace[trace.size() - 1];
    }

    std::mutex mutex;
    std::deque<thread_logger<Record>> trace;   // per thread logs
    static thread_local thread_logger<Record>* m_logger;
    post_processor_t m_post_process = nullptr;
};

template <typename Record> thread_local thread_logger<Record>* tracer<Record>::m_logger = nullptr;

} // common
} // profiling
} // internal
} // tcm

#endif // __TCM_TRACING_UTILS_HEADER
