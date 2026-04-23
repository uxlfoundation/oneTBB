/*
   Copyright (c) 2023 Intel Corporation

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#ifndef __TCM_TIME_TRACER_HEADER
#define __TCM_TIME_TRACER_HEADER

#if __TCM_ENABLE_CALLSTACK_TRACER

#include "tcm/detail/_tcm_assert.h"
#include "_tracing_utils.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <stack>
#include <string>
#include <vector>

namespace tcm {
namespace internal {
namespace profiling {
namespace callstack {

using common::print_table_row;
using common::determine_column_widths;
using common::thread_logger;
using common::tracer;
using common::tm_point;
using common::current_time_point;
using common::fit_column_widths;

/**
 * The information dumped at every cutoff.
 */
struct function_call_event {
    const char* func_name = nullptr;
    const tm_point time_point;
};

/**
 * Callstack is represented as the structure of arrays, where the elements with the same index
 * across the containers in this structure are associated with single callstack frame
 */
struct callstack {
    // First item is the first function entered in the callstack, it is related to the bottom of the
    // callstack (i.e., least recently called function), while the last item is the most recently
    // called one. The first item embraces the following.
    std::vector<std::string> function_names;

    // Corresponding nesting levels of callstack frames in relation to the bottom of the callstack,
    // which nesting level is zero
    std::vector<unsigned> nesting_levels;

    // TODO: Consider to introduce three-way comparison operator once support for C++20 is required
    bool operator<(const callstack& rhs) const
    {
        // Lexicographically compare function names and their nesting levels
        if (function_names < rhs.function_names) {
            return true;
        } else if (rhs.function_names < function_names) {
            return false;
        }
        // Function names are equivalent, compare the nesting levels
        return nesting_levels < rhs.nesting_levels;
    }
};

/**
 * Associated durations of frames in callstack and individual functions, when frames are considered
 * separately. Moved outside the callstack structure for easier build of the statistics map.
 */
using function_durations = std::vector<std::chrono::nanoseconds>;
using frames_durations = std::vector<function_durations>;

/**
 * Holds computed statistics for a particular function/callstack frame
 */
struct func_statistic {
    unsigned num_measurements = 0;
    std::chrono::nanoseconds min = std::chrono::nanoseconds(0);
    std::chrono::nanoseconds mean = std::chrono::nanoseconds(0);
    std::chrono::nanoseconds median = std::chrono::nanoseconds(0);
    std::chrono::nanoseconds max = std::chrono::nanoseconds(0);
};
using frames_statistics = std::vector<func_statistic>;

// Per callstack data
using callstacks_data = std::map<callstack, frames_durations>;
using callstacks_statistics = std::map<callstack, frames_statistics>;

// Per function data. Gathered for individual callstack frames by considering them separately.
using functions_data = std::map<std::string, function_durations>;
using functions_statistics = std::map<std::string, func_statistic>;

using callstack_iterator = std::deque<function_call_event>::const_iterator;

/**
 * Adds function name of a frame, its nesting level and duration into corresponding parameters.
 */
inline void add_stack_frame(callstack& calls, function_durations& durations,
                            const char* func_name, const unsigned nesting_level,
                            const std::chrono::nanoseconds& duration)
{
    __TCM_ASSERT(func_name, "Incorrect function name");

    // Suppressing 'func_name' could be '0': this does not adhere to the specification for the
    // function 'std::basic_string<char,std::char_traits<char>,std::allocator<char> >::{ctor}'
    __TCM_SUPPRESS_WARNING_WITH_PUSH(6387)
    calls.function_names.push_back(std::string(func_name));
    __TCM_SUPPRESS_WARNING_POP

    calls.nesting_levels.push_back(nesting_level);
    durations.push_back(duration);
}

/**
 * An association between the iterator in the log pointing to start of a stack frame and index in
 * containers of being populated \c callstack entity corresponding to that stack frame.
 */
struct stack_pointer {
    const callstack_iterator frame_start_it;
    const unsigned callstack_index = 0;
};

/**
 * Builds the callstack along with durations of each function in the callstack.
 *
 * @param it An iterator pointing to the bottom frame, i.e., the beginning of a callstack. Must be
 *           dereferenceable.
 *
 * @param calls An empty \p callstack to be filled in by the function.
 *
 * @param durations An empty container that is to be filled with frames durations. Each element
 *                  corresponds to an entry with the same index in containers of the \p calls
 *                  structure.
 *
 * @return An iterator to the starting frame of the next callstack or an iterator to a corresponding
 *         container's cend().
 */
inline callstack_iterator build_callstack(callstack_iterator it, callstack& calls,
                                          function_durations& durations)
{
    unsigned entry_index = 0;
    std::stack<stack_pointer> stack_frames;
    stack_frames.push({it, entry_index});

    add_stack_frame(calls, durations, it->func_name, /*nesting_level*/0,
                    /*duration*/std::chrono::nanoseconds(0));

    while (!stack_frames.empty()) {
        ++it;
        const unsigned current_nesting_level = unsigned(stack_frames.size());
        const auto& [last_entry_it, last_entry_idx] = stack_frames.top();

        const bool is_new_stack_frame = 0 != std::strcmp(it->func_name, last_entry_it->func_name);
        if (is_new_stack_frame) {
            add_stack_frame(calls, durations, it->func_name, current_nesting_level,
                            /*duration*/std::chrono::nanoseconds(0));
            stack_frames.push({it, ++entry_index});
            continue;
        }

        __TCM_ASSERT(std::strcmp(calls.function_names[last_entry_idx].c_str(), it->func_name) == 0
                     && current_nesting_level - 1 == calls.nesting_levels[last_entry_idx],
                     "Callstack entry does not correspond to the log entry.");

        const std::chrono::nanoseconds duration = it->time_point - last_entry_it->time_point;
        __TCM_ASSERT(last_entry_idx < durations.size(), "Broken invariant");
        durations[last_entry_idx] = duration;

        stack_frames.pop();
    }
    return ++it;
}

/**
 * Appends duration of each frame in a callstack to a container of durations of that frame
 * associated with certain callstack.
 *
 * @param per_frame_durations A 2D matrix of frames' durations constituting a callstack
 *
 * @param durations A 1D array of frames' durations from a single callstack run.
 */
inline void append_callstack_durations_piecewise(frames_durations& per_frame_durations,
                                                 function_durations const& durations)
{
    if (per_frame_durations.empty()) {
        per_frame_durations.resize(durations.size());
    }
    __TCM_ASSERT(per_frame_durations.size() == durations.size(),
                 "Accumulating callstack_durations from callstacks of different size");
    for (unsigned i = 0; i < per_frame_durations.size(); ++i) {
        per_frame_durations[i].push_back(durations[i]);
    }
}

/**
 * Traverses gathered logs in a \p trace to build a callstack map of the form
 *
 *           <callstack> <=> <frame durations in a given callstack>
 *
 * Callstacks are considered different if their callee's invocation chain including callee's nesting
 * level is different.
 *
 * An example of possible callstack map, containing two callstacks that originate in the same \c
 * caller1 frame, but containing different set of callee frames, hence considered separately.
 *
 *                             +------------+--------------------+
 *                             | Callstacks | Frame durations    |
 *                             |------------+--------------------|
 *                             | caller1    | 100 120 110 130... |
 *                             | -callee1   | 30  30  43  37...  |
 *                             | -callee2   | 25  34  30  29...  |
 *                             | --callee3  | 10  20  15  18...  |
 *                             | -callee4   | 20  18  19  23...  |
 *                             |------------+--------------------|
 *                             | caller1    | 60 70 70 71...     |
 *                             | -callee1   | 30 20 25 33...     |
 *                             | -callee4   | 20 24 23 22...     |
 *                             |------------+--------------------|
 *                             |     ...    |        ...         |
 *                             +------------+--------------------+
 *
 * @param trace A container of gathered per thread callstacks. Each callstack should originate and
 *              end in a given element of the \p trace container.
 *
 * @return A map of callstacks and their per-frame durations.
 */
inline callstacks_data build_callstack_map(const std::deque<thread_logger<function_call_event>>& trace)
{
    callstacks_data data;
    for (const auto& per_thread_logger : trace) {
        const std::deque<function_call_event>& logs = per_thread_logger.get_logs();
        callstack_iterator next_callstack_it = logs.cbegin();
        while (next_callstack_it != logs.cend()) {
            callstack calls;
            function_durations durations;
            next_callstack_it = build_callstack(next_callstack_it, calls, durations);

            __TCM_ASSERT(2 * calls.function_names.size() - calls.nesting_levels.size() -
                         durations.size() == 0, "Inconsistent callstack");

            append_callstack_durations_piecewise(data[calls], durations);
        }
    }
    return data;
}

/**
 * Gathers durations of the functions across callstacks.
 *
 * @param callstacks_measurements Callstacks with gathered measurements per each callstack frame.
 *
 * @return An associative array with the function name as a key and its measurements as the value.
 */
inline functions_data build_function_map(callstacks_data const& callstacks_measurements)
{
    functions_data data;
    for (const auto& node : callstacks_measurements) {
        callstack const& cs = node.first;
        frames_durations const& frames_data = node.second;

        for (unsigned i = 0; i < cs.function_names.size(); ++i) {
            function_durations& function_measurements = data[cs.function_names[i]];
            function_measurements.insert(function_measurements.cend(),
                                         frames_data[i].cbegin(), frames_data[i].cend());
        }
    }
    return data;
}

/**
 * Sorts the measurements gathered per each frame in the callstack and per each function.
 *
 * @param callstacks An associative array of callstack and measurements of each frame in the
 * callstack.
 *
 * @param functions An associative array of functions found across all gathered callstacks and their
 * measurements.
 */
inline void sort_measurements(callstacks_data& callstacks, functions_data& functions)
{
    for (auto& node : callstacks) {
        frames_durations& frames_data = node.second;
        for (unsigned i = 0; i < frames_data.size(); ++i) {
            std::sort(frames_data[i].begin(), frames_data[i].end());
        }
    }

    for (auto& node : functions) {
        function_durations& function_measurements = node.second;
        std::sort(function_measurements.begin(), function_measurements.end());
    }
}

/**
 * Compute statistics given sorted data.
 *
 * @param durations A sorted container from which to obtain statistical values.
 *
 * @return Computed statistics as described by the corresponding return type.
 */
inline func_statistic compute_statistic(function_durations const& durations)
{
    __TCM_ASSERT(std::is_sorted(durations.cbegin(), durations.cend()), "Input is not sorted");

    const unsigned size = unsigned(durations.size());
    const auto mean = std::accumulate(durations.cbegin(), durations.cend(),
                                         std::chrono::nanoseconds(0)) / size;
    auto median = durations[size / 2];
    if (size % 2 == 0) {
        const unsigned idx = size / 2;
        median = (durations[idx] + durations[idx - 1]) / 2;
    }

    return {size, /*min*/durations[0], mean, median, /*max*/durations[size - 1]};
}

/**
 * Calculates statistics per each frame in callstack and per each function independently from the
 * callstack in which it was found.
 *
 * @param callstacks Gathered measurements of frames for every callstack as an associative array.
 *
 * @param functions Gathered measurements for functions as an associative array.
 *
 * @return A pair of computed statistics. First item in a pair is per callstack statistics, second -
 * per function.
 */
inline std::pair<callstacks_statistics, functions_statistics>
compute_statistics(callstacks_data const& callstacks, functions_data const& functions)
{
    callstacks_statistics cs_stats;
    functions_statistics fn_stats;
    for (auto& node : callstacks) {
        const callstack& cs = node.first;
        frames_durations const& frames_data = node.second;
        __TCM_ASSERT(cs_stats.count(cs) == 0, "Incorrect invariant");
        frames_statistics& frames_stats = cs_stats[cs];
        frames_stats.resize(frames_data.size());
        for (unsigned i = 0; i < frames_data.size(); ++i) {
            frames_stats[i] = compute_statistic(frames_data[i]);
        }
    }

    for (auto& node : functions) {
        fn_stats[/*function name*/node.first] = compute_statistic(/*function durations*/node.second);
    }
    return std::make_pair(cs_stats, fn_stats);
}

using func_statistic_view = std::vector<std::string>; // a node in functions_statistics as strings

/**
 * Extracts the name of the function from its signature.
 *
 * @param s A function signature, from which to extract the name of the function.
 *
 * @return A string containing the function name only.
 */
inline std::string prettify(std::string const& s)
{
    unsigned braces_nesting_level = 0;
    int idx = int(s.length() - 1);
    while (idx >= 0) {
        char c = s[idx--];
        if (c == ')') {
            ++braces_nesting_level;
        } else if (c == '(') {
            --braces_nesting_level;
            if (0 == braces_nesting_level) {
                break;      // Found the beginning of parameters
            }
        }
    }

    while (idx >= 0 && s[idx] == ' ') --idx; // Skipping space between name and parameters
    auto end_idx = idx + 1;

    while (idx >= 0 && s[idx] != ' ') --idx; // Rewind till the beginning of name
    auto begin_idx = idx + 1;

    return s.substr(begin_idx, end_idx - begin_idx);
}

using functions_statistics_view = std::vector<func_statistic_view>;
using callstack_view = std::vector<func_statistic_view>;  // a node in callstacks_statistics
using callstacks_statistics_view = std::vector<callstack_view>;

/**
 * Sorts statistics of callstacks so that the most contributing ones appear last. Contribution
 * factor is calculated as the number of measurements times mean duration of the bottom of the
 * callstack, i.e., least recently called frame, as its duration already includes the durations of
 * its frames.
 *
 * @param cs_stats Statistics of callstacks to sort.
 *
 * @return A container of sorted <callstack, its frames statistics> pairs.
 */
inline std::vector<std::pair<callstack, frames_statistics>>
sort_callstacks_stats(callstacks_statistics const& cs_stats)
{
    std::vector<std::pair<callstack, frames_statistics>> sorted_cs_stats(cs_stats.cbegin(),
                                                                         cs_stats.cend());
    std::sort(sorted_cs_stats.begin(), sorted_cs_stats.end(),
              [](std::pair<callstack, frames_statistics> const& l,
                 std::pair<callstack, frames_statistics> const& r)
              {
                  const auto l_val = l.second[0].num_measurements * l.second[0].mean.count();
                  const auto r_val = r.second[0].num_measurements * r.second[0].mean.count();
                  return l_val < r_val;
              });
    return sorted_cs_stats;
}

/**
 * Sorts statistics of functions so that the most contributing ones appear first. Contribution
 * factor is calculated as the number of measurements times mean duration.
 *
 * @param fn_stats Statistics of functions to sort.
 *
 * @return A container of sorted <function name, function statistics> pairs.
 */
inline std::vector<std::pair<std::string, func_statistic>>
sort_functions_stats(functions_statistics const& fn_stats)
{
    std::vector<std::pair<std::string, func_statistic>> sorted_fn_stats(fn_stats.cbegin(),
                                                                        fn_stats.cend());
    std::sort(sorted_fn_stats.begin(), sorted_fn_stats.end(),
              [](std::pair<std::string, func_statistic> const& l,
                 std::pair<std::string, func_statistic> const& r)
              {
                  const auto l_val = l.second.num_measurements * l.second.mean.count();
                  const auto r_val = r.second.num_measurements * r.second.mean.count();
                  return r_val < l_val;
              });

    return sorted_fn_stats;
}

/**
 * Gets the textual representation of gathered function's statistics to form a row in a table view.
 * Also, updates the columns' widths to capture their maximum widths.
 *
 * @param function_name A textual representation of function name.
 *
 * @param fn_stats Statistics of functions.
 *
 * @param columns_widths A vector with widths of table columns to update.
 *
 * @return A container with textual representation of function's statistics.
 */
inline func_statistic_view make_func_statistic_view(std::string const& function_name,
                                                    func_statistic const& fn_stats,
                                                    std::vector<unsigned>& columns_widths)
{
    func_statistic_view view; view.reserve(columns_widths.size());

    view.push_back(function_name);
    view.push_back(std::to_string(fn_stats.num_measurements));
    view.push_back(std::to_string(fn_stats.min.count()));
    view.push_back(std::to_string(fn_stats.median.count()));
    view.push_back(std::to_string(fn_stats.mean.count()));
    view.push_back(std::to_string(fn_stats.max.count()));

    return view;
}

/**
 * Prepares the gathered data for presentation. Sorts the data and adjusts the widths of table
 * columns accordingly.
 *
 * @param cs_stats Callstacks statistics.
 *
 * @param fn_stats Function statistics.
 *
 * @param indent Indentation level per each nested callstack frame.
 *
 * @param indent_symbol Symbol to use for indentation.
 *
 * @param columns Column names in table
 *
 * @return A tuple consisting of widths of table columns, callstacks and functions representation.
 */
inline std::tuple<std::vector<unsigned> /*columns widths*/,
                  callstacks_statistics_view,
                  functions_statistics_view>
prepare_for_presentation(callstacks_statistics const& cs_stats, functions_statistics const& fn_stats,
                         unsigned const indent, char const indent_symbol,
                         std::vector<std::string> const& columns)
{
    std::vector<unsigned> columns_widths = determine_column_widths(
        columns, /*common_width_start_column*/2
    );

    auto sorted_cs_stats = sort_callstacks_stats(cs_stats);
    auto sorted_fn_stats = sort_functions_stats(fn_stats);

    callstacks_statistics_view callstacks_representation;
    for (auto const& cs_stat : sorted_cs_stats) {
        callstack const& cs = cs_stat.first;
        frames_statistics const& frames_stats = cs_stat.second;

        callstack_view cs_representation;
        for (unsigned i = 0; i < cs.function_names.size(); ++i) {
            std::string function_name =
                /*indentation*/std::string(indent * cs.nesting_levels[i], indent_symbol) +
                prettify(cs.function_names[i]);
            auto frame_view = make_func_statistic_view(function_name, frames_stats[i],
                                                       columns_widths);
            fit_column_widths(columns_widths, frame_view);
            cs_representation.push_back(std::move(frame_view));
        }
        callstacks_representation.push_back(std::move(cs_representation));
    }

    functions_statistics_view fns_view;
    for (auto const& fn_stat : sorted_fn_stats) {
        func_statistic const& stat = fn_stat.second;
        auto fn_view = make_func_statistic_view(/*function name*/prettify(fn_stat.first), stat,
                                                columns_widths);
        fit_column_widths(columns_widths, fn_view);
        fns_view.push_back(std::move(fn_view));
    }

    return std::make_tuple(columns_widths, callstacks_representation, fns_view);
}

/**
 * Outputs measurement statistics of each callstack and function.
 *
 * @param out An output stream where to send the data.
 *
 * @param cs_stats Statistics of frames in callstacks.
 *
 * @param fn_stats Statistics of functions.
 */
inline void
print(std::ostream& out, callstacks_statistics const& cs_stats, functions_statistics const& fn_stats)
{
    unsigned const indent = 2;  // number of indent symbols per each nesting level
    char const indent_symbol = ' ';
    std::vector<std::string> const columns = {
        "Function name", "# calls", "Minimum", "Median", "Mean", "Maximum"
    };
    std::string const intercolumn_space = " ";

    auto const [fields_widths, cs_stats_view, fn_stats_view] = prepare_for_presentation(
        cs_stats, fn_stats, indent, indent_symbol, columns
    );
    unsigned const table_width = std::accumulate(fields_widths.cbegin(), fields_widths.cend(), 0) +
                                 unsigned((fields_widths.size() - 1) * intercolumn_space.size());
    std::string const table_row_separator = std::string(table_width, '-');

    out << "\nCallstacks statistics in nanoseconds from the least to the most contributors "
           "(number of calls * mean):\n";
    print_table_row(out, fields_widths, intercolumn_space, columns);
    out << table_row_separator << std::endl;

    for (auto const& cs_view : cs_stats_view) {
        for (auto const& func_stats_view : cs_view) {
            print_table_row(out, fields_widths, intercolumn_space, func_stats_view);
        }
        out << table_row_separator << std::endl;
    }

    out << "\nFunctions statistics in nanoseconds from the most to least contributors "
           "(number of calls * mean):\n";
    print_table_row(out, fields_widths, intercolumn_space, columns);
    out << table_row_separator << std::endl;

    for (auto const& fn_view : fn_stats_view) {
        print_table_row(out, fields_widths, intercolumn_space, fn_view);
    }
}

/**
 * Analyzes the gathered data and presents it to the user.
 *
 * @param trace Gathered data
 */
inline void post_process(const std::deque<thread_logger<function_call_event>>& trace)
{
    callstacks_data callstacks = build_callstack_map(trace);
    functions_data functions = build_function_map(callstacks);
    sort_measurements(callstacks, functions);
    auto [cs_stats, fn_stats] = compute_statistics(callstacks, functions);
    print(std::cout, cs_stats, fn_stats);
}

// The global object that gathers data for analysis and presenting to the user
inline tracer<function_call_event> g_trace_log(post_process);

class tcm_function_profiling_guard {
public:
    tcm_function_profiling_guard(const char* unique_func_name,
                                 tracer<function_call_event>& t = g_trace_log)
        : m_func_name(unique_func_name), m_tracer(t)
    {
        m_tracer.get_thread_logger().log(m_func_name, current_time_point());
    }

    ~tcm_function_profiling_guard()
    {
        m_tracer.get_thread_logger().log(m_func_name, current_time_point());
    }

private:
    const char* m_func_name = nullptr;
    tracer<function_call_event>& m_tracer;
};

inline tcm_function_profiling_guard make_function_profiling_guard(const char* func_name)
{
    return tcm_function_profiling_guard(func_name);
}

#define __TCM_PROFILE_THIS_FUNCTION_AUX(function_name)                                    \
  const auto tcm_function_profiling_guard_obj =                                           \
    ::tcm::internal::profiling::callstack::make_function_profiling_guard(function_name)

#if _WIN32
#define __TCM_PROFILE_THIS_FUNCTION()                                                     \
  __TCM_PROFILE_THIS_FUNCTION_AUX((const char *)(&__FUNCSIG__))
#else // _WIN32
#define __TCM_PROFILE_THIS_FUNCTION()                                                     \
  __TCM_PROFILE_THIS_FUNCTION_AUX((const char *)(&__PRETTY_FUNCTION__))
#endif // _WIN32

} // callstack
} // profiling
} // internal
} // tcm

#else  // __TCM_ENABLE_CALLSTACK_TRACER
#define __TCM_PROFILE_THIS_FUNCTION()
#endif // __TCM_ENABLE_CALLSTACK_TRACER

#endif // __TCM_TIME_TRACER_HEADER
