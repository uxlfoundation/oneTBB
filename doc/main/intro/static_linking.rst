.. _static_linking:

Static Linking of oneTBB
************************

|full_name| is designed, built, and distributed as a shared library. Building a static
library is possible, but it is not a recommended configuration. CMake emits a warning, validation
coverage is limited, and several features are unavailable by default. If a static oneTBB
library is used by an application, you should carefully validate its performance and correctness.

Using a static library is not a problem in itself. The problems arise when a static oneTBB
library is combined with another static or shared oneTBB library in the same application.
In the good case, this is a potential performance problem due to oversubscription but in the
worst case, something fails in a way that is hard to diagnose.

This page explains why the shared library is the recommended form, what can go wrong for
performance and for correctness when more than one copy of oneTBB ends up in the same process,
which features are unavailable in a static build, and how to produce a static build anyway.

Why a Shared Library Is Recommended
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

oneTBB's thread pool is used to manage a machine's hardware threads. To do that job well,
the oneTBB task scheduler and its worker thread pool must have a single instance across
the entire process. The most practical way to create a program-wide singleton is to put
it in a shared library that every component links against.

Performance Risks of Multiple oneTBB Copies
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Performance problems can arise because each instance of oneTBB creates roughly as many worker
threads as there are hardware threads. If a program contains ``k`` independent oneTBB schedulers,
it gets ``k`` times as many software threads as hardware threads. This oversubscription may
cause excessive context switching and cache contention.

This oversubscription issue is most noticeable when nested parallelism is used. If a oneTBB
algorithm or task calls into a library that uses a different oneTBB instance, the number
of threads increases and the tasks in each layer become isolated from each other, limiting
flexibility in work stealing and load balancing. This is the case when parallelism at the
application level is combined with parallelism at the library level.

Oversubscription also cannot be capped from a single place. A ``tbb::global_control`` object
that limits ``max_allowed_parallelism`` constrains only the copy of oneTBB it was created in,
so the other copies keep creating their own worker threads.

Correctness Risks of Multiple oneTBB Copies
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Beyond performance, mixing copies may break correctness. There are a number of subtle issues
that can arise. One example is that oneTBB objects can carry state that belongs to the copy
that created it, including task arenas, scheduler state stored in thread-local storage, etc.
Passing a ``tbb::task_arena``, ``tbb::task_group``, ``tbb::flow::graph``, or other oneTBB
object to a component that uses a different copy of oneTBB is undefined behavior.

Settings that are documented as process-wide also stop behaving that way. ``tbb::global_control``
applies only to the scheduler instance it is created in, not to the other copies, so a component
cannot rely on it to establish an application-wide policy.

Features Not Available in a Static Build
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A static build disables oneTBB's run-time dynamic loading, which several features depend on:

* The ``tbbbind`` libraries are not built, and oneTBB cannot load them at run time.
  ``tbb::info`` and ``tbb::task_arena::constraints`` therefore cannot report or apply
  topology-based constraints.

* ``tbbmalloc_proxy`` is not built. The scalable allocator is still usable through the
  explicit interfaces, such as ``tbb::scalable_allocator``, ``tbb::cache_aligned_allocator``,
  and ``scalable_malloc``.

  * oneTBB will not load the Thread Composability Manager (TCM) library at run time, 
  so coordination with other threading runtimes through TCM is unavailable.

* IPO is enabled only for shared library builds, so a static oneTBB may be slower than
  the shared one even in the single-copy case.

.. _static_linking_recommendations:

Recommendations If You Must Link Statically
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If you still choose to statically link oneTBB into your application, follow these
recommendations:

* Use only a single copy of oneTBB for the entire application if possible.

* Detect additional copies at run time. Setting the ``TBB_VERSION`` environment variable to
  ``1`` makes each initialized oneTBB instance print its version information to ``stderr``,
  so more than one block of output means more than one copy is loaded. See
  :ref:`Mixing_Two_Runtimes` for an example of the output.

* Do not pass oneTBB objects, such as ``tbb::task_arena``, ``tbb::task_group``, or
  ``tbb::flow::graph``, across component boundaries unless all of the components share the
  same copy of oneTBB.

* Validate the performance and correctness of the resulting application yourself. Static
  builds receive only light validation coverage.

Building oneTBB as a Static Library
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The CMake build system supports static library builds.

Configure the build with ``BUILD_SHARED_LIBS=OFF``. The configure step prints a warning
stating that the configuration is highly discouraged, but the build proceeds.

For the full procedure, including the effects on the rest of the build configuration and how
to consume the result, see `Building oneTBB as a Static Library
<https://github.com/uxlfoundation/oneTBB/blob/master/cmake/README.md#building-onetbb-as-a-static-library>`_
in the Build System Description.
