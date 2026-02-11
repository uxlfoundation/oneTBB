
## Threading Benchmark Results (Intel-Bench, 4 Intel CPUs)

`python -m tbb -t script.py` replaces `threading.Thread` with TBB-based implementation.

| Work Size | Threads | System | TBB (-t) | Improvement |
|-----------|---------|--------|----------|-------------|
| 1,000 | 10 | 2.0ms | 1.6ms | **20% faster** |
| 1,000 | 20 | 3.6ms | 1.9ms | **47% faster** |
| 1,000 | 50 | 9.0ms | 3.9ms | **57% faster** |
| 10,000 | 10 | 4.0ms | 3.7ms | 8% faster |
| 10,000 | 20 | 8.3ms | 7.5ms | 10% faster |
| 10,000 | 50 | 21.7ms | 15.1ms | **30% faster** |
| 50,000 | 10 | 15.8ms | 16.2ms | ~same |
| 50,000 | 20 | 31.1ms | 28.8ms | 7% faster |
| 50,000 | 50 | 71.0ms | 62.6ms | **12% faster** |

**Key insight:** TBB shines for short-lived threads where thread creation overhead dominates.
