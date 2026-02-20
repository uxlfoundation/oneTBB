%pythonbegin %{
#
# Copyright (c) 2016-2025 Intel Corporation
# Copyright (c) 2026 UXL Foundation Contributors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


__all__ = ["task_arena",
           "task_group",
           "global_control",
           "default_num_threads",
           "this_task_arena_max_concurrency",
           "this_task_arena_current_thread_index",
           "runtime_version",
           "runtime_interface_version",
           "tbb_run_and_wait",
           "tbb_parallel_for"]
%}
%begin %{
/* Defines Python wrappers for Intel(R) oneAPI Threading Building Blocks (oneTBB)
 *
 * Free-threading (NOGIL) Python 3.13+ Support:
 * This module declares Py_MOD_GIL_NOT_USED to indicate it can run safely
 * without the Global Interpreter Lock. All callbacks to Python code properly
 * acquire the GIL using SWIG_PYTHON_THREAD_BEGIN_BLOCK/END_BLOCK macros.
 */

%}
%module api

#if SWIG_VERSION < 0x030001
#error SWIG version 3.0.6 or newer is required for correct functioning
#endif

%{
#include "tbb/task_arena.h"
#include "tbb/task_group.h"
#include "tbb/global_control.h"
#include "tbb/version.h"

#include <condition_variable>
#include <mutex>
#include <memory>

using namespace tbb;

/*
 * PyCaller - Wrapper for Python callable objects
 * 
 * Thread-safety for free-threading Python:
 * - Uses SWIG_PYTHON_THREAD_BEGIN_BLOCK to acquire GIL before Python API calls
 * - Uses SWIG_PYTHON_THREAD_END_BLOCK to release GIL after Python API calls
 * - Reference counting (Py_INCREF/DECREF) is protected by GIL acquisition
 * 
 * This ensures safe operation when called from TBB worker threads.
 */
class PyCaller : public swig::SwigPtr_PyObject {
public:
    // Copy constructor - must acquire GIL for Py_XINCREF
    PyCaller(const PyCaller& s) : SwigPtr_PyObject() {
        SWIG_PYTHON_THREAD_BEGIN_BLOCK;
        _obj = s._obj;
        Py_XINCREF(_obj);
        SWIG_PYTHON_THREAD_END_BLOCK;
    }
    
    PyCaller(PyObject *p, bool initial = true) : SwigPtr_PyObject() {
        SWIG_PYTHON_THREAD_BEGIN_BLOCK;
        _obj = p;
        if (!initial) {
            Py_XINCREF(_obj);
        }
        SWIG_PYTHON_THREAD_END_BLOCK;
    }
    
    // Move constructor - transfer ownership without refcount change
    PyCaller(PyCaller&& s) noexcept : SwigPtr_PyObject() {
        _obj = s._obj;
        s._obj = nullptr;  // Prevent source destructor from decref
    }
    
    // Move assignment - prevent accidental use
    PyCaller& operator=(PyCaller&&) = delete;
    
    // Copy assignment - prevent accidental use  
    PyCaller& operator=(const PyCaller&) = delete;
    
    // Destructor - must acquire GIL for Py_XDECREF
    ~PyCaller() {
        if (_obj) {  // Only decref if we still own the object
            SWIG_PYTHON_THREAD_BEGIN_BLOCK;
            Py_XDECREF(_obj);
            _obj = nullptr;  // Prevent double-free in base destructor
            SWIG_PYTHON_THREAD_END_BLOCK;
        }
    }

    void operator()() const {
        /* Acquire GIL before calling Python code - required for free-threading */
        SWIG_PYTHON_THREAD_BEGIN_BLOCK;
        PyObject* r = PyObject_CallFunctionObjArgs((PyObject*)*this, nullptr);
        if(r) {
            Py_DECREF(r);
        } else {
            /* Log exception - cannot propagate from TBB worker thread.
             * Note: This logs to stderr. In production, consider capturing
             * exceptions via a thread-safe queue for proper handling.
             */
            PyErr_WriteUnraisable((PyObject*)*this);
        }
        SWIG_PYTHON_THREAD_END_BLOCK;
    }
};

/*
 * ArenaPyCaller - Wrapper for Python callable with task_arena binding
 * 
 * Thread-safety: GIL is acquired for Py_XINCREF in constructor and
 * the actual Python call is delegated to PyCaller which handles GIL.
 */
struct ArenaPyCaller {
    task_arena *my_arena;
    PyObject *my_callable;
    
    ArenaPyCaller(task_arena *a, PyObject *c) : my_arena(a), my_callable(c) {
        SWIG_PYTHON_THREAD_BEGIN_BLOCK;
        Py_XINCREF(c);
        SWIG_PYTHON_THREAD_END_BLOCK;
    }
    
    // Copy constructor - needed because TBB may copy task functors
    ArenaPyCaller(const ArenaPyCaller& other) : my_arena(other.my_arena), my_callable(other.my_callable) {
        SWIG_PYTHON_THREAD_BEGIN_BLOCK;
        Py_XINCREF(my_callable);
        SWIG_PYTHON_THREAD_END_BLOCK;
    }
    
    // Move constructor - transfer ownership without refcount change
    ArenaPyCaller(ArenaPyCaller&& other) noexcept 
        : my_arena(other.my_arena), my_callable(other.my_callable) {
        other.my_callable = nullptr;  // Prevent source destructor from decref
    }
    
    // Destructor - release Python object reference
    ~ArenaPyCaller() {
        if (my_callable) {  // Only decref if we still own the object
            SWIG_PYTHON_THREAD_BEGIN_BLOCK;
            Py_XDECREF(my_callable);
            SWIG_PYTHON_THREAD_END_BLOCK;
        }
    }
    
    // Assignment operators - prevent double-free issues
    ArenaPyCaller& operator=(const ArenaPyCaller&) = delete;
    ArenaPyCaller& operator=(ArenaPyCaller&&) = delete;
    
    void operator()() const {
        my_arena->execute(PyCaller(my_callable, false));
    }
};

struct barrier_data {
    std::condition_variable event;
    std::mutex m;
    int worker_threads, full_threads;
};

/*
 * _concurrency_barrier - Wait for all TBB worker threads to be ready
 * 
 * This function is thread-safe and does not require GIL as it only
 * uses C++ synchronization primitives (mutex, condition_variable).
 */
void _concurrency_barrier(int threads = tbb::task_arena::automatic) {
    if(threads == tbb::task_arena::automatic)
        threads = tbb::this_task_arena::max_concurrency();
    if(threads < 2)
        return;
    std::unique_ptr<global_control> g(
        (global_control::active_value(global_control::max_allowed_parallelism) < unsigned(threads))?
            new global_control(global_control::max_allowed_parallelism, threads) : nullptr);

    tbb::task_group tg;
    barrier_data b;
    b.worker_threads = 0;
    b.full_threads = threads-1;
    for(int i = 0; i < b.full_threads; i++)
        tg.run([&b]{
            std::unique_lock<std::mutex> lock(b.m);
            if(++b.worker_threads >= b.full_threads)
                b.event.notify_all();
            else while(b.worker_threads < b.full_threads)
                b.event.wait(lock);
        });
    std::unique_lock<std::mutex> lock(b.m);
    b.event.wait(lock, [&b]{ return b.worker_threads >= b.full_threads; });
    tg.wait();
};

%}

void _concurrency_barrier(int threads = tbb::task_arena::automatic);

namespace tbb {

    class task_arena {
    public:
        static const int automatic = -1;
        task_arena(int max_concurrency = automatic, unsigned reserved_for_masters = 1);
        task_arena(const task_arena &s);
        ~task_arena();
        void initialize();
        void initialize(int max_concurrency, unsigned reserved_for_masters = 1);
        void terminate();
        bool is_active();
        %extend {
        void enqueue( PyObject *c ) { $self->enqueue(PyCaller(c, false)); }
        void execute( PyObject *c ) { $self->execute(PyCaller(c, false)); }
        };
    };

    class task_group {
    public:
        task_group();
        ~task_group();
        void wait();
        void cancel();
        %extend {
        void run( PyObject *c ) { $self->run(PyCaller(c, false)); }
        void run( PyObject *c, task_arena *a ) { $self->run(ArenaPyCaller(a, c)); }
        };
    };

    class global_control {
    public:
        enum parameter {
            max_allowed_parallelism,
            thread_stack_size,
            parameter_max // insert new parameters above this point
        };
        global_control(parameter param, size_t value);
        ~global_control();
        static size_t active_value(parameter param);
    };

} // tbb

%inline {
    inline const char* runtime_version() { return TBB_runtime_version();}
    inline int runtime_interface_version() { return TBB_runtime_interface_version();}
    inline int this_task_arena_max_concurrency() { return this_task_arena::max_concurrency();}
    inline int this_task_arena_current_thread_index() { return this_task_arena::current_thread_index();}
};

/*
 * Direct TBB thread dispatch - minimal Python overhead
 * 
 * Provides tbb_run_and_wait() for batch execution of callables.
 */

// Python wrapper using existing task_group
%pythoncode %{
def tbb_run_and_wait(callables):
    """
    Run multiple callables on TBB threads and wait for completion.
    
    Run callables in parallel using TBB task_group.
    
    Note: Exceptions raised in callables are not propagated. They are logged
    to stderr via PyErr_WriteUnraisable at the C++ level. If you need exception
    propagation, use TBBThread (via patch_threading) which re-raises in join().
    
    Args:
        callables: iterable of callable objects
    
    Example:
        def work(x):
            return x * 2
        
        funcs = [lambda i=i: work(i) for i in range(10)]
        tbb_run_and_wait(funcs)
    """
    tg = task_group()
    for c in callables:
        tg.run(c)
    tg.wait()

def tbb_parallel_for(n, func):
    """
    Run func(i) for i in range(n) on TBB threads.
    
    Note: Exceptions raised in func are not propagated. They are logged
    to stderr via PyErr_WriteUnraisable at the C++ level.
    
    Args:
        n: number of iterations
        func: callable that takes one int argument
    
    Example:
        results = []
        def work(i):
            results.append(i * 2)
        
        tbb_parallel_for(10, work)
    """
    tg = task_group()
    for i in range(n):
        # Capture i by creating closure
        def task(idx=i):
            func(idx)
        tg.run(task)
    tg.wait()
%}

// Additional definitions for Python part of the module
%pythoncode %{
default_num_threads = this_task_arena_max_concurrency
%}
