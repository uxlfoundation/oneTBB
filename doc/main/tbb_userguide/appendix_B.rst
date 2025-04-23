.. _appendix_B:

Appendix B Mixing With Other Threading Packages
===============================================


Correct Interoperability
^^^^^^^^^^^^^^^^^^^^^^^^

You can use |short_name| with other threading packages. No additional 
effort is required.


Here is an example that parallelizes an outer loop with OpenMP and an
inner loop with |short_name|.

.. literalinclude:: ./examples/tbb_mixing_other_runtimes_example.cpp
   :language: c++
   :start-after: /*begin outer loop openmp with nested tbb*/
   :end-before: /*end outer loop openmp with nested tbb*/


The ``#pragma omp parallel`` instructs OpenMP to create a team of
threads. Each thread executes the code block statement associated with
the directive. 
The ``#pragma omp for`` indicates that the compiler should
distribute the iterations of the following loop among the threads in the existing thread team, enabling parallel execution of the loop body.


See the similar example with the POSIX\* Threads:

.. literalinclude:: ./examples/tbb_mixing_other_runtimes_example.cpp
   :language: c++
   :start-after: /*begin pthreads with tbb*/
   :end-before: /*end pthreads with tbb*/


.. _avoid_cpu_overutilization:

Avoid CPU Overutilization
^^^^^^^^^^^^^^^^^^^^^^^^^

While you can safely use |short_name| with other threading packages without
affecting the execution correctness, simultaneous running of large
number of threads from multiple thread pools concurrently can lead to oversubscription.
This may significantly overutilize system resources, affecting the
execution performance.


Consider the previous example with nested parallelism, but with an
OpenMP parallel region executed within |short_name| parallel loop:

.. literalinclude:: ./examples/tbb_mixing_other_runtimes_example.cpp
   :language: c++
   :start-after: /*begin outer loop tbb with nested omp*/
   :end-before: /*end outer loop tbb with nested omp*/


Due to the semantics of OpenMP parallel region, this composition of parallel
runtimes may result in a quadratic number of simultaneously running
threads. Such oversubscription can degrade the performance.


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


The ``TCM: TCM_ENABLE 1`` line confirms that Thread
Composability Manager is active.

When used with the OpenMP
implementation of Intel(R) DPC++/C++ Compiler, TCM
allows runtimes to avoid simultaneous scheduling excessive threads in the scenarios similar
to the above.


You can submit feedback or ask questions about Thread Composability Manager through |short_name| `GitHub Issues
<https://github.com/uxlfoundation/oneTBB/issues>`_ or `Discussions
<https://github.com/uxlfoundation/oneTBB/discussions>`_.


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

