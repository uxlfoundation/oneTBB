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

#ifndef TRACE_COLLECTOR_H
#define TRACE_COLLECTOR_H

#include <chrono>
#include <fstream>
#include <mutex>
#include <algorithm>
#include <string>
#include <vector>
#include <utility>
#include <cstdio>

// Trace collection helper class
class TraceCollector {
public:
    enum class WriteMode {
        LAZY,   // Buffer in memory, write at end
        EAGER   // Write immediately to file
    };

private:
    struct trace_event {
        std::string name;
        std::string phase;  // "B" or "E"
        int tid;
        int64_t ts;  // microseconds from start
    };

    WriteMode write_mode;
    std::string filename;
    int pid;
    std::string category;
    std::chrono::time_point<std::chrono::steady_clock> start_time;
    std::vector<trace_event> events;  // Only used in lazy mode
    std::vector<std::pair<int, std::string>> thread_names;
    std::mutex mtx;

    // Eager mode members
    std::ofstream trace_file;
    bool file_opened;
    bool first_event;  // Track if we need comma before next event

    // Write all events in lazy mode (called by destructor)
    void write_lazy_trace() {
        std::lock_guard<std::mutex> lock(mtx);
        std::sort(events.begin(), events.end(),
            [](const trace_event& a, const trace_event& b) { return a.ts < b.ts; });

        // Write JSON Array Format
        std::ofstream out_file(filename);
        out_file << "[\n";

        // Add thread name metadata events
        for (size_t i = 0; i < thread_names.size(); ++i) {
            int tid = thread_names[i].first;
            const std::string& name = thread_names[i].second;
            out_file << "  {\"name\": \"thread_name\", \"ph\": \"M\", \"pid\": " << pid
                     << ", \"tid\": " << tid
                     << ", \"args\": {\"name\": \"" << name << "\"}}";
            if (i < thread_names.size() - 1 || !events.empty()) {
                out_file << ",";
            }
            out_file << "\n";
        }

        // Write trace events
        for (size_t i = 0; i < events.size(); ++i) {
            const auto& e = events[i];
            out_file << "  {\"name\": \"" << e.name << "\""
                     << ", \"cat\": \"" << category << "\""
                     << ", \"ph\": \"" << e.phase << "\""
                     << ", \"ts\": " << e.ts
                     << ", \"pid\": " << pid
                     << ", \"tid\": " << e.tid
                     << "}";
            if (i < events.size() - 1) out_file << ",";
            out_file << "\n";
        }
        out_file << "]\n";
        out_file.close();
        std::printf("Lazy trace written to %s\n", filename.c_str());
    }

public:
    TraceCollector(const std::string& fname,
                   int pid_val,
                   const std::string& cat,
                   WriteMode mode = WriteMode::LAZY)
        : write_mode(mode)
        , filename(fname)
        , pid(pid_val)
        , category(cat)
        , start_time(std::chrono::steady_clock::now())
        , file_opened(false)
        , first_event(true)
    {
        if (write_mode == WriteMode::EAGER) {
            trace_file.open(filename);
            trace_file << "[\n";
            file_opened = true;
        }
    }

    ~TraceCollector() {
        if (write_mode == WriteMode::EAGER) {
            if (file_opened) {
                std::lock_guard<std::mutex> lock(mtx);
                trace_file << "\n]\n";
                trace_file.close();
                std::printf("Eager trace written to %s\n", filename.c_str());
            }
        } else {
            // Lazy mode: write everything now
            write_lazy_trace();
        }
    }

    WriteMode get_mode() const { return write_mode; }

    void add_thread_name(int tid, const std::string& name) {
        std::lock_guard<std::mutex> lock(mtx);

        if (write_mode == WriteMode::EAGER && file_opened) {
            // Write immediately in eager mode
            if (!first_event) trace_file << ",\n";
            trace_file << "  {\"name\": \"thread_name\", \"ph\": \"M\", "
                       << "\"pid\": " << pid
                       << ", \"tid\": " << tid
                       << ", \"args\": {\"name\": \"" << name << "\"}}";
            trace_file.flush();
            first_event = false;
        } else {
            // Buffer for lazy mode
            thread_names.emplace_back(tid, name);
        }
    }


    // Record a BEGIN or END event
    void record_event(const std::string& name, const std::string& phase, int tid) {
        auto now = std::chrono::steady_clock::now();
        auto ts_us = std::chrono::duration_cast<std::chrono::microseconds>(now - start_time).count();

        std::lock_guard<std::mutex> lock(mtx);

        if (write_mode == WriteMode::LAZY) {
            // Lazy mode: buffer in memory
            events.push_back({name, phase, tid, ts_us});
        } else {
            // Eager mode: write immediately
            if (file_opened) {
                if (!first_event) {
                    trace_file << ",\n";
                } else {
                    first_event = false;
                }
                trace_file << "  {\"name\": \"" << name << "\""
                           << ", \"cat\": \"" << category << "\""
                           << ", \"ph\": \"" << phase << "\""
                           << ", \"ts\": " << ts_us
                           << ", \"pid\": " << pid
                           << ", \"tid\": " << tid
                           << "}";
                trace_file.flush();  // Ensure immediate write to disk
            }
        }
    }
};

// RAII class for scoped trace events
class ScopedTraceEvent {
    TraceCollector& collector;
    std::string name;
    int tid;
public:
    ScopedTraceEvent(TraceCollector& c, const std::string& n, int t)
        : collector(c), name(n), tid(t) {
        collector.record_event(name, "B", tid);
    }
    ~ScopedTraceEvent() {
        collector.record_event(name, "E", tid);
    }
};

#endif // TRACE_COLLECTOR_H
