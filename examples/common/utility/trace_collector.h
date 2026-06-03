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
        std::string phase;  // "B", "E", or "X"
        int tid;
        int64_t ts;  // microseconds from start
        int64_t dur;  // duration in microseconds (only for "X" phase)
    };

    WriteMode write_mode;
    std::string filename;
    int pid;
    std::string category;
    std::vector<trace_event> events;  // Only used in lazy mode
    std::vector<std::pair<int, std::string>> thread_names;
    std::mutex mtx;

    // Eager mode members
    std::ofstream trace_file;
    bool file_opened;
    bool first_event;  // Track if we need comma before next event
    bool lazy_written;  // Track if lazy trace already written

public:
    std::chrono::time_point<std::chrono::steady_clock> start_time;

private:

    // Write all events in lazy mode (called by destructor)
    void write_lazy_trace() {
        std::lock_guard<std::mutex> lock(mtx);

        // Safe to call multiple times (e.g., signal + destructor)
        if (lazy_written) {
            return;
        }
        lazy_written = true;

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
                     << ", \"ts\": " << e.ts;
            if (e.phase == "i") {
                out_file << ", \"s\": \"g\"";  // Scope: global for instant events
            }
            out_file << ", \"pid\": " << pid
                     << ", \"tid\": " << e.tid;
            if (e.phase == "X") {
                out_file << ", \"dur\": " << e.dur;
            }
            out_file << "}";
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
        , lazy_written(false)
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

    // Force flush of lazy trace (safe to call multiple times)
    void flush() {
        if (write_mode == WriteMode::LAZY) {
            write_lazy_trace();
        }
    }

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
            events.push_back({name, phase, tid, ts_us, 0});
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

    // Record a DURATION event (single event with start time and duration)
    void record_duration_event(const std::string& name, int tid, int64_t ts_us, int64_t dur_us) {
        std::lock_guard<std::mutex> lock(mtx);

        if (write_mode == WriteMode::LAZY) {
            // Lazy mode: buffer in memory
            events.push_back({name, "X", tid, ts_us, dur_us});
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
                           << ", \"ph\": \"X\""
                           << ", \"ts\": " << ts_us
                           << ", \"dur\": " << dur_us
                           << ", \"pid\": " << pid
                           << ", \"tid\": " << tid
                           << "}";
                trace_file.flush();  // Ensure immediate write to disk
            }
        }
    }

    // Record an INSTANT event (phase "i")
    void record_instant_event(const std::string& name, int tid) {
        auto now = std::chrono::steady_clock::now();
        auto ts_us = std::chrono::duration_cast<std::chrono::microseconds>(now - start_time).count();

        std::lock_guard<std::mutex> lock(mtx);

        if (write_mode == WriteMode::LAZY) {
            // Lazy mode: buffer in memory (duration field unused for instant events)
            events.push_back({name, "i", tid, ts_us, 0});
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
                           << ", \"ph\": \"i\""
                           << ", \"ts\": " << ts_us
                           << ", \"s\": \"g\""  // Scope: global (required for instant events)
                           << ", \"pid\": " << pid
                           << ", \"tid\": " << tid
                           << "}";
                trace_file.flush();
            }
        }
    }
};

// RAII class for scoped trace events
class ScopedTraceEvent {
    TraceCollector& collector;
    std::string name;
    int tid;
    std::chrono::time_point<std::chrono::steady_clock> start_time;
    bool use_duration;
public:
    // Constructor for duration events (recommended to avoid nesting issues)
    ScopedTraceEvent(TraceCollector& c, const std::string& n, int t, bool use_dur = true)
        : collector(c), name(n), tid(t), use_duration(use_dur) {
        if (use_duration) {
            start_time = std::chrono::steady_clock::now();
        } else {
            collector.record_event(name, "B", tid);
        }
    }
    ~ScopedTraceEvent() {
        if (use_duration) {
            auto end_time = std::chrono::steady_clock::now();
            auto ts_us = std::chrono::duration_cast<std::chrono::microseconds>(start_time - collector.start_time).count();
            auto dur_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
            collector.record_duration_event(name, tid, ts_us, dur_us);
        } else {
            collector.record_event(name, "E", tid);
        }
    }

    // Make start_time accessible to record_duration_event
    friend class TraceCollector;
};

#endif // TRACE_COLLECTOR_H
