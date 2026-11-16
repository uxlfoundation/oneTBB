#ifndef __TBB_trace_H
#define __TBB_trace_H

#ifndef COLLECT_TRACE
#define COLLECT_TRACE 0
#endif

#if COLLECT_TRACE

#include "trace_collector.h"

inline TraceCollector* trace_collector_ptr = nullptr;

inline void initialize_trace_collector(const char* file_name, int pid, const char* category) {
    if (trace_collector_ptr != nullptr) {
        throw std::runtime_error("trace_collector already initialized");
    }
    trace_collector_ptr = new TraceCollector(file_name, pid, category);
}

inline void finish_tracing() {
    delete trace_collector_ptr;
}

inline void add_tracing_threads(int start_index, int finish_index) {
    for (int index = start_index; index != finish_index; ++index) {
        trace_collector_ptr->add_thread_name(index, "thread");
    }
}

inline void record_instant_tracing_event(const char* name, int tid) {
    trace_collector_ptr->record_instant_event(name, tid);
}

inline void begin_tracing_event(const char* name, int tid) {
    trace_collector_ptr->record_event(name, "B", tid);
}

inline void end_tracing_event(const char* name, int tid) {
    trace_collector_ptr->record_event(name, "E", tid);
}

#define TRACE_INITIALIZE(FILE_NAME, PID, CATEGORY) initialize_trace_collector(FILE_NAME, PID, CATEGORY);
#define TRACE_FINISH() finish_tracing();
#define TRACE_ADD_THREADS(START_INDEX, FINISH_INDEX) add_tracing_threads(START_INDEX, FINISH_INDEX);
#define TRACE_INSTANT_EVENT(NAME, TID) record_instant_tracing_event(NAME, TID);
#define TRACE_BEGIN_EVENT(NAME, TID) begin_tracing_event(NAME, TID);
#define TRACE_END_EVENT(NAME, TID) end_tracing_event(NAME, TID);

#else // COLLECT_TRACE

#define TRACE_INITIALIZE(FILE_NAME, PID, CATEGORY)
#define TRACE_FINISH()
#define TRACE_ADD_THREADS(START_INDEX, FINISH_INDEX)
#define TRACE_INSTANT_EVENT(NAME, TID)
#define TRACE_BEGIN_EVENT(NAME, TID)
#define TRACE_END_EVENT(NAME, TID)

#endif // COLLECT_TRACE

#endif // __TBB_trace_H
