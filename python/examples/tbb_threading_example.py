#!/usr/bin/env python3
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
Example: Using TBB-based threading.Thread replacement.

Shows three ways to use TBB threading:
1. patch_threading() — global replacement
2. tbb_threading() — scoped context manager
3. python -m tbb -t — command-line flag
"""

import threading
import time


def compute(task_id):
    """Simulate a short compute task."""
    total = 0
    for i in range(100_000):
        total += i * task_id
    return total


# ── Method 1: patch_threading() ──────────────────────────────────

def example_patch():
    """Global patch — all threading.Thread uses TBB pool."""
    from tbb import patch_threading, unpatch_threading

    patch_threading()

    results = {}
    lock = threading.Lock()

    def worker(task_id):
        result = compute(task_id)
        with lock:
            results[task_id] = result

    threads = []
    for i in range(8):
        t = threading.Thread(target=worker, args=(i,))
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    unpatch_threading()
    print(f"patch_threading: {len(results)} tasks completed")


# ── Method 2: tbb_threading() context manager ────────────────────

def example_context_manager():
    """Scoped patch — TBB threading only inside the block."""
    from tbb import tbb_threading

    results = {}
    lock = threading.Lock()

    def worker(task_id):
        result = compute(task_id)
        with lock:
            results[task_id] = result

    with tbb_threading():
        threads = []
        for i in range(8):
            t = threading.Thread(target=worker, args=(i,))
            threads.append(t)
            t.start()

        for t in threads:
            t.join()

    print(f"tbb_threading: {len(results)} tasks completed")


# ── Method 3: tbb_parallel_for ───────────────────────────────────

def example_parallel_for():
    """Direct TBB dispatch for simple parallel loops."""
    from tbb.api import tbb_parallel_for, tbb_run_and_wait

    results = []
    lock = threading.Lock()

    def work(i):
        result = compute(i)
        with lock:
            results.append(result)

    tbb_parallel_for(8, work)
    print(f"tbb_parallel_for: {len(results)} tasks completed")


if __name__ == "__main__":
    print("=== TBB Threading Examples ===\n")

    example_patch()
    example_context_manager()
    example_parallel_for()

    print("\nAll examples completed.")
    print("\nTip: You can also run any script with TBB threading via CLI:")
    print("  python -m tbb -t your_script.py")
