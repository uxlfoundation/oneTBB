/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#ifndef __TCM_TIME_TRACER_HEADER
#define __TCM_TIME_TRACER_HEADER


#if __TCM_ENABLE_TIME_TRACER
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <deque>
#include <chrono>
#include <iostream>
#include <numeric>
#endif

namespace tcm {
namespace internal {

enum class tcm_events {
    request_permit,
    deactivate_permit,
    idle_permit,
    activate_permit,
    release_permit,
    adjust_existing_permit,
    try_satisfy_request,
    calculate_updates,
    negotiate,
    apply,
    invoke_callbacks,
    renegotiate_permits
};

#if __TCM_ENABLE_TIME_TRACER
struct time_tracer_data {
    time_tracer_data(time_tracer_data** ptr) : m_td_ptr(ptr)
    {}

    ~time_tracer_data() {
        *m_td_ptr = nullptr;
    }

    void register_event(tcm_events event) {
        events.emplace_back(event_and_time{event, std::chrono::steady_clock::now()});
    }

    void unregister_event(tcm_events event) {
        events.emplace_back(event_and_time{event, std::chrono::steady_clock::now()});

        tcm_events top_event = events.front().event;
        if (top_event == events.back().event) {
            calculate_events_duration(top_event);
            events.clear();
        }
    }

    void calculate_events_duration(tcm_events top_event) {
        std::unordered_map<tcm_events, std::chrono::steady_clock::time_point> events_first_entry;

        for (auto e_t : events) {
            auto it = events_first_entry.find(e_t.event);
            if (it != events_first_entry.end()) {
                auto event_duration = (e_t.time_point - it->second);
                if (e_t.event != top_event) {
                    nested_events_durations[top_event][e_t.event].push_back(event_duration.count());
                } else {
                    top_events_durations[top_event].push_back(
                        std::chrono::duration_cast<std::chrono::microseconds>(event_duration)
                        .count()
                    );
                }

                events_first_entry.erase(it);
            } else {
                events_first_entry.insert(std::make_pair(e_t.event, e_t.time_point));
            }
        }
    }

    struct event_and_time {
        tcm_events event;
        std::chrono::steady_clock::time_point time_point;
    };

    std::deque<event_and_time> events;

    std::unordered_map<tcm_events,
        std::vector<unsigned long>> top_events_durations;
    std::unordered_map<tcm_events,
        std::unordered_map<tcm_events, std::vector<unsigned long>>> nested_events_durations;

    time_tracer_data** m_td_ptr{nullptr};
};

class time_tracer_type {
public:
    ~time_tracer_type() {
        std::map<tcm_events, std::string> top_event_names = {
            std::make_pair(tcm_events::request_permit, "request_permit"),
            std::make_pair(tcm_events::deactivate_permit, "deactivate_permit"),
            std::make_pair(tcm_events::idle_permit, "idle_permit"),
            std::make_pair(tcm_events::activate_permit, "activate_permit"),
            std::make_pair(tcm_events::release_permit, "release_permit")
        };

        std::map<tcm_events, std::string> nested_event_names = {
            std::make_pair(tcm_events::adjust_existing_permit, "adjust_existing_permit"),
            std::make_pair(tcm_events::try_satisfy_request, "try_satisfy_request"),
            std::make_pair(tcm_events::calculate_updates, "calculate_updates"),
            std::make_pair(tcm_events::negotiate, "negotiate"),
            std::make_pair(tcm_events::apply, "apply"),
            std::make_pair(tcm_events::invoke_callbacks, "invoke_callbacks"),
            std::make_pair(tcm_events::renegotiate_permits, "renegotiate_permits")
        };

        for (auto& top_event : top_event_names) {
            std::vector<unsigned long> top_per_thread_avg;
            std::unordered_map<tcm_events, std::vector<unsigned long>> nested_per_thread_avg;

            for (auto& th_data : data) {
                auto& top_event_durations = th_data.top_events_durations[top_event.first];
                if (!top_event_durations.empty()) {
                    // Calculate AVG durations for top events
                    top_per_thread_avg.push_back(std::accumulate(top_event_durations.begin(),
                        top_event_durations.end(), 0) / top_event_durations.size());

                    for (auto nested_event : nested_event_names) {
                        auto& nested_event_durations =
                            th_data.nested_events_durations[top_event.first][nested_event.first];
                        if (!nested_event_durations.empty()) {
                            // Calculate AVG durations for nested events
                            nested_per_thread_avg[nested_event.first].push_back(
                                std::accumulate(nested_event_durations.begin(),
                                nested_event_durations.end(), 0) / nested_event_durations.size());
                        }
                    }
                }
            }

            if (!top_per_thread_avg.empty()) {
                auto avg_top_event_duration = std::accumulate(top_per_thread_avg.begin(),
                    top_per_thread_avg.end(), 0) / top_per_thread_avg.size();
                std::cout << top_event.second << " avg time = "
                          << avg_top_event_duration << "us" << std::endl;

                for (auto nested_event : nested_event_names) {
                    auto& nested_avg_times = nested_per_thread_avg[nested_event.first];
                    if (!nested_avg_times.empty()) {
                        auto avg_nested_event_duration = std::accumulate(nested_avg_times.begin(),
                            nested_avg_times.end(), 0) / nested_avg_times.size();
                        std::cout << "  > " << nested_event.second << " avg time = "
                                  << avg_nested_event_duration << "ns" << std::endl;
                    }
                }
                std::cout << std::endl;
            }
        }
    }

    void register_event(tcm_events event) {
        auto td = thread_time_data ? thread_time_data : init_thread_local_data();
        td->register_event(event);
    }

    void unregister_event(tcm_events event) {
        auto td = thread_time_data ? thread_time_data : init_thread_local_data();
        td->unregister_event(event);
    }

private:
    time_tracer_data* init_thread_local_data() {
        std::lock_guard<std::mutex> lock(data_mutex);
        data.emplace_back(&thread_time_data);

        thread_time_data = &data[data.size() - 1];

        return thread_time_data;
    }

    std::mutex data_mutex;
    std::deque<time_tracer_data> data;
    static thread_local time_tracer_data* thread_time_data;
};

thread_local time_tracer_data* time_tracer_type::thread_time_data{nullptr};

class time_tracer_guard {
public:
    time_tracer_guard(time_tracer_type& t, tcm_events e) : time_tracer(&t), event(e) {
        time_tracer->register_event(event);
    }

    time_tracer_guard(const time_tracer_guard&) = delete;
    time_tracer_guard(time_tracer_guard&&) = delete;

    ~time_tracer_guard() {
        dismiss();
    }

    void dismiss() {
        if (time_tracer) {
            time_tracer->unregister_event(event);
            time_tracer = nullptr;
        }
    }

private:
    time_tracer_type* time_tracer;
    tcm_events event;
};

time_tracer_guard make_event_duration_tracer(time_tracer_type& tr, tcm_events event) {
    return time_tracer_guard{tr, event};
}

#else
struct time_tracer_type {};
struct time_tracer_guard {
    time_tracer_guard() {}
    ~time_tracer_guard() {}
    void dismiss() {}
};

time_tracer_guard make_event_duration_tracer(time_tracer_type&, tcm_events) {
    return time_tracer_guard{};
}

#endif // __TCM_ENABLE_TIME_TRACER

} // internal
} // tcm

#endif // __TCM_TIME_TRACER_HEADER
