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

#include <oneapi/tbb/cache_aligned_allocator.h>
#include <oneapi/tbb/enumerable_thread_specific.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

// Trace collection helper class
//
// In lazy mode each thread appends to its own buffer and recording takes no lock.
// In eager mode every event is written to the file as it is recorded, under a mutex.
class TraceCollector {
public:
    enum class WriteMode {
        LAZY,   // Buffer in memory, write at end
        EAGER   // Write immediately to file
    };

private:
    struct trace_event {
        std::string name;
        int64_t ts = 0;    // microseconds from start
        int64_t dur = 0;   // duration in microseconds (only for 'X')
        int tid = 0;
        char phase = 0;    // 'B', 'E', 'X', 'i', or 'M' (a thread name)
    };

    // Lazy mode: one thread's events, as a list of fixed-size chunks.
    struct event_chunk {
        static constexpr std::size_t capacity = 1024;
        trace_event events[capacity];
        event_chunk* next = nullptr;
    };

    struct thread_buffer {
        event_chunk* head = nullptr;            // set before the buffer is registered
        event_chunk* tail = nullptr;            // owner thread only
        std::size_t tail_used = 0;              // owner thread only
        std::atomic<std::size_t> published{0};  // events completely written
        thread_buffer* next_registered = nullptr;

        thread_buffer() = default;
        thread_buffer(const thread_buffer&) = delete;
        thread_buffer& operator=(const thread_buffer&) = delete;

        ~thread_buffer() {
            while (head) {
                event_chunk* next = head->next;
                delete head;
                head = next;
            }
        }
    };

    WriteMode write_mode;
    std::string filename;
    int pid;
    std::string category;

    // Lazy mode members.
    oneapi::tbb::enumerable_thread_specific<
        thread_buffer,
        oneapi::tbb::cache_aligned_allocator<thread_buffer>,
        oneapi::tbb::ets_key_per_instance> buffers;
    std::atomic<thread_buffer*> registered{nullptr};
    std::mutex flush_mtx;  // serialises flush() and the destructor; never held while recording
    bool lazy_written;     // guarded by flush_mtx

    // Eager mode members
    std::mutex mtx;
    std::ofstream trace_file;
    bool file_opened;
    bool first_event;  // Track if we need comma before next event

public:
    std::chrono::time_point<std::chrono::steady_clock> start_time;

private:
    int64_t now_us() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_time).count();
    }

    // Write one event as a JSON object, without a separator.
    void write_entry(std::ostream& out, char phase, const std::string& name,
                     int tid, int64_t ts, int64_t dur) const {
        if (phase == 'M') {
            out << "  {\"name\": \"thread_name\", \"ph\": \"M\", \"pid\": " << pid
                << ", \"tid\": " << tid
                << ", \"args\": {\"name\": \"" << name << "\"}}";
            return;
        }
        out << "  {\"name\": \"" << name << "\""
            << ", \"cat\": \"" << category << "\""
            << ", \"ph\": \"" << phase << "\""
            << ", \"ts\": " << ts;
        if (phase == 'i') {
            out << ", \"s\": \"g\"";  // Scope: global (required for instant events)
        }
        out << ", \"pid\": " << pid
            << ", \"tid\": " << tid;
        if (phase == 'X') {
            out << ", \"dur\": " << dur;
        }
        out << "}";
    }

    // The calling thread's buffer, registered the first time the thread records.
    thread_buffer& local_buffer() {
        bool exists = false;
        thread_buffer& b = buffers.local(exists);
        if (!exists) {
            b.head = b.tail = new event_chunk;
            thread_buffer* h = registered.load(std::memory_order_relaxed);
            do {
                b.next_registered = h;
            } while (!registered.compare_exchange_weak(h, &b, std::memory_order_release,
                                                       std::memory_order_relaxed));
        }
        return b;
    }

    // Lazy mode: append to the calling thread's buffer. No lock.
    void append(char phase, const std::string& name, int tid, int64_t ts, int64_t dur) {
        thread_buffer& b = local_buffer();
        if (b.tail_used == event_chunk::capacity) {
            event_chunk* c = new event_chunk;
            b.tail->next = c;
            b.tail = c;
            b.tail_used = 0;
        }
        trace_event& e = b.tail->events[b.tail_used++];
        e.name = name;
        e.ts = ts;
        e.dur = dur;
        e.tid = tid;
        e.phase = phase;
        b.published.store(b.published.load(std::memory_order_relaxed) + 1,
                          std::memory_order_release);
    }

    void record(char phase, const std::string& name, int tid, int64_t ts, int64_t dur) {
        if (write_mode == WriteMode::LAZY) {
            append(phase, name, tid, ts, dur);
            return;
        }
        std::lock_guard<std::mutex> lock(mtx);
        if (!file_opened) {
            return;
        }
        if (!first_event) {
            trace_file << ",\n";
        }
        first_event = false;
        write_entry(trace_file, phase, name, tid, ts, dur);
        trace_file.flush();  // Ensure immediate write to disk
    }

    // Calls f on each of the first n events of b, in the order they were recorded.
    template <typename F>
    static void for_each_event(const thread_buffer& b, std::size_t n, F f) {
        for (const event_chunk* c = b.head; n > 0; ) {
            const std::size_t k = n < event_chunk::capacity ? n : event_chunk::capacity;
            for (std::size_t i = 0; i < k; ++i) {
                f(c->events[i]);
            }
            n -= k;
            if (n > 0) {
                c = c->next;  // only read when an event in the next chunk is published
            }
        }
    }

    // Write all events in lazy mode (called by flush() and the destructor). Other
    // threads may still be recording: what they had published when this started is
    // written, and anything later is not.
    void write_lazy_trace() {
        std::lock_guard<std::mutex> lock(flush_mtx);

        // Safe to call multiple times (e.g., signal + destructor)
        if (lazy_written) {
            return;
        }
        lazy_written = true;

        // Take each buffer's count once, so both passes below write the same events.
        // The list is newest first; write threads in the order they first recorded.
        std::vector<std::pair<const thread_buffer*, std::size_t>> snapshot;
        for (const thread_buffer* b = registered.load(std::memory_order_acquire); b;
             b = b->next_registered) {
            snapshot.emplace_back(b, b->published.load(std::memory_order_acquire));
        }
        std::reverse(snapshot.begin(), snapshot.end());

        // Write JSON Array Format: thread names first, then each thread's events.
        std::ofstream out_file(filename);
        out_file << "[\n";
        bool first = true;
        auto write_if = [&](bool metadata) {
            for (const auto& [b, n] : snapshot) {
                for_each_event(*b, n, [&](const trace_event& e) {
                    if ((e.phase == 'M') != metadata) {
                        return;
                    }
                    if (!first) {
                        out_file << ",\n";
                    }
                    first = false;
                    write_entry(out_file, e.phase, e.name, e.tid, e.ts, e.dur);
                });
            }
        };
        write_if(true);
        write_if(false);
        if (!first) {
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
        , lazy_written(false)
        , file_opened(false)
        , first_event(true)
        , start_time(std::chrono::steady_clock::now())
    {
        if (write_mode == WriteMode::EAGER) {
            trace_file.open(filename);
            trace_file << "[\n";
            file_opened = true;
        }
    }

    ~TraceCollector() {
        if (write_mode == WriteMode::EAGER) {
            std::lock_guard<std::mutex> lock(mtx);
            if (file_opened) {
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
        record('M', name, tid, 0, 0);
    }

    // Record a BEGIN or END event
    void record_event(const std::string& name, const std::string& phase, int tid) {
        record(phase.empty() ? '?' : phase[0], name, tid, now_us(), 0);
    }

    // Record a DURATION event (single event with start time and duration)
    void record_duration_event(const std::string& name, int tid, int64_t ts_us, int64_t dur_us) {
        record('X', name, tid, ts_us, dur_us);
    }

    // Record an INSTANT event (phase "i")
    void record_instant_event(const std::string& name, int tid) {
        record('i', name, tid, now_us(), 0);
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
