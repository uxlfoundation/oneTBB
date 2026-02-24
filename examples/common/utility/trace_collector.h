/*
    Copyright (c) 2026 UXL Foundation Contributors
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
private:
    struct trace_event {
        std::string name;
        std::string phase;  // "B" or "E"
        int tid;
        int64_t ts;  // microseconds from start
    };
    
    std::chrono::time_point<std::chrono::steady_clock> start_time;
    std::vector<trace_event> events;
    std::mutex mtx;

public:
    TraceCollector() : start_time(std::chrono::steady_clock::now()) {}
    
    // Record a BEGIN or END event
    void record_event(const std::string& name, const std::string& phase, int tid) {
        auto now = std::chrono::steady_clock::now();
        auto ts_us = std::chrono::duration_cast<std::chrono::microseconds>(now - start_time).count();
        std::lock_guard<std::mutex> lock(mtx);
        events.push_back({name, phase, tid, ts_us});
    }
    
    // Write trace to JSON file
    void write_trace(const std::string& filename, 
                     int pid, 
                     const std::string& category,
                     const std::vector<std::pair<int, std::string>>& thread_names) {
        // Sort events by timestamp
        std::sort(events.begin(), events.end(), 
            [](const trace_event& a, const trace_event& b) { return a.ts < b.ts; });

        // Write JSON Array Format
        std::ofstream trace_file(filename);
        trace_file << "[\n";
        
        // Add thread name metadata events
        for (size_t i = 0; i < thread_names.size(); ++i) {
            int tid = thread_names[i].first;
            const std::string& name = thread_names[i].second;
            trace_file << "  {\"name\": \"thread_name\", \"ph\": \"M\", \"pid\": " << pid
                       << ", \"tid\": " << tid
                       << ", \"args\": {\"name\": \"" << name << "\"}}";
            if (i < thread_names.size() - 1 || !events.empty()) {
                trace_file << ",";
            }
            trace_file << "\n";
        }
        
        // Write trace events
        for (size_t i = 0; i < events.size(); ++i) {
            const auto& e = events[i];
            trace_file << "  {\"name\": \"" << e.name << "\""
                       << ", \"cat\": \"" << category << "\""
                       << ", \"ph\": \"" << e.phase << "\""
                       << ", \"ts\": " << e.ts
                       << ", \"pid\": " << pid
                       << ", \"tid\": " << e.tid
                       << "}";
            if (i < events.size() - 1) trace_file << ",";
            trace_file << "\n";
        }
        trace_file << "]\n";
        trace_file.close();
        std::printf("Trace written to %s\n", filename.c_str());
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
