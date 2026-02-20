# Python* API for Intel&reg; oneAPI Threading Building Blocks (oneTBB) .

## Overview
It is a preview Python* module which unlocks opportunities for additional performance in
multi-threaded and multiprocess Python programs by enabling threading composability
between two or more thread-enabled libraries like Numpy, Scipy, Sklearn, Dask, Joblib, and etc.

The biggest improvement can be achieved when a task pool like the ThreadPool or Pool from the Python
standard library or libraries like Dask or Joblib (used either in multi-threading or multi-processing mode)
execute tasks calling compute-intensive functions of Numpy/Scipy/Sklearn/PyDAAL which in turn are
parallelized using Intel&reg; oneAPI Math Kernel Library or/and oneTBB.

The module implements Pool class with the standard interface using oneTBB which can be used to replace Python's ThreadPool.
Thanks to the monkey-patching technique implemented in class Monkey, no source code change is needed in order to enable threading composability in Python programs.

For more information and examples, please refer to [forum discussion](https://community.intel.com/t5/Intel-Distribution-for-Python/TBB-module-Unleash-parallel-performance-of-Python-programs/m-p/1074459).

## Directories
 - **rml** - The folder contains sources for building the plugin with cross-process dynamic thread scheduler implementation.
 - **tbb** - The folder contains Python module sources.

## Files
 - **setup.py** - Standard Python setuptools script. Calling it directly is deprecated since setuptools version 58.3.0.
 - **TBB.py** - Alternative entry point for Python module.

## CMake predefined targets
 - `irml` - compilation of plugin with cross-process dynamic thread scheduler implementation.
 - `python_build` - building of oneTBB module for Python.

## Command-line interface

 - `python3 -m tbb -h` - Print documentation on command-line interface.
 - `pydoc tbb` - Read built-in documentation for Python interfaces.
 - `python3 -m tbb your_script.py` - Run your_script.py in context of `with tbb.Monkey():` when oneTBB is enabled. By default only multi-threading will be covered.
 - `python3 -m tbb --ipc your_script.py` - Run your_script.py in context of `with tbb.Monkey():` when oneTBB enabled in both multi-threading and multi-processing modes.
 - `python3 -m tbb --patch-threading your_script.py` - Replace `threading.Thread` with TBB-based implementation for faster thread creation (see below).
 - `TBB_INCLUDEDIRS=<path_to_tbb_includes> TBB_LIBDIRS=<path_to_prebuilt_libraries> python3 -m pip install --no-build-isolation --prefix <output_directory_path>` - Build and install oneTBB module for Python. (Prerequisites: built oneTBB and IRML libraries)
 - `python3 -m TBB test` - run test for oneTBB module for Python.
 - `python3 -m tbb test` - run test for oneTBB module for Python.

## Threading Patch (`--patch-threading`)

The `--patch-threading` flag (or `-T`) replaces Python's `threading.Thread` with a TBB-based implementation that reuses threads from the TBB pool instead of creating new OS threads.

### When to use

This optimization is most effective for workloads that create many short-lived threads, where thread creation overhead dominates:

| Work Size | Threads | System | TBB | Improvement |
|-----------|---------|--------|-----|-------------|
| 1,000 ops | 50 | 9.0ms | 3.9ms | **57% faster** |
| 10,000 ops | 50 | 21.7ms | 15.1ms | **30% faster** |
| 50,000 ops | 50 | 71.0ms | 62.6ms | 12% faster |

*Benchmarks on Intel 4-core CPU*

### Combining with other flags

| Command | Effect |
|---------|--------|
| `python -m tbb script.py` | TBB pools for ThreadPool/Pool |
| `python -m tbb --ipc script.py` | + Inter-process TBB coordination |
| `python -m tbb --patch-threading script.py` | + TBB-based threading.Thread |
| `python -m tbb --ipc --patch-threading script.py` | All optimizations enabled |

### Programmatic usage

```python
import threading
from tbb import patch_threading, tbb_threading

# Option 1: Global patch
patch_threading()
# Now all threading.Thread uses TBB

# Option 2: Context manager (recommended)
with tbb_threading():
    threads = [threading.Thread(target=work) for _ in range(100)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
```

### Limitations

**Thread behavior:**
- `daemon` property has no effect (TBB manages thread lifecycle)
- `native_id` and `ident` may be reused across TBBThread instances (TBB worker reuse)
- Best for CPU-bound work; I/O-bound threads see less benefit

**Worker pool considerations:**
- TBBThread uses a shared worker pool; blocking operations (locks, I/O, `time.sleep()`) can exhaust workers
- Mixing TBBThread with OS-level synchronization primitives may cause unexpected blocking
- For I/O-bound workloads, consider using `concurrent.futures.ThreadPoolExecutor` instead

**Exception handling:**
- Unlike standard `threading.Thread`, exceptions in TBBThread are re-raised in `join()`
- Tracebacks are stripped to prevent memory leaks and potential data exposure

**Security considerations:**
- `patch_threading()` modifies a global module; not recommended for multi-tenant environments
- Use `tbb_threading()` context manager for scoped patching when possible

## Free-threading Python 3.13+ (NOGIL)

When built with Python 3.13t (free-threaded build), the module automatically enables `Py_MOD_GIL_NOT_USED` via SWIG's built-in NOGIL support. All callbacks properly acquire GIL when needed using `SWIG_PYTHON_THREAD_BEGIN_BLOCK/END_BLOCK` macros.

## System Requirements
 - Python 3.9 or higher
 - SWIG 3.0.6 or higher
 - **For free-threading (NOGIL) support:** SWIG 4.4.0 or higher and Python 3.13t
