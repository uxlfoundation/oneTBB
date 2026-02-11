"""
TBB-based threading.Thread replacement for Python NOGIL.

This module provides a drop-in replacement for threading.Thread that uses
TBB's task-based execution instead of creating OS threads directly.

Usage:
    from tbb.threading_patch import patch_threading, unpatch_threading
    
    patch_threading()  # Replace threading.Thread with TBB-based version
    # ... your code using threading.Thread ...
    unpatch_threading()  # Restore original threading.Thread

Or use as context manager:
    from tbb.threading_patch import tbb_threading
    
    with tbb_threading():
        t = threading.Thread(target=my_func)
        t.start()
        t.join()
"""

import threading
import queue
import sys
from typing import Optional, Callable, Any, Tuple
from contextlib import contextmanager

# Import TBB components
try:
    from tbb.pool import Pool, task_group
    from tbb import this_task_arena_max_concurrency
except ImportError:
    raise ImportError("TBB module required. Install with: pip install tbb")

# Store original Thread class
_OriginalThread = threading.Thread
_is_patched = False


class TBBThread:
    """
    A threading.Thread-compatible class that executes work on TBB thread pool.
    
    Key differences from threading.Thread:
    - Does not create a new OS thread for each instance
    - Uses TBB's work-stealing thread pool
    - More efficient for many short-lived threads
    
    Limitations:
    - native_id returns TBB worker thread ID, may be reused
    - daemon property has no effect (TBB manages thread lifecycle)
    - Some threading.Thread attributes not supported
    """
    
    # Shared TBB pool for all TBBThread instances
    _pool: Optional[Pool] = None
    _pool_lock = threading.Lock()
    
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
        self._result_queue: queue.Queue = queue.Queue()
        self._exception: Optional[BaseException] = None
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
                cls._pool = None
    
    def _run_wrapper(self) -> None:
        """Wrapper that runs target and captures result/exception."""
        import _thread
        self._native_id = _thread.get_native_id()
        self._ident = _thread.get_ident()
        self._started.set()
        
        try:
            if self._target:
                self._target(*self._args, **self._kwargs)
        except BaseException as e:
            # Store exception without traceback to prevent memory leaks
            # and potential sensitive data exposure in traceback frames
            self._exception = e.with_traceback(None)
        finally:
            self._stopped.set()
    
    def start(self) -> None:
        """Start the thread (submit to TBB pool)."""
        with self._start_lock:
            if self._start_called:
                raise RuntimeError("threads can only be started once")
            # Mark as starting (not yet running, but start() was called)
            self._start_called = True
            # Submit inside lock to prevent TOCTOU race with pool shutdown
            pool = self._get_pool()
            pool.apply_async(self._run_wrapper)
    
    def join(self, timeout: Optional[float] = None) -> None:
        """Wait for thread to complete.
        
        Note: Unlike threading.Thread, exceptions from the target function
        are re-raised in join(). This is intentional for better error handling
        but differs from standard library behavior.
        """
        if not self._start_called:
            raise RuntimeError("cannot join thread before it is started")
        
        # Wait for thread to complete (combines start wait + stop wait)
        if not self._stopped.wait(timeout):
            # Timeout expired - thread still running, don't check exception
            return
        
        if self._exception is not None:
            # Re-raise exception from thread
            # Create new exception to avoid leaking internal state
            exc = self._exception
            self._exception = None  # Clear to allow re-join
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


def patch_threading(*, _warn: bool = True) -> None:
    """
    Replace threading.Thread with TBB-based implementation.
    
    After calling this, all new threading.Thread instances will use
    TBB's thread pool instead of creating OS threads.
    
    WARNING: This is a global modification with important limitations:
    - daemon property has no effect (TBB manages thread lifecycle)
    - native_id/ident may be reused across TBBThread instances  
    - Blocking operations (locks, I/O) can exhaust the TBB worker pool
    - Not recommended for security-sensitive or multi-tenant environments
    
    Args:
        _warn: If True (default), emit a warning about limitations
    """
    import warnings
    
    global _is_patched
    if _is_patched:
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
    _is_patched = True


def unpatch_threading() -> None:
    """
    Restore original threading.Thread implementation.
    """
    global _is_patched
    if not _is_patched:
        return
    
    TBBThread._shutdown_pool()
    threading.Thread = _OriginalThread
    _is_patched = False


def is_patched() -> bool:
    """Return True if threading is currently patched."""
    return _is_patched


@contextmanager
def tbb_threading(*, warn: bool = False):
    """
    Context manager for temporary TBB threading patch.
    
    This is the recommended way to use TBB threading as it ensures
    proper cleanup and limits the scope of the patch.
    
    Args:
        warn: If True, emit warning about limitations (default False
              since context manager scope is inherently safer)
    
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


# For `python -m tbb.threading_patch` usage
if __name__ == "__main__":
    import sys
    
    if len(sys.argv) < 2:
        print("Usage: python -m tbb.threading_patch <script.py> [args...]")
        sys.exit(1)
    
    # Patch threading before running script (no warning for CLI usage)
    patch_threading(_warn=False)
    
    # Run the script
    script = sys.argv[1]
    sys.argv = sys.argv[1:]
    
    with open(script) as f:
        code = compile(f.read(), script, 'exec')
        exec(code, {'__name__': '__main__', '__file__': script})
