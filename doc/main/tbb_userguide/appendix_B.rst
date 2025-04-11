.. _appendix_B:

Appendix B Mixing With Other Threading Packages
===============================================


Correct Interoperability
^^^^^^^^^^^^^^^^^^^^^^^^

|full_name| can be mixed with other threading packages. No special
effort is required to use any part of |short_name| with other threading
packages.


Here is an example that parallelizes an outer loop with OpenMP and an
inner loop with |short_name|.

.. literalinclude:: ./examples/tbb_mixing_other_runtimes_example.cpp
   :language: c++
   :start-after: /*begin outer loop openmp with nested tbb*/
   :end-before: /*end outer loop openmp with nested tbb*/


The ``#pragma omp parallel`` causes the OpenMP to create a team of
threads, and each thread executes the block statement associated with
the pragma. The ``#pragma omp for`` indicates that the compiler should
use the previously created thread team to execute the loop in parallel.


Here is the same example written using POSIX\* Threads.

.. literalinclude:: ./examples/tbb_mixing_other_runtimes_example.cpp
   :language: c++
   :start-after: /*begin pthreads with tbb*/
   :end-before: /*end pthreads with tbb*/


.. _avoid_cpu_overutilization:

Avoid CPU Overutilization
^^^^^^^^^^^^^^^^^^^^^^^^^

Although |short_name| can be mixed with other threading packages without
affecting the execution correctness, simultaneous running of large
number of threads from multiple thread pools may end up oversubscribing
and overutilizing system resources, significantly affecting the
execution performance.


Consider the example with nested parallelism from above but now with the
OpenMP parallel region executed from |short_name| parallel loop.

.. literalinclude:: ./examples/tbb_mixing_other_runtimes_example.cpp
   :language: c++
   :start-after: /*begin outer loop tbb with nested omp*/
   :end-before: /*end outer loop tbb with nested omp*/


Due to semantics of OpenMP parallel region, such composition of parallel
runtimes will instantiate quadratic number of simultaneously running
threads, which will lead to performance drops.


|full_name| is able to negotiate on the usage of CPU resources,
cooperating with other threading runtimes through the Thread
Composability Manager, a CPU resource coordination layer.


Thread Composability Manager is an experimental feature in oneAPI, hence
it is disabled by default. To enable it, set ``TCM_ENABLE`` environment
variable to ``1``. To make sure it works as intended set ``TCM_VERSION``
environment variable to ``1`` before running your application and search
for the output starting from ``TCM:`` lines, as in the example:

::

    TCM: VERSION            1.3.0
    <...>
    TCM: TCM_ENABLE         1


, where the ``TCM: TCM_ENABLE         1`` line indicates that Thread
Composability Manager is enabled and works. When used with the OpenMP
implementation of Intel(R) DPC++/C++ Compiler, Thread Composability
Manager prevents creation of excessive threads in the scenarios similar
to the above.


You may use |short_name| communication channels (`GitHub issues
<https://github.com/uxlfoundation/oneTBB/issues>`_, `discussions
<https://github.com/uxlfoundation/oneTBB/discussions>`_, etc.) to also
ask questions and provide feedback on Thread Composability Manager.


.. note::
   The use of Thread Composability Manager to negotiate utilization of
   CPU resources requires support in threading packages. For most
   efficient coordination, it should be supported by all thread pools
   used within the application. Consult the documentation of other
   threading packages to see if such support exists there.


.. rubric:: See also

* `End Parallel Runtime Scheduling Conflicts with Thread Composability
  Manager
  <https://www.intel.com/content/www/us/en/developer/videos/threading-composability-manager-with-onetbb.html>`_

