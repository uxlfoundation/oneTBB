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

"""
Tests for TBB-based threading.Thread replacement.

Tests cover:
- patch_threading / unpatch_threading lifecycle
- TBBThread as drop-in for threading.Thread
- tbb_threading context manager
- tbb_run_and_wait / tbb_parallel_for helpers
- Edge cases and error handling
"""

import threading
import time
import unittest

from . import (
    TBBThread,
    patch_threading,
    unpatch_threading,
    is_threading_patched,
    tbb_threading,
    _OriginalThread,
)
from .api import tbb_run_and_wait, tbb_parallel_for


class TestPatchLifecycle(unittest.TestCase):
    """Tests for patch_threading / unpatch_threading."""

    def setUp(self):
        # Ensure clean state before each test
        if is_threading_patched():
            unpatch_threading()

    def tearDown(self):
        if is_threading_patched():
            unpatch_threading()

    def test_patch_replaces_thread(self):
        assert threading.Thread is _OriginalThread
        patch_threading()
        assert threading.Thread is TBBThread
        assert is_threading_patched()

    def test_unpatch_restores_thread(self):
        patch_threading()
        unpatch_threading()
        assert threading.Thread is _OriginalThread
        assert not is_threading_patched()

    def test_double_patch_is_safe(self):
        patch_threading()
        patch_threading()  # should not raise
        assert threading.Thread is TBBThread
        unpatch_threading()
        assert threading.Thread is _OriginalThread

    def test_double_unpatch_is_safe(self):
        patch_threading()
        unpatch_threading()
        unpatch_threading()  # should not raise
        assert threading.Thread is _OriginalThread


class TestTBBThread(unittest.TestCase):
    """Tests for TBBThread as a drop-in replacement."""

    def test_basic_start_join(self):
        results = []

        def worker():
            results.append(42)

        t = TBBThread(target=worker)
        t.start()
        t.join()
        assert results == [42]

    def test_target_with_args(self):
        results = []

        def worker(a, b):
            results.append(a + b)

        t = TBBThread(target=worker, args=(3, 7))
        t.start()
        t.join()
        assert results == [10]

    def test_target_with_kwargs(self):
        results = []

        def worker(x, y=0):
            results.append(x * y)

        t = TBBThread(target=worker, args=(5,), kwargs={"y": 6})
        t.start()
        t.join()
        assert results == [30]

    def test_is_alive(self):
        event = threading.Event()

        def worker():
            event.wait(timeout=5)

        t = TBBThread(target=worker)
        assert not t.is_alive()
        t.start()
        # Give it a moment to start
        time.sleep(0.05)
        assert t.is_alive()
        event.set()
        t.join()
        assert not t.is_alive()

    def test_name_property(self):
        t = TBBThread(target=lambda: None, name="my-thread")
        assert t.name == "my-thread"
        t.name = "renamed"
        assert t.name == "renamed"

    def test_daemon_property(self):
        t = TBBThread(target=lambda: None, daemon=True)
        assert t.daemon is True
        t.daemon = False
        assert t.daemon is False

    def test_exception_propagation(self):
        def failing():
            raise ValueError("test error")

        t = TBBThread(target=failing)
        t.start()
        with self.assertRaises(ValueError) as ctx:
            t.join()
        assert "test error" in str(ctx.exception)

    def test_cannot_start_twice(self):
        t = TBBThread(target=lambda: None)
        t.start()
        t.join()
        with self.assertRaises(RuntimeError):
            t.start()

    def test_cannot_join_before_start(self):
        t = TBBThread(target=lambda: None)
        with self.assertRaises(RuntimeError):
            t.join()

    def test_multiple_threads(self):
        results = []
        lock = threading.Lock()

        def worker(i):
            with lock:
                results.append(i)

        threads = [TBBThread(target=worker, args=(i,)) for i in range(10)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()

        assert sorted(results) == list(range(10))

    def test_repr(self):
        t = TBBThread(target=lambda: None, name="test")
        assert "initial" in repr(t)
        t.start()
        t.join()
        assert "stopped" in repr(t)

    def test_join_timeout(self):
        event = threading.Event()

        def worker():
            event.wait(timeout=5)

        t = TBBThread(target=worker)
        t.start()
        t.join(timeout=0.05)  # Should return without blocking
        assert t.is_alive()
        event.set()
        t.join()
        assert not t.is_alive()


class TestTBBThreadingContextManager(unittest.TestCase):
    """Tests for tbb_threading context manager."""

    def test_patches_inside_restores_outside(self):
        assert threading.Thread is _OriginalThread
        with tbb_threading():
            assert threading.Thread is TBBThread
        assert threading.Thread is _OriginalThread

    def test_threads_work_inside_context(self):
        results = []

        with tbb_threading():
            t = threading.Thread(target=lambda: results.append(1))
            t.start()
            t.join()

        assert results == [1]

    def test_restores_on_exception(self):
        try:
            with tbb_threading():
                assert threading.Thread is TBBThread
                raise RuntimeError("boom")
        except RuntimeError:
            pass
        assert threading.Thread is _OriginalThread


class TestPatchedThreadingThread(unittest.TestCase):
    """Tests that patched threading.Thread works identically."""

    def setUp(self):
        patch_threading()

    def tearDown(self):
        unpatch_threading()

    def test_thread_via_standard_api(self):
        results = []
        t = threading.Thread(target=lambda: results.append("done"))
        t.start()
        t.join()
        assert results == ["done"]

    def test_concurrent_threads(self):
        counter = {"value": 0}
        lock = threading.Lock()

        def increment():
            for _ in range(100):
                with lock:
                    counter["value"] += 1

        threads = [threading.Thread(target=increment) for _ in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        assert counter["value"] == 400


class TestTBBRunAndWait(unittest.TestCase):
    """Tests for tbb_run_and_wait helper."""

    def test_basic(self):
        results = []
        lock = threading.Lock()

        def work(i):
            with lock:
                results.append(i)

        funcs = [lambda i=i: work(i) for i in range(5)]
        tbb_run_and_wait(funcs)
        assert sorted(results) == [0, 1, 2, 3, 4]

    def test_empty(self):
        tbb_run_and_wait([])  # should not raise


class TestTBBParallelFor(unittest.TestCase):
    """Tests for tbb_parallel_for helper."""

    def test_basic(self):
        results = []
        lock = threading.Lock()

        def work(i):
            with lock:
                results.append(i * 2)

        tbb_parallel_for(5, work)
        assert sorted(results) == [0, 2, 4, 6, 8]

    def test_zero_iterations(self):
        tbb_parallel_for(0, lambda i: None)  # should not raise


def test(verbose=False):
    """Run all threading tests."""
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromModule(__import__(__name__))
    verbosity = 2 if verbose else 1
    runner = unittest.TextTestRunner(verbosity=verbosity)
    result = runner.run(suite)
    return result.wasSuccessful()


if __name__ == "__main__":
    unittest.main()
