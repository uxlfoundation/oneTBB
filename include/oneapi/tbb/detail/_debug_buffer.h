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

#ifndef __TBB_detail_debug_buffer_H
#define __TBB_detail_debug_buffer_H

#include <string>
#include <vector>
#include <mutex>
#include <cstdio>
#include <cstdarg>
#include <atomic>
#include <chrono>

namespace tbb {
namespace detail {
namespace d1 {

// Thread-safe buffered debug logger that writes on demand
class debug_buffer {
private:
    struct log_entry {
        std::chrono::time_point<std::chrono::steady_clock> timestamp;
        std::string message;
    };

    std::vector<log_entry> entries;
    mutable std::mutex mtx;
    std::chrono::time_point<std::chrono::steady_clock> start_time;
    bool flushed;

public:
    debug_buffer()
        : start_time(std::chrono::steady_clock::now())
        , flushed(false)
    {}

    // Printf-style buffered logging
    void log(const char* format, ...) {
        char buffer[512];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        auto now = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(mtx);
        entries.push_back({now, std::string(buffer)});
    }

    // Flush all buffered messages to file
    void flush(const char* filename = "debug_output.txt") {
        std::lock_guard<std::mutex> lock(mtx);

        if (flushed) {
            return; // Already flushed
        }
        flushed = true;

        std::FILE* f = std::fopen(filename, "w");
        if (!f) {
            std::fprintf(stderr, "ERROR: Could not open %s for writing\n", filename);
            return;
        }

        std::fprintf(f, "Debug output: %zu entries\n", entries.size());
        std::fprintf(f, "=====================================\n\n");

        for (const auto& entry : entries) {
            auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                entry.timestamp - start_time).count();
            std::fprintf(f, "[%8lld us] %s", (long long)elapsed_us, entry.message.c_str());
        }

        std::fclose(f);
        std::fprintf(stderr, "\nDebug buffer flushed to %s (%zu entries)\n", filename, entries.size());
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mtx);
        return entries.size();
    }
};

// Global debug buffer instance
inline std::atomic<debug_buffer*> g_debug_buffer{nullptr};

// Helper function to get or create the global buffer
inline debug_buffer* get_debug_buffer() {
    debug_buffer* buf = g_debug_buffer.load(std::memory_order_acquire);
    if (!buf) {
        static debug_buffer instance;
        g_debug_buffer.store(&instance, std::memory_order_release);
        buf = &instance;
    }
    return buf;
}

// Macro for conditional debug logging
#if __TBB_DEBUG_RESOURCE_ACQUISITION
    #define TBB_DEBUG_LOG(...) tbb::detail::d1::get_debug_buffer()->log(__VA_ARGS__)
    #define TBB_DEBUG_FLUSH(filename) tbb::detail::d1::get_debug_buffer()->flush(filename)
#else
    #define TBB_DEBUG_LOG(...) ((void)0)
    #define TBB_DEBUG_FLUSH(filename) ((void)0)
#endif

} // namespace d1
} // namespace detail
} // namespace tbb

#endif // __TBB_detail_debug_buffer_H
