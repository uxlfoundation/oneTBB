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

import multiprocessing.pool
import ctypes
import atexit
import sys
import os
import threading
import _thread
import warnings
from typing import Optional, Callable, Tuple
from contextlib import contextmanager

import platform

if "Windows" in platform.system():
    import site
    path_to_env = site.getsitepackages()[0]
    path_to_libs = os.path.join(path_to_env, "Library", "bin")
    if sys.version_info.minor >= 8:
        os.add_dll_directory(path_to_libs)
    os.environ['PATH'] += os.pathsep + path_to_libs


from .api import  *
from .api import __all__ as api__all
from .pool import *
from .pool import __all__ as pool__all

__all__ = ["Monkey", "is_active", "TBBThread", "patch_threading", "unpatch_threading", 
           "is_threading_patched", "tbb_threading"] + api__all + pool__all

__doc__ = """
Python API for Intel(R) oneAPI Threading Building Blocks (oneTBB)
extended with standard Python's pools implementation and monkey-patching.

Command-line interface example:
$  python3 -m tbb $your_script.py
Runs your_script.py in context of tbb.Monkey
"""

is_active = False
""" Indicates whether oneTBB context is activated """

# Threading patch state
_OriginalThread = threading.Thread
_is_threading_patched = False
_patch_lock = threading.Lock()

ipc_enabled = False
""" Indicates whether IPC mode is enabled """

libirml = "libirml.so.1"


def _test(arg=None):
    """Some tests"""
    import platform
    if platform.system() == "Linux":
        ctypes.CDLL(libirml)
        assert 256 == os.system("ldd "+_api.__file__+"| grep -E 'libimf|libsvml|libintlc'") # nosec
    from .test import test
    test(arg)
    print("done")


def tbb_process_pool_worker27(inqueue, outqueue, initializer=None, initargs=(),
                            maxtasks=None):
    from multiprocessing.pool import worker
    worker(inqueue, outqueue, initializer, initargs, maxtasks)
    if ipc_enabled:
        try:
            librml = ctypes.CDLL(libirml)
            librml.release_resources()
        except:
            print("Warning: Can not load ", libirml, file=sys.stderr)


class TBBProcessPool27(multiprocessing.pool.Pool):
    def _repopulate_pool(self):
        """Bring the number of pool processes up to the specified number,
        for use after reaping workers which have exited.
        """
        from multiprocessing.util import debug

        for i in range(self._processes - len(self._pool)):
            w = self.Process(target=tbb_process_pool_worker27,
                             args=(self._inqueue, self._outqueue,
                                   self._initializer,
                                   self._initargs, self._maxtasksperchild)
                            )
            self._pool.append(w)
            w.name = w.name.replace('Process', 'PoolWorker')
            w.daemon = True
            w.start()
            debug('added worker')

    def __del__(self):
        self.close()
        for p in self._pool:
            p.join()

    def __exit__(self, *args):
        self.close()
        for p in self._pool:
            p.join()


def tbb_process_pool_worker3(inqueue, outqueue, initializer=None, initargs=(),
                            maxtasks=None, wrap_exception=False):
    from multiprocessing.pool import worker
    worker(inqueue, outqueue, initializer, initargs, maxtasks, wrap_exception)
    if ipc_enabled:
        try:
            librml = ctypes.CDLL(libirml)
            librml.release_resources()
        except:
            print("Warning: Can not load ", libirml, file=sys.stderr)


class TBBProcessPool3(multiprocessing.pool.Pool):
    def _repopulate_pool(self):
        """Bring the number of pool processes up to the specified number,
        for use after reaping workers which have exited.
        """
        from multiprocessing.util import debug

        for i in range(self._processes - len(self._pool)):
            w = self.Process(target=tbb_process_pool_worker3,
                             args=(self._inqueue, self._outqueue,
                                   self._initializer,
                                   self._initargs, self._maxtasksperchild,
                                   self._wrap_exception)
                            )
            self._pool.append(w)
            w.name = w.name.replace('Process', 'PoolWorker')
            w.daemon = True
            w.start()
            debug('added worker')

    def __del__(self):
        self.close()
        for p in self._pool:
            p.join()

    def __exit__(self, *args):
        self.close()
        for p in self._pool:
            p.join()


class TBBThread:
    """
    A threading.Thread-compatible class that executes work on TBB thread pool.
    
    Key differences from threading.Thread:
    - Does not create a new OS thread for each instance
    - Uses TBB's work-stealing thread pool
    - More efficient for many short-lived threads
    - Exceptions are re-raised in join() (unlike stdlib)
    
    Limitations:
    - native_id returns TBB worker thread ID, may be reused
    - daemon property has no effect (TBB manages thread lifecycle)
    - Blocking operations can exhaust TBB worker pool
    - threading.local() data is per-worker, not per-TBBThread (workers are reused)
    """
    
    # Shared TBB pool for all TBBThread instances
    _pool: Optional[Pool] = None
    _pool_lock = threading.Lock()
    _active_threads: set = set()
    _active_lock = threading.Lock()
    
    def __init__(
        self,
        group=None,
        target: Optional[Callable] = None,
        name: Optional[str] = None,
        args: Tuple = (),
        kwargs: Optional[dict] = None,
        *,
        daemon: Optional[bool] = None
    ):
        if group is not None:
            raise ValueError("group argument must be None for TBBThread")
        
        self._target = target
        self._name = name or f"TBBThread-{id(self)}"
        self._args = args
        self._kwargs = kwargs or {}
        self._daemon = daemon
        
        self._started = threading.Event()
        self._stopped = threading.Event()
        self._start_lock = threading.Lock()
        self._start_called = False
        self._exception: Optional[Exception] = None
        self._join_lock = threading.Lock()
        self._native_id: Optional[int] = None
        self._ident: Optional[int] = None
        
    @classmethod
    def _get_pool(cls) -> Pool:
        """Get or create shared TBB pool."""
        if cls._pool is None:
            with cls._pool_lock:
                if cls._pool is None:
                    cls._pool = Pool()
        return cls._pool
    
    @classmethod
    def _shutdown_pool(cls):
        """Shutdown shared pool (called on unpatch)."""
        with cls._pool_lock:
            if cls._pool is not None:
                cls._pool.close()
                cls._pool.join()
                cls._pool = None
    
    def _run_wrapper(self) -> None:
        """Wrapper that runs target and captures result/exception."""
        self._native_id = _thread.get_native_id()
        self._ident = _thread.get_ident()
        self._started.set()
        
        with TBBThread._active_lock:
            TBBThread._active_threads.add(self)
        
        try:
            if self._target:
                self._target(*self._args, **self._kwargs)
        except Exception as e:
            # Store exception without traceback to prevent memory leaks
            self._exception = e.with_traceback(None)
        finally:
            with TBBThread._active_lock:
                TBBThread._active_threads.discard(self)
            self._stopped.set()
    
    def start(self) -> None:
        """Start the thread (submit to TBB pool)."""
        with self._start_lock:
            if self._start_called:
                raise RuntimeError("threads can only be started once")
            self._start_called = True
            # Submit inside lock to prevent TOCTOU race
            pool = self._get_pool()
            pool.apply_async(self._run_wrapper)
    
    def join(self, timeout: Optional[float] = None) -> None:
        """Wait for thread to complete.
        
        Note: Unlike threading.Thread, exceptions from the target function
        are re-raised in join().
        """
        if not self._start_called:
            raise RuntimeError("cannot join thread before it is started")
        
        if not self._stopped.wait(timeout):
            return  # Timeout expired
        
        with self._join_lock:
            if self._exception is not None:
                exc = self._exception
                self._exception = None
                raise exc
    
    def is_alive(self) -> bool:
        """Return True if thread is running."""
        return self._started.is_set() and not self._stopped.is_set()
    
    @property
    def name(self) -> str:
        return self._name
    
    @name.setter
    def name(self, value: str) -> None:
        self._name = value
    
    @property
    def ident(self) -> Optional[int]:
        return self._ident
    
    @property
    def native_id(self) -> Optional[int]:
        return self._native_id
    
    @property
    def daemon(self) -> bool:
        return self._daemon or False
    
    @daemon.setter
    def daemon(self, value: bool) -> None:
        self._daemon = value
    
    def __repr__(self) -> str:
        status = "initial"
        if self._started.is_set():
            status = "stopped" if self._stopped.is_set() else "started"
        return f"<TBBThread({self._name}, {status})>"


# Register atexit handler to ensure TBB pool cleanup on shutdown
def _tbb_thread_atexit():
    """Join non-daemon TBBThreads and cleanup pool on interpreter shutdown."""
    # Join non-daemon threads (mirrors threading._shutdown behavior)
    with TBBThread._active_lock:
        threads_to_join = [t for t in TBBThread._active_threads if not t.daemon]
    for t in threads_to_join:
        try:
            t.join(timeout=5.0)
        except Exception:
            pass
    TBBThread._shutdown_pool()

atexit.register(_tbb_thread_atexit)


class Monkey:
    """
    Context manager which replaces standard multiprocessing.pool
    implementations with tbb.pool using monkey-patching. It also enables oneTBB
    threading for Intel(R) oneAPI Math Kernel Library (oneMKL). For example:

        with tbb.Monkey():
            run_my_numpy_code()

    It allows multiple parallel tasks to be executed on the same thread pool
    and coordinate number of threads across multiple processes thus avoiding
    overheads from oversubscription.
    """
    _items   = {}
    _modules = {}

    def __init__(self, max_num_threads=None, benchmark=False, threads=False):
        """
        Create context manager for running under TBB scheduler.
        
        :param max_num_threads: if specified, limits maximal number of threads
        :param benchmark: if specified, blocks in initialization until requested number of threads are ready
        :param threads: if True, replace threading.Thread with TBB-based version
        """
        self._patch_threads = threads
        if max_num_threads:
            self.ctl = global_control(global_control.max_allowed_parallelism, int(max_num_threads))
        if benchmark:
            if not max_num_threads:
               max_num_threads = default_num_threads()
            from .api import _concurrency_barrier
            _concurrency_barrier(int(max_num_threads))

    def _patch(self, class_name, module_name, obj):
        m = self._modules[class_name] = __import__(module_name, globals(),
                                                   locals(), [class_name])
        if m == None:
            return
        oldattr = getattr(m, class_name, None)
        if oldattr == None:
            self._modules[class_name] = None
            return
        self._items[class_name] = oldattr
        setattr(m, class_name, obj)

    def __enter__(self):
        global is_active
        assert is_active == False, "tbb.Monkey does not support nesting yet"
        is_active = True
        self.env_mkl = os.getenv('MKL_THREADING_LAYER')
        os.environ['MKL_THREADING_LAYER'] = 'TBB'
        self.env_numba = os.getenv('NUMBA_THREADING_LAYER')
        os.environ['NUMBA_THREADING_LAYER'] = 'TBB'

        if ipc_enabled:
            if sys.version_info.major == 2 and sys.version_info.minor >= 7:
                self._patch("Pool", "multiprocessing.pool", TBBProcessPool27)
            elif sys.version_info.major == 3 and sys.version_info.minor >= 5:
                self._patch("Pool", "multiprocessing.pool", TBBProcessPool3)
        self._patch("ThreadPool", "multiprocessing.pool", Pool)
        
        # Patch threading.Thread with TBB-based implementation
        if self._patch_threads:
            self._patch("Thread", "threading", TBBThread)
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        global is_active
        assert is_active == True, "modified?"
        is_active = False
        if self.env_mkl is None:
            del os.environ['MKL_THREADING_LAYER']
        else:
            os.environ['MKL_THREADING_LAYER'] = self.env_mkl
        if self.env_numba is None:
            del os.environ['NUMBA_THREADING_LAYER']
        else:
            os.environ['NUMBA_THREADING_LAYER'] = self.env_numba
        for name in self._items.keys():
            setattr(self._modules[name], name, self._items[name])


def patch_threading(*, _warn: bool = True) -> None:
    """
    Replace threading.Thread with TBB-based implementation.
    
    After calling this, all new threading.Thread instances will use
    TBB's thread pool instead of creating OS threads.
    
    WARNING: This is a global modification with important limitations:
    - daemon property has no effect (TBB manages thread lifecycle)
    - native_id/ident may be reused across TBBThread instances  
    - Blocking operations (locks, I/O) can exhaust the TBB worker pool
    
    Args:
        _warn: If True (default), emit a warning about limitations
    """
    global _is_threading_patched
    with _patch_lock:
        if _is_threading_patched:
            return
        
        if _warn:
            warnings.warn(
                "patch_threading() replaces threading.Thread globally. "
                "TBBThread has limitations: daemon ignored, thread IDs may be reused, "
                "blocking operations can exhaust worker pool. "
                "Use _warn=False to suppress this warning.",
                UserWarning,
                stacklevel=2
            )
        
        threading.Thread = TBBThread
        _is_threading_patched = True


def unpatch_threading() -> None:
    """Restore original threading.Thread implementation."""
    global _is_threading_patched
    with _patch_lock:
        if not _is_threading_patched:
            return
        
        TBBThread._shutdown_pool()
        threading.Thread = _OriginalThread
        _is_threading_patched = False


def is_threading_patched() -> bool:
    """Return True if threading is currently patched."""
    return _is_threading_patched


@contextmanager
def tbb_threading(*, warn: bool = False):
    """
    Context manager for temporary TBB threading patch.
    
    This is the recommended way to use TBB threading as it ensures
    proper cleanup and limits the scope of the patch.
    
    Args:
        warn: If True, emit warning about limitations (default False)
    
    Example:
        with tbb_threading():
            threads = [threading.Thread(target=work) for _ in range(100)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()
    """
    patch_threading(_warn=warn)
    try:
        yield
    finally:
        unpatch_threading()


def init_sem_name():
    try:
        librml = ctypes.CDLL(libirml)
        librml.set_active_sem_name()
        librml.set_stop_sem_name()
    except Exception as e:
        print("Warning: Can not initialize name of shared semaphores:", e,
              file=sys.stderr)


def tbb_atexit():
    if ipc_enabled:
        try:
            librml = ctypes.CDLL(libirml)
            librml.release_semaphores()
        except:
            print("Warning: Can not release shared semaphores",
                  file=sys.stderr)


def _main():
    # Run the module specified as the next command line argument
    # python3 -m TBB user_app.py
    global ipc_enabled

    import platform
    import argparse
    parser = argparse.ArgumentParser(prog="python3 -m tbb", description="""
                Run your Python script in context of tbb.Monkey, which
                replaces standard Python pools and threading layer of
                Intel(R) oneAPI Math Kernel Library (oneMKL) by implementation based on
                Intel(R) oneAPI Threading Building Blocks (oneTBB). It enables multiple parallel
                tasks to be executed on the same thread pool and coordinate
                number of threads across multiple processes thus avoiding
                overheads from oversubscription.
             """, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    if platform.system() == "Linux":
        parser.add_argument('--ipc', action='store_true',
                        help="Enable inter-process (IPC) coordination between oneTBB schedulers")
        parser.add_argument('-a', '--allocator', action='store_true',
                        help="Enable oneTBB scalable allocator as a replacement for standard memory allocator")
        parser.add_argument('--allocator-huge-pages', action='store_true',
                        help="Enable huge pages for oneTBB allocator (implies: -a)")
    parser.add_argument('-p', '--max-num-threads', default=default_num_threads(), type=int,
                        help="Initialize oneTBB with P max number of threads per process", metavar='P')
    parser.add_argument('-b', '--benchmark', action='store_true',
                        help="Block oneTBB initialization until all the threads are created before continue the script. "
                        "This is necessary for performance benchmarks that want to exclude lazy scheduler initialization effects from the measurements")
    parser.add_argument('-v', '--verbose', action='store_true',
                        help="Request verbose and version information")
    parser.add_argument('-m', action='store_true', dest='module',
                        help="Executes following as a module")
    parser.add_argument('-T', '--patch-threading', action='store_true', dest='threads',
                        help="Replace threading.Thread with TBB-based implementation (uses TBB task_group, 20-57%% faster for short-lived threads)")
    parser.add_argument('name', help="Script or module name")
    parser.add_argument('args', nargs=argparse.REMAINDER,
                        help="Command line arguments")
    args = parser.parse_args()

    if args.verbose:
        os.environ["TBB_VERSION"] = "1"
    if platform.system() == "Linux":
        if args.allocator_huge_pages:
            args.allocator = True
        if args.allocator and not os.environ.get("_TBB_MALLOC_PRELOAD"):
            libtbbmalloc_lib = 'libtbbmalloc_proxy.so.2'
            ld_preload = 'LD_PRELOAD'
            os.environ["_TBB_MALLOC_PRELOAD"] = "1"
            preload_list = filter(None, os.environ.get(ld_preload, "").split(':'))
            if libtbbmalloc_lib in preload_list:
                print('Info:', ld_preload, "contains", libtbbmalloc_lib, "already\n")
            else:
                os.environ[ld_preload] = ':'.join([libtbbmalloc_lib] + list(preload_list))

            if args.allocator_huge_pages:
                assert platform.system() == "Linux"
                try:
                    with open('/proc/sys/vm/nr_hugepages', 'r') as f:
                        pages = int(f.read())
                    if pages == 0:
                        print("oneTBB: Pre-allocated huge pages are not currently reserved in the system. To reserve, run e.g.:\n"
                              "\tsudo sh -c 'echo 2000 > /proc/sys/vm/nr_hugepages'")
                    os.environ["TBB_MALLOC_USE_HUGE_PAGES"] = "1"
                except:
                    print("oneTBB: Failed to read number of pages from /proc/sys/vm/nr_hugepages\n"
                          "\tIs the Linux kernel configured with the huge pages feature?")
                    sys.exit(1)

            os.execl(sys.executable, sys.executable, '-m', 'tbb', *sys.argv[1:])
            assert False, "Re-execution failed"

    sys.argv = [args.name] + args.args
    ipc_enabled = platform.system() == "Linux" and args.ipc
    os.environ["IPC_ENABLE"] = "1" if ipc_enabled else "0"
    if ipc_enabled:
        atexit.register(tbb_atexit)
        init_sem_name()
    if not os.environ.get("KMP_BLOCKTIME"): # TODO move
        os.environ["KMP_BLOCKTIME"] = "0"
    if '_' + args.name in globals():
        return globals()['_' + args.name](*args.args)
    else:
        import runpy
        runf = runpy.run_module if args.module else runpy.run_path
        with Monkey(max_num_threads=args.max_num_threads, benchmark=args.benchmark, threads=args.threads):
            runf(args.name, run_name='__main__')
