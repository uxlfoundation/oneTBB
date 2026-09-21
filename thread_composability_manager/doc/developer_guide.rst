TCM Developer Guide
###################

Usage Model
***********

Below is a simple example of a usage model a parallel runtime should follow to successfully use TCM.

#. Include :code:`tcm.h` header file and link the TCM library with the project.

   .. code-block:: cpp

       #include "tcm.h"

   .. note:: If necessary, adjust project settings so that the compiler can find :code:`tcm.h`
             header file and the TCM library when building and linking the project.

#. Register a client, providing a callback function to react on permit changes.

   .. code-block:: cpp

       tcm_client_id_t client_id;
       tcmConnect(client_callback, &client_id);

#. Describe the resources needed using the :code:`tcm_permit_request_t` data structure.

   .. code-block:: cpp

       tcm_permit_request_t request = TCM_PERMIT_REQUEST_INITIALIZER;

   .. note:: To describe a portion of platform resources adjust the fields of
             :code:`tcm_permit_request_t` object accordingly. Refer to description of
             :ref:`tcm_permit_request_t <tcm_permit_request_t>` data structure for more info.

#. Request a permit for resources.

   .. code-block:: cpp

       uint32_t grant = 0;
       tcm_permit_t permit {
           &grant, /*cpu_masks*/nullptr, /*size*/1, /*state*/{}, /*flags*/{}
       };
       tcm_permit_handle_t permit_handle = nullptr;
       tcmRequestPermit(client_id, request, callback_arg, &permit_handle,
                        &permit);

   .. note:: The :code:`tcmRequestPermit` function might result in permit switched to
             :code:`TCM_PERMIT_STATE_PENDING` state, meaning that the requested resources are being
             used by another permit, and the requesting side should wait until TCM is able to
             satisfy the permit, hence activating it and notifying the client through invocation of
             a client callback.

#. Once the permit is activated, register that number of threads that were suggested by TCM.

   .. code-block:: cpp

       uint32_t suggested_concurrency = permit.concurrencies[0];

       // Wake up suggested_concurrency number of threads and register them with
       // the permit
       tcmRegisterThread(permit_handle); // Invoked by each participating thread

#. Deactivate, activate, and re-request the permit depending on the resources usage model.

   .. code-block:: cpp

       // Once processing block ends, deactivate the permit
       tcmDeactivatePermit(permit_handle);

       // Activate permit when processing begins again
       tcmActivatePermit(permit_handle);

       // Re-request if desired number of threads changes
       tcmRequestPermit(client_id, new_request, callback_arg,
                        &existing_permit_handle, &permit);

   .. note:: Similarly to :code:`tcmRequestPermit` function, call to :code:`tcmActivatePermit` might
             result in a permit switched to :code:`TCM_PERMIT_STATE_PENDING` state, meaning that the
             requested resources are being used by another permit. In this case, the requesting side
             should wait until TCM is able to satisfy the permit, hence activating it and notifying
             the client through invocation of a client callback.

#. Unregister threads and release permit once its resources are no longer needed.

   .. code-block:: cpp

       // By each thread, which was previously registered with the permit, run
       tcmUnregisterThread();

       // Invoke once
       tcmReleasePermit(permit_handle);

#. Disconnect from TCM when resource coordination is no longer planned.

   .. code-block:: cpp

       tcmDisconnect(client_id);


   .. note:: When running application that uses TCM, set :code:`TCM_ENABLE=1` environment variable
             to actually enable its use.

Refer to :doc:`api_reference` to find more detailed information about TCM API.

See also :ref:`TCM usage examples <tcm_usage_examples>` to learn by example.

Permit State Transitions
************************

The diagram below shows possible transitions of a permit state. Black arrows show state transitions
when TCM API is invoked by a client, while red arrows show state transitions initiated by TCM.
Whenever change is not initiated by a client itself, this client is notified through invocation of a
callback function that was registered during the call to :code:`tcmConnect`.

.. image:: ./resources/state_transitions.png
   :width: 800px
   :height: 400px
   :scale: 100 %
   :alt: State transitions of a resource permit
   :align: center


Resource Permits and Teams of Threads
*************************************

The table below shows relation between permit state, team of threads, and whether the resources
described by a permit are allowed to be used or not.

Resource permit:

- Requested by language runtime (RT)
- Granted by TCM
- Includes maximum concurrency and CPU mask (if :code:`tcm_cpu_constraints_t` was specified)
- The CPU mask may be different from concurrency

Team of threads

- Managed by language RT
- Can only be active with a valid resource permit


+----------------+------------------+-----------------------------------------------------------+------------------------------------+
| Permit State   | Resource usage   | Thread Team State                                         | Reactivation Speed                 |
+================+==================+===========================================================+====================================+
| Void/No permit | Not allowed      | **Cold**: Team sleeping or disbanded.                     | Slow - same as new request         |
|                |                  | No Language RT configuration maintained.                  |                                    |
+----------------+------------------+-----------------------------------------------------------+------------------------------------+
| Inactive       | Not allowed      | **Warm**: Team not actively consuming CPU resources.      | Fast - if reclaimed by Language RT |
|                |                  | Some configuration for Language RT is maintained.         |                                    |
+----------------+------------------+-----------------------------------------------------------+------------------------------------+
| Pending        | Not allowed      | **Warm** or **Cold**.                                     |                                    |
|                |                  | Language RT waits for the permit to be granted.           |                                    |
+----------------+------------------+-----------------------------------------------------------+------------------------------------+
| Idle           | Allowed          | **Hot**: Team is at quiescent point, might actively spin. | Fastest                            |
|                |                  | Highly configured for Language RT.                        |                                    |
+----------------+------------------+-----------------------------------------------------------+------------------------------------+
| Active         | Allowed          | **Active**: Team is executing tasks for Language RT.      | (Already Active)                   |
|                | (permit granted) | Highly configured for Language RT.                        |                                    |
+----------------+------------------+-----------------------------------------------------------+------------------------------------+

Composition Scenarios
*********************

The section describes various composition scenarios of parallel runtimes that can occur in runtime
providing details on possible transition of CPU resources between them.

Sequential Requests
===================

This represent composition of TCM permit requests where one or more clients request resources one
after the other.

.. code-block:: cpp
   :caption: Example of sequential permit requests for resources from multiple clients

    #pragma omp parallel for
    for(int i = 0; i < 100; ++i) {
        /*OpenMP threads working*/
    }

    tbb::parallel_for(0, 100, [](int) {
        /*TBB threads working*/
    });

    #pragma omp parallel for
    for(int i = 0; i < 100; ++i) {
        /*OpenMP threads working again*/
    }

At every moment of time the resources are meant to be used by only one parallel runtime.

Although, this represents the simplest composition scenario, it is still can benefit from using the
Thread Composability Manager. This is because usually resources are not released immediately after a
parallel region, but remain being used for some time anticipating new parallel work to appear soon.
It is important to notify TCM about such situation through a call to :code:`tcmIdlePermit` so that
corresponding resources can be re-used by subsequent requests from possibly another runtime.

Concurrent Requests
===================

Concurrent permit requests for resources appear when two or more clients request resources
concurrently and independently. No client makes new requests while holding one.

.. code-block:: cpp
   :caption: Example of independent requests happening concurrently: one client requests for
             resources to accommodate of :math:`P_1` threads, the other - :math:`P_2`

    std::thread omp_call([&] {
        #pragma omp parallel for num_threads(P1)
        for(int i = 0; i < 100; ++i) {
            /*OpenMP threads working*/
        }
    });

    std::thread tbb_call([&] {
        tbb::task_arena a(P2);
        a.execute([&] {
            tbb::parallel_for(0, 100, [](int) {
                /*TBB threads working*/
            });
        });
    });

    omp_call.join();
    tbb_call.join();

Concurrent requests can be subdivided onto two possible scenarios:

1. *Independent requests*

   Requests are not coordinated and may compete for the same resources.

2. *Perfect or hierarchical concurrency*.

   Multiple resource requests are spread across available resources with no oversubscription. For
   example, each request is done for cores in a separate NUMA domain.

Nested Requests
===============

A nested permit request corresponds to a situation when a client request a permit for resources
while holding and using another permit from one of the previous requests.

.. code-block:: cpp
   :caption: Example of nested permit requests

    tbb::parallel_for(0, 100, [](int) {
        /*TBB threads working*/

        #pragma omp parallel for
        for(int i = 0; i < 100; ++i) {
            /*OpenMP threads working*/
        }
    });

Possible scenarios:

1. *Agnostic nesting*.

   Each level requests parallelism independently, as if it was alone. This is the typical case of
   oneAPI Math Kernel Library (oneMKL) calls nested in oneAPI Threading Building Blocks (oneTBB)
   calls.

2. *Perfect or hierarchical nesting*.

   The outer level limits its concurrency requesting widely spread resources (e.g. one core per
   every socket), under the assumption/knowledge about inner levels utilizing “close” resources
   (e.g. all cores in a socket).

Combined Use Cases
==================

The combined use cases include sequential, concurrent, and nested use cases mixed in the code.

.. code-block:: cpp
   :caption: Example of sequential with nested calls

    #pragma omp parallel for
    for(int i = 0; i < 100; ++i) {
        /*OpenMP threads working*/
    }

    tbb::parallel_for(0, 100, [](int) {
        /*TBB threads working*/
        #pragma omp parallel for
        for(int i = 0; i < 100; ++i) {
            /*OpenMP threads working again*/
        }
    });


.. _tcm_usage_examples:

Usage examples
**************

Examples below demonstrate the use of TCM in various scenarios. Parallelism in these examples is
achieved through functional decomposition where initial amount of work is split among threads
participating in computation.

The examples below use the following helper function that returns an object of :code:`tcm_permit_t`
type. This structure is filled by TCM when its client wants to read the current state of a permit
data.

.. literalinclude:: ./examples/utils.h
    :language: c++
    :start-after: /* begin make_permit helper */
    :end-before: /* end make_permit helper */

Ad hoc parallelism
==================

The simplest way to do computations in parallel is to create a bunch of threads, split the work
among these threads, and wait for them to finish. Such instantiated threads are usually busy only
with the useful work they are given, not distracting themselves on other non-payload activities.
This does not allow them to react on changes to resource permissions that can be communicated by
TCM. Below example demonstrates the use of TCM for such ad hoc scenarios.

Since resources can be already occupied by another parallel runtime or concurrent invocation of
same parallel region, using them disrespecting those other clients would result in platform
oversubscription. Therefore, the TCM client should first wait until the requested resources become
free and TCM decides to re-distribute them to this client. Once it is so, the permit is activated
and TCM invokes client's callback function with a handle of a permit that has just been changed.

To signal about changes in a permit back to a parallel region, this example uses the following
structure:

.. literalinclude:: ./examples/ad-hoc-parallelism-example.cpp
   :language: c++
   :start-after: /* begin synchronization data */
   :end-before: /* end synchronization data */

The pointer to instance of this structure is passed to callback function as the value for its
:code:`callback_arg` parameter.

Because threads in such fixed parallel regions cannot react on changes to recommendations of
resources usage, the negotiation callback function is only needed to signal parallel region about
activation of its permit, and can be written as the following:

.. literalinclude:: ./examples/ad-hoc-parallelism-example.cpp
   :language: c++
   :start-after: /* begin negotiation callback */
   :end-before: /* end negotiation callback */

Callback is invoked to notify client about changes in its permit so that client can react on these
changes accordingly. In this example, client's callback is called once permit is activated. The
negotiation callback function above demonstrates how to read permit data properly. The
:code:`tcmGetPermitData` function can return data of a being changed permit. This is indicated by
:code:`tcm_permit_flags_t::stale` bit flag, and it means that the callback is going to be invoked
one more time once changes to permit are finalized by TCM. Thus, client should abandon the data it
has just read.

Besides splitting the work among instantiated threads, the main function in this example consults
with TCM to determine the number of threads it can use so that the platform is not oversubscribed.
To do so it connects to TCM, requests a permit, waits for it to be activated, and then reads the
recommended number of threads for use in the :code:`grant` variable. Telling TCM that the permit
will not allow negotiations once it is activated is done by assigning :code:`1` to the
:code:`tcm_permit_flags_t::rigid_concurrency` flag during setting up the
:code:`tcm_permit_request_t` structure for a permit request.

The first thing each thread does before executing the work it is created for is to register itself
with the permit, in which it participates. This is necessary to tell TCM that the thread consumes
one of the resources assigned to a permit, and is done by calling :code:`tcmRegisterThread` function
passing the instance of :code:`tcm_permit_handle_t` whose resource this thread is going to consume.
At the end of its work, thread unregister itself from permit by calling :code:`tcmUnregisterThread`.

Once all the threads finish with their task, the main thread releases the resources by calling
:code:`tcmReleasePermit` TCM function. This marks the resources described by passed instance of
:code:`tcm_permit_handle_t` as free, hence making them available for other clients.

At the end, main thread disconnects from TCM, essentially telling it that the client won't have
future permit requests.

.. literalinclude:: ./examples/ad-hoc-parallelism-example.cpp
   :language: c++
   :start-after: /* begin parallel compute example */
   :end-before: /* end parallel compute example */

This is basic example of TCM integration. Despite lacking functionality for dealing with overheads
related to threads management and reacting on changes in utilization of resources from other
clients, it demonstrates main API calls parallel runtime should follow to make use of Thread
Composability Manager and reduce otherwise potential CPU oversubscription.

Pool of Threads
===============

Below is a more complex example that demonstrates usage of TCM by a :code:`client_thread_pool` class
that manages a pool of threads. Unlike the example from `Ad hoc parallelism`_ this example creates
worker threads once, effectively re-using them to perform computations in parallel. A worker thread
executes tasks only while the pool holds a permit, and no more workers do so than the permit grants.
The thread pool reacts to changes in permit by updating the grant, hence waking up missing threads
or putting excessive ones to sleep. The example also includes synchronization code that allows
invocation of a parallel computation concurrently with itself, making sure the resources are not
released while there is work to do.

The code re-uses :code:`make_permit` helper from `Ad hoc parallelism`_ example.

Pool interface
--------------

The public interface of the pool consists of the :code:`parallel_for` member function and the pool
lifetime management. The :code:`parallel_for` function asks TCM for the concurrency it is allowed to
use, splits the given range into that many chunks, submits them as tasks, and waits for their
completion. Since the pool does not need the resources anymore once the work is done, it deactivates
the permit, thus letting TCM re-distribute the resources to other clients.

The constructor connects the client to TCM and creates the worker threads. Note that the workers are
created eagerly, while the permit is requested lazily: the number of threads a pool owns is its own
business, whereas the number of threads that are allowed to run simultaneously is negotiated with
TCM.

The destructor stops the workers, releases the permit and disconnects from TCM.

.. literalinclude:: ./examples/client-thread-pool.cpp
   :language: c++
   :start-after: /* begin pool interface */
   :end-before: /* end pool interface */

Permit management
-----------------

Unlike the example from `Ad hoc parallelism`_ section, this pool keeps a single permit for its whole
lifetime: :code:`tcmRequestPermit` creates a permit when it is given a null handle and re-uses the
permit the handle refers to otherwise. Also, several :code:`parallel_for` calls may be running
concurrently in the same pool, so they share that permit: the first of them requests it, the others
only wait until it becomes usable, and the last one to finish deactivates it.

A permit is usable when it is activated by TCM, that is its state equals to
:code:`TCM_PERMIT_STATE_ACTIVE`, and the data read for it is not marked with the
:code:`tcm_permit_flags_t::stale` flag. Waiting for such a state is done through the
:code:`permit_updates` counter, which is incremented every time new permit data is published, either
by a permit request or by the negotiation callback. Reading the counter before reading the permit
data guarantees that an update, which happens in between, is not missed.

Once the permit data is read successfully, the granted concurrency is applied to the pool. Because
several threads may read the permit data concurrently, each read takes a value of the
:code:`permit_epoch` counter, and only the newest read is allowed to publish the grant it has
observed. The permit handle is stored before the grant is published, since the workers, which are
woken up by the new grant, register themselves with that handle.

.. literalinclude:: ./examples/client-thread-pool.cpp
   :language: c++
   :start-after: /* begin permit management */
   :end-before: /* end permit management */

Worker pool
-----------

The pool of threads translates the permit grant into the number of workers that are allowed to run
tasks. The :code:`allowed_threads` variable holds that number, and the workers, which do not fit
into it, keep sleeping on the :code:`pool_cv` condition variable. Increasing the grant wakes up the
missing threads, while revoking it puts the excessive ones to sleep as soon as they complete the
tasks they are busy with.

Each worker registers itself with the permit by the :code:`tcmRegisterThread` call before it starts
taking tasks, and unregisters itself by the :code:`tcmUnregisterThread` call once it has no more
tasks to do. Therefore, a thread is known to TCM only for the time it actually consumes the
resources described by the permit.

.. literalinclude:: ./examples/client-thread-pool.cpp
   :language: c++
   :start-after: /* begin worker pool */
   :end-before: /* end worker pool */

Tasking
-------

The tasking part of the pool is not related to TCM, and is shown for completeness. It is a simple
deque of tasks, which is filled by the :code:`parallel_for` function and is drained by the workers.
The only TCM related detail here is that a worker does not wait for new tasks indefinitely: if no
work appears for a while, :code:`get_task` gives up so that the worker can leave the pool and stop
being counted by TCM as a thread that uses the resources.

.. literalinclude:: ./examples/client-thread-pool.cpp
   :language: c++
   :start-after: /* begin tasking */
   :end-before: /* end tasking */

The state of the pool described above is kept in the following data members:

.. literalinclude:: ./examples/client-thread-pool.cpp
   :language: c++
   :start-after: /* begin pool state */
   :end-before: /* end pool state */

Negotiation callback
--------------------

TCM invokes the negotiation callback to notify the pool that its permit has changed. When the change
is about the granted concurrency, the callback re-reads the permit data, which also applies the new
grant to the pool, hence waking up the missing workers or putting the excessive ones to sleep. In
any case, the callback publishes a permit update so that the threads waiting for the permit to
become usable re-examine its data.

.. literalinclude:: ./examples/client-thread-pool.cpp
   :language: c++
   :start-after: /* begin negotiation callback */
   :end-before: /* end negotiation callback */

This example demonstrates how a parallel runtime, which manages a pool of threads, can adjust the
number of threads it runs to the resources TCM grants it, both when the permit is requested and when
TCM renegotiates it later.
