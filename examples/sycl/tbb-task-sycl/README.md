# `TBB-Task-Sycl` Sample
This sample illustrates how two oneAPI Threading Building Blocks (oneTBB)
 tasks can execute similar computational kernels, with one task executing the SYCL code and the other task executing the TBB code. This `tbb-task-sycl` sample code is implemented using C++ and SYCL* for CPU and GPU.

## Purpose
The purpose of this sample is to show how similar computational kernels can be executed by two TBB tasks, with one executing on SYCL-compliant code and another on TBB code.

## Building the TBB-Task-Sycl Program

### Setting Environment Variables
When working with the Command Line Interface (CLI), you should configure the oneAPI toolkits using environment variables. Set up your CLI environment by sourcing the `setvars` script every time you open a new terminal window. This practice ensures your compiler, libraries, and tools are ready for development.

> **Note**: If you have not already done so, set up your CLI environment by sourcing the `setvars` script located in the root of your oneAPI installation.
>
> Linux*:
> - For system wide installations: `. /opt/intel/oneapi/setvars.sh`
> - For private installations: `. ~/intel/oneapi/setvars.sh`
> - For non-POSIX shells, like csh, use the following command: `$ bash -c 'source <install-dir>/setvars.sh ; exec csh'`
>
> Windows*:
> - `C:\"Program Files (x86)"\Intel\oneAPI\setvars.bat`
> - For Windows PowerShell*, use the following command: `cmd.exe "/K" '"C:\Program Files (x86)\Intel\oneAPI\setvars.bat" && powershell'`
>
> Microsoft Visual Studio:
> - Open a command prompt window and execute `setx SETVARS_CONFIG " "`. This only needs to be set once and will automatically execute the `setvars` script every time Visual Studio is launched.

## Building the Sample

## Running the Sample

### Example of Output
```bash
    executing on CPU
    executing on GPU
    Heterogeneous triad correct.
    TBB triad correct.
    input array a_array: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
    input array b_array: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
    output array c_array on GPU: 0 1.5 3 4.5 6 7.5 9 10.5 12 13.5 15 16.5 18 19.5 21 22.5
    output array c_array_tbb on CPU: 0 1.5 3 4.5 6 7.5 9 10.5 12 13.5 15 16.5 18 19.5 21 22.5
```

