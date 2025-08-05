# Code Samples demonstrating interoperability of oneAPI Threading Building Blocks with SYCL

| Code sample name                          | Description
|:---                                       |:---
| tbb-async-sycl             | The calculations are split between TBB Flow Graph asynchronous node that calls SYCL* kernel on GPU while TBB functional node does CPU part of calculations.
| tbb-task-sycl              | One TBB task executes SYCL code on GPU while another TBB task performs calculations using TBB parallel_for.
| tbb-resumable-tasks-sycl   | The calculations are split between TBB resumable task that calls SYCL kernel on GPU while TBB parallel_for does CPU part of calculations.
