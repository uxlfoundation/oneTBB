# Code Samples of Intel(r) oneAPI Threading Building Blocks (oneAPI TBB)

| Code sample name                          | Supported Intel(r) Architecture(s) | Description
|:---                                       |:---                                |:---
| tbb-async-sycl             | GPU, CPU  | The calculations are split between TBB Flow Graph asynchronous node that calls SYCL* kernel on GPU while TBB functional node does CPU part of calculations.
| tbb-task-sycl              | GPU, CPU  | One TBB task executes SYCL code on GPU while another TBB task performs calculations using TBB parallel_for.
| tbb-resumable-tasks-sycl   | GPU, CPU  | The calculations are split between TBB resumable task that calls SYCL kernel on GPU while TBB parallel_for does CPU part of calculations.

## License
Code samples are licensed under the MIT license. See
[License.txt](https://github.com/oneapi-src/oneAPI-samples/blob/master/License.txt) for details.

Third party program Licenses can be found here: [third-party-programs.txt](https://github.com/oneapi-src/oneAPI-samples/blob/master/third-party-programs.txt).
