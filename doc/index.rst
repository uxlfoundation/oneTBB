Design Principles of the Thread Composability Manager
=====================================================

The purpose of the Thread Composability Manager is to
distribute CPU resources between multiple clients. The clients can
request resources in arbitrary order, including nesting of the requests.

This document aims to explain the design principles and general idea of
the Thread Composability Manager, elaborating the communication protocol that is
used to interact with its clients.

Notation
========

There may be special marks throughout this document that denote
different stages of the API design and its implementation process. Their
purpose is to communicate the status of the feature being described.
These marks are:

-  **WIP\_IMPL** – API has been designed and agreed, but not yet
   implemented.

-  **WIP\_DSGN** – Neither API nor its implementation has been designed
   and agreed.

General Principles 
==================

1. The Thread Composability Manager (TCM) is not aware what its clients are. It
   treats them equally providing the interface to ask for new resources,
   adjust usage and release previously permitted resources.

2. TCM does not allocate or deallocate resources. Its sole purpose is to
   coordinate resources usage across its clients. A client is expected
   to request a new portion of resources as demand for those appears,
   and to release these resources once the work is done and no new
   demand is foreseen. Threads that utilize these resources are created
   by the clients as needed.

   **Note:** TCM makes no assumptions about which threads – from the
   application or from a client’s thread pool – utilize the granted
   concurrency. Clients should adjust the concurrency value as needed
   to account for application threads that are going to participate in
   a parallel region.

3. It is responsibility of the client to follow the negotiated permits.
   TCM assumes its clients are well-behaved and neither ignore nor abuse
   their resource permits.

4. TCM resolves resource requests in accordance with global restrictions
   set for the process (such as affinity masks). In other words, TCM
   respects constraints on the resources imposed on the application.

5. TCM provides no “independent progress” guarantee for its clients, that
   is whether and when a request for resources is satisfied depends on
   the resource usage by other clients.

6. TCM provides no formal fairness guarantee for its clients, though the
   implementation will apply fair strategies where appropriate.

7. In case of not being able to fully satisfy the request, TCM may:

   - reject the request, that is permit no use of additional resources.

   - partially satisfy the request, possibly by taking back some of
     earlier permitted resources from previous requests and therefore
     balancing resource usage across its clients.

   **Note**: These situations are considered normal behaviour, not an
   error or exception.

8. In case of not being able to satisfy the requested minimum, TCM lets
   the client know this by assigning PENDING state to the permit,
   allowing clients to wait until the necessary minimum becomes
   available.

9. If unsatisfied or partially satisfied requests exist and unused
   resources appear (e.g. released by another client), TCM notifies the
   corresponding clients through a registered callback to better satisfy
   their requests.

10. TCM may also invoke the callback to revoke some resources previously
    granted above the requested minimum.

    **Note**: Since clients cannot immediately react to reduced set of
    resources that was initially negotiated, it is expected that these
    clients will reduce the resources usage as soon as execution allows.
    Depending on the chosen resource distribution strategy, it may
    happen that the system is oversubscribed for a limited time; TCM
    should however try avoiding that as much as possible.

Resource Requests Use Cases 
===========================

Sequential Requests 
-------------------

**Use Case:** One or more clients request resources one after the other.

Example:

*Listing 1: Sequential requests for resources from multiple clients.*

.. code:: cpp

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

At every moment of time there is only one active request from one of the
clients.

Concurrent Requests 
-------------------

**Use Case:** Two or more clients request resources concurrently and
independently. No client makes new requests while holding one.

Possible scenarios:

1. *Independent requests*

   Requests are not coordinated and may compete for the same resources.

Example:

*Listing 2: Independent requests happening concurrently: one client
requests for :math:`P_{1}` resources, the other - for :math:`P_{2}`.*

.. code:: cpp

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

2. *Perfect or hierarchical concurrency*.

   Multiple resource requests are spread across available resources
   with no oversubscription. For example, each request is done for
   cores in a separate NUMA domain.

Nested Requests 
---------------

**Use Case:** One or more clients request resources while using the
permit from one of the previous requests.

Common case:

*Listing 3: Nested requests for resources from different runtimes.*

.. code:: cpp

    tbb::parallel_for(0, 100, [](int) {
        /*TBB threads working*/

        #pragma omp parallel for
        for(int i = 0; i < 100; ++i) {
            /*OpenMP threads working*/
        }
    });

Possible scenarios:

1. *Agnostic nesting*.

   Each level requests parallelism independently, as if it was alone.
   This is the typical case of MKL calls nested in TBB calls.

2. *Perfect or hierarchical nesting*.

   The outer level limits its concurrency requesting widely spread
   resources (e.g. one core per every socket), under the
   assumption/knowledge about inner levels utilizing “close” resources
   (e.g. all cores in a socket).

Nested Support Requirement 
~~~~~~~~~~~~~~~~~~~~~~~~~~~

To track the nested requests correctly, the clients are required to pass
some additional info to the nested requests so that TCM knows that this
new request is not a separate request but goes in addition to the previous
outer one.

To satisfy this requirement, it is not practical to oblige the clients
explicitly pass any data down through various software layers that might
not know about each other or even do not work with the Thread Composability
Manager directly. A good protocol for this should be fully implicit for
such intermediate software layers, likely using some form of thread local
storage.

Combined Use Cases 
-------------------

The combined use cases include sequential, concurrent, and nested use
cases mixed in the code.

Sequential with Nested 
~~~~~~~~~~~~~~~~~~~~~~~

*Listing 4: Example of sequential with nested calls.*

.. code:: cpp

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

**TODO**: Should hot teams outlive the first loop?

Priorities
----------

TBB arenas might have a priority – low, normal, or high – that affect
how threads are balanced between the arenas. SYCL/DPC++ have a similar
notion of a priority for queue.

The Thread Composability Manager Support Guarantees
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Each permit request has a priority. Priorities can be of low, medium,
and high values. If the client does not explicitly specify a priority,
the default value of normal is used.

**[WIP\_DSGN]** The Thread Composability Manager treats permit requests from
different clients equally. The value of priority affects only requests from
single client, i.e., the request with priority is relative to other requests
with priorities within the same client.

Given several requests from single client, the Thread Composability Manager
distribute resources in accordance with the available resources and the
priorities specified in these requests, servicing the requests from
higher to lower priorities order.

Use Cases of Permit Requests with Priorities
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**1. Sequential**

Since all the requests are separated in time and priorities are made
to have relations between the requests, in any given moment of time
there is no any other request to have a relation with the current
one in terms of priorities. Therefore, there is no difference
whether these requests are made explicitly specifying the priority
or not.

**2. Concurrent**

*Listing 5: Concurrent permit requests with priorities specified on 8
threads machine.*

.. code:: cpp

    std::thread tbb_high_priority_work([&] { // 1st in time
        const int concurrency = 6;
        tbb::task_arena a_high(
            concurrency,
            /*reserved_for_masters*/1,
            tbb::task_arena::priority::high
        );
        
        a_high.execute([&] {
            tbb::parallel_for(0, I, [](int) {
                /*parallel processing using TBB threads*/
            });
        });
    });

    std::thread tbb_low_priority_work([&] { // 2nd in time
        const int concurrency = tbb::task_arena::automatic;
        tbb::task_arena a_low(
            concurrency,
            /*reserved_for_masters*/1,
            tbb::task_arena::priority::low
        );

        a_low.execute([&] {
            tbb::parallel_for(0, J, [](int) {
                /*parallel processing using TBB threads*/
            });
        });
    });

    std::thread omp_work([&] { // 3rd in time
        #pragma omp parallel for
        for(int i = 0; i < K; ++i) {
            /*parallel processing using OpenMP threads*/
        }
    });

    tbb_high_priority_work.join();
    tbb_low_priority_work.join();
    omp_work.join();

**[WIP\_DSGN]** Consider the use case shown in 5 executed on a machine with
8 threads. Imagine that these permit requests are made chronologically from
top to bottom, that is in the order :code:`tbb_high_priority_work`,
:code:`tbb_low_priority_work`, and :code:`omp_work`.
In this case, the :code:`tbb_high_priority_work` request will be given 6
threads at first, the :code:`tbb_low_priority_work` request will use only
2 threads, because it is the request of lower priority, hence cannot compete
for the resources given to more prioritized requests of the same client.
Before the OpenMP request, the machine is already saturated. Once the
:code:`omp_work` request is made, the resource renegotiation mechanism ends
up with equal distribution of resources between TBB and OpenMP runtimes, and
since high priority request requires more resources than was given by the
Thread Composability Manager, the :code:`tbb_low_priority_work` request gets
nothing.

-  4 resources are given to :code:`tbb_high_priority_work`;

-  0 resources are given to :code:`tbb_low_priority_work`;

-  4 resources are given to :code:`omp_work`.

**3. Nested**

There are two possible scenarios:

1. The outer loop has higher priority than the nested loop.

   To avoid priority inversion problem, the Thread Composability Manager
   should detect such scenarios and raise the priority of a nested loop to
   a priority level of an outer loop.

2. The outer loop has lower priority than the nested loop.

   No special support is necessary. It is okay for the Thread Composability
   Manager to satisfy nested prioritized requests as usual.

The Thread Composability Manager Protocol 
=========================================

Clients request for and release the permits using the Thread Composability
Manager API. Each permit contains info about the resources a client can use.
Clients can have multiple requests at the same time, grouping them together
using unique client IDs that are assigned by the Thread Composability
Manager upon connecting to it.

Connecting to and disconnecting from the Thread Composability Manager 
---------------------------------------------------------------------

Before asking for a permit every client should register itself with the
Thread Composability Manager using:

.. code:: cpp

    tcm_result_t tcmConnect(tcm_callback_t callback, tcm_client_id_t* client_id)

+-------------------+--------+--------------------------------------------------------------------------------+
| Parameter         | Type   | Description                                                                    |
+===================+========+================================================================================+
| :code:`callback`  | In     | Permit renegotiation callback.                                                 |
+-------------------+--------+--------------------------------------------------------------------------------+
| :code:`client_id` | Out    | Client ID assigned by the Thread Composability Manager for further relation.   |
+-------------------+--------+--------------------------------------------------------------------------------+

If the client does not expect to request or release resources anymore,
it should close the connection by calling:

.. code:: cpp

    tcm_result_t tcmDisconnect(tcm_client_id_t client_id)

+-------------------+--------+---------------------------+
| Parameter         | Type   | Description               |
+===================+========+===========================+
| :code:`client_id` | In     | Client ID to disconnect   |
+-------------------+--------+---------------------------+

Requesting a permit 
--------------------

Clients request for a permit using:

.. code:: cpp

    tcm_result_t tcmRequestPermit(tcm_client_id_t client_id,
                                  tcm_permit_request_t request,
                                  void* callback_arg,
                                  tcm_permit_handle_t* permit_handle,
                                  tcm_permit_t* permit)

+-----------------------+----------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| Parameter             | Type     | Description                                                                                                                                                                                                                                                     |
+=======================+==========+=================================================================================================================================================================================================================================================================+
| :code:`client_id`     | In       | Client ID obtained by tcmConnect.                                                                                                                                                                                                                              |
+-----------------------+----------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`request`       | In       | Specification of resources requested.                                                                                                                                                                                                                           |
+-----------------------+----------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`callback_arg`  | In       | The argument to pass into the callback function (set previously using :code:`tcmConnect`) in case of a subsequent permit renegotiation.                                                                                                                        |
+-----------------------+----------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`permit_handle` | In/Out   | Descriptor of resources permitted by the Thread Composability Manager for use by the client. Assign :code:`nullptr` before passing to this function to request a new permit. Pass a descriptor of an existing permit to request updates to permit parameters.   |
+-----------------------+----------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`permit`        | In/Out   | The description of the resources given to the client as a response to this request. Allocated/deallocated by the client, filled in by the Thread Composability Manager.                                                                                         |
+-----------------------+----------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+

The function return value is used to indicate possible execution errors,
not the availability of resources. After a successful invocation, the
caller should check the permit state and fields to ensure resource usage
is allowed.

Updating a permit request
~~~~~~~~~~~~~~~~~~~~~~~~~

Updating of a permit request is done using the :code:`tcmRequestPermit` API.

To indicate that it is an update of the existing permit request rather
than a request for a new one, client passes a :code:`permit_handle` value that
was returned by a previous call to :code:`tcmRequestPermit`.

The parameters of a permit request that can be changed are:

-  Callback argument (:code:`callback_arg` parameter of the :code:`tcmRequestPermit`)

-  Request priority (see `Priorities of Permit
   Requests <#priorities-of-permit-requests>`__)

-  Minimum and maximum software threads (see `Permit
   Requests <#permit-requests>`__)

-  Permit properties (see `Properties of
   Permits <#properties-of-permits>`__)

Reading Latest Permit Data
--------------------------

To get the latest values from the Thread Composability Manager on resources
allotted to a particular permit, the client should use the following API:

.. code:: cpp

    tcm_result_t tcmGetPermitData(tcm_permit_handle_t* permit_handle,
                                  tcm_permit_t* permit)

+-----------------------+----------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| Parameter             | Type     | Description                                                                                                                                                               |
+=======================+==========+===========================================================================================================================================================================+
| :code:`permit_handle` | In       | Existing descriptor of resources permitted by the Thread Composability Manager for use by the client.                                                                     |
+-----------------------+----------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`permit`        | In/Out   | The description of the resources given to the client as a response to this request. Allocated/deallocated by the client, filled in by the Thread Composability Manager.   |
+-----------------------+----------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------+

**Note**: Due to possible concurrent requests from clients, resulting in
redistribution of resources by the Thread Composability Manager, the data
received in a :code:`permit` might be already old by the time the thread
returns from the :code:`tcmGetPermitData` function. In some situations,
the Thread Composability Manager can detect this is happening during the
call to :code:`tcmGetPermitData`, in which case a :code:`stale` flag of
the received permit is set to true (see section about `permit properties
<#properties-of-permits>`__). In any case, the client is responsible for
synchronization of multiple, possibly different, copies of permit’s data
on its own side.

**Note:** :code:`tcmGetPermitData` is designed to be lightweight, lock-free
function.

Threads of the client 
----------------------

The client utilizes granted CPU resources by running one or more
software threads.

To register a thread that will be working as part of the resource
permit, user calls:

.. code:: cpp

    tcm_result_t tcmRegisterThread(tcm_permit_handle_t permit_handle)

+-----------------------+--------+--------------------------------------------------------------------------------+
| Parameter             | Type   | Description                                                                    |
+=======================+========+================================================================================+
| :code:`permit_handle` | In     | Descriptor of the granted resources current thread is going to be a part of.   |
+-----------------------+--------+--------------------------------------------------------------------------------+

To unregister a particular thread to denote that it won't be working as
part of the latest resource permit, user calls:

.. code:: cpp

    tcm_result_t tcmUnregisterThread()

This API should be called by every thread that is going to be a part of
the permit, including the thread that requested the permit.

See the 9.1.1. for an implementation approach.

Idling, Activating and Deactivating a Permit 
---------------------------------------------

There might be situations when the client having the active permit does
not have the work to process as part of this permit. Though, the client
anticipates the work to appear soon. In this case, the client can avoid
releasing the permit, but tell the Thread Composability Manager that it
is in idle state now using :code:`zeIdlePermit`:

.. code:: cpp

    tcm_result_t tcmIdlePermit(tcm_permit_handle_t permit_handle)

+-----------------------+--------+------------------------------------------------+
| Parameter             | Type   | Description                                    |
+=======================+========+================================================+
| :code:`permit_handle` | In     | Descriptor of the resources to mark as idle.   |
+-----------------------+--------+------------------------------------------------+

The idle state indicates that the threads are actively spinning CPU
cycles looking for work.

If the resource is not immediately needed and client does not anticipate
the work soon, but still does not want to release the permit, it calls
:code:`zeDeactivatePermit`:

.. code:: cpp

    tcm_result_t tcmDeactivatePermit(tcm_permit_handle_t permit_handle)

+-----------------------+--------+----------------------------------------------+
| Parameter             | Type   | Description                                  |
+=======================+========+==============================================+
| :code:`permit_handle` | In     | Descriptor of the resources to deactivate.   |
+-----------------------+--------+----------------------------------------------+

TCM can also deactivate an idle permit and initiate a permit negotiation
– particularly, if idle resources granted by the permit are needed to
satisfy another request.

Once the work appeared again, the client can reactivate the permit
(either idle or inactive) using :code:`zeActivatePermit`:

.. code:: cpp

    tcm_result_t tcmActivatePermit(tcm_permit_handle_t permit_handle)

+-----------------------+----------+----------------------------------------------+
| Parameter             | Type     | Description                                  |
+=======================+==========+==============================================+
| :code:`permit_handle` | In/Out   | Descriptor of the resources to reactivate.   |
+-----------------------+----------+----------------------------------------------+

Reactivating an idle permit is typically expected to succeed; however,
the client might not (yet) be aware of TCM concurrently deactivating the
permit. Reactivating an inactive permit is not guaranteed to succeed as
its resources might be in use by another client. Therefore, the caller
should check the permit state and fields to ensure resource usage is
allowed.

Releasing a permit 
------------------

When the resources allocated as part of the permit are not required
anymore, the client releases the permit by calling:

.. code:: cpp

    tcm_result_t tcmReleasePermit(tcm_permit_handle_t permit_handle)

+-----------------------+--------+------------------------------------------------------------------------------------+
| Parameter             | Type   | Description                                                                        |
+=======================+========+====================================================================================+
| :code:`permit_handle` | In     | Descriptor of the resources to release back to the Thread Composability Manager.   |
+-----------------------+--------+------------------------------------------------------------------------------------+

**[WIP\_DSGN]** Setting Client Properties
-----------------------------------------

Client-specific properties can be set using the following API. The
property has an overriding effect to a particular value of the same
property specified on a more granular level such as permit or permit
request.

**TODO**: using separate functions for each possible parameter does not
scale well and is not consistent with other proposed API. Should there
be a “client properties” structure?

*[WIP\_DSGN] Setting Max Concurrency of the Client*
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code:: cpp

    tcm_result_t tcmSetClientMaxConcurrency(tcm_client_id_t client_id,
                                            uint32_t max_concurrency)

+-------------------------+--------+--------------------------------------------------------------------------------------------------------------------------------------+
| Parameter               | Type   | Description                                                                                                                          |
+=========================+========+======================================================================================================================================+
| :code:`client_id`       | In     | Descriptor of the client to set property for.                                                                                        |
+-------------------------+--------+--------------------------------------------------------------------------------------------------------------------------------------+
| :code:`max_concurrency` | In     | Value to be used as the upper bound for maximum concurrency for every permit issued on behalf of the client with :code:`client_id`.  |
+-------------------------+--------+--------------------------------------------------------------------------------------------------------------------------------------+

*[WIP\_DSGN] Setting CPU Constraints of the Client*
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code:: cpp

    tcm_result_t tcmSetClientConstraints(tcm_client_id_t client_id,
                                         tcm_cpu_contraints_t constraints)

+---------------------+--------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| Parameter           | Type   | Description                                                                                                                                                                      |
+=====================+========+==================================================================================================================================================================================+
| :code:`client_id`   | In     | Descriptor of the client to set property for.                                                                                                                                    |
+---------------------+--------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`constraints` | In     | Value to use as common CPU constraints of the client. Each permit request issued within the :code:`client_id` client becomes automatically bound to this value of constraints.   |
+---------------------+--------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+

**[WIP\_DSGN]** Querying Constraints of the Thread Composability Manager 
------------------------------------------------------------------------

There might be situations when selection of resources to include in one
or the other permit is constrained for the Thread Composability Manager.
Due to `general principle <#org2f99bf7>`__, the Thread Composability
Manager should intersect its and constraints of the client when responding
to requests of the latter.

For the client to make informed requests of permits and to know that the
Thread Composability Manager makes its decisions under certain conditions,
the client needs API to query these conditions from the Thread Composability
Manager:

.. code:: cpp

    tcm_result_t tcmQueryResourceManagerConstraints(tcm_cpu_constraints_t*
    constraints);

+---------------------+--------+---------------------------------------------------------------------------------------+
| Parameter           | Type   | Description                                                                           |
+=====================+========+=======================================================================================+
| :code:`constraints` | Out    | Constraints the Thread Composability Manager considers when responding to requests.   |
+---------------------+--------+---------------------------------------------------------------------------------------+

Data Structures of the Thread Composability Manager 
===================================================

Result of the Thread Composability Manager Function Invocation 
--------------------------------------------------------------

:code:`tcm_result_t` enum defines a set of possible return codes that the API
may use.

.. code:: cpp

    typedef enum _tcm_result_t {
      TCM_RESULT_SUCCESS = 0x0,
      TCM_RESULT_ERROR_UNKNOWN = 0x7ffffffe,
      TCM_RESULT_UNSUPPORTED = 0x6ffffffe
    } tcm_result_t;

+---------------------------------+----------------------------------------------------------------+
| Value                           | Description                                                    |
+=================================+================================================================+
| :code:`TCM_RESULT_SUCCESS`       | Indicates successful execution of the function.                |
+---------------------------------+----------------------------------------------------------------+
| :code:`TCM_RESULT_ERROR_UNKNOWN` | Indicates erroneous situation during the function execution.   |
+---------------------------------+----------------------------------------------------------------+
| :code:`TCM_RESULT_UNSUPPORTED`   | Indicates that the feature is unsupported.                     |
+---------------------------------+----------------------------------------------------------------+

Example of returning :code:`TCM_RESULT_ERROR_UNKNOWN` is passing non-existing
:code:`permit_handle` to :code:`tcmGetPermitData` function.

Examples of returning :code:`TCM_RESULT_UNSUPPORTED` are the usages of the API
with [WIP\_DSGN] or [WIP\_IMPL] statuses found in this document.

**Note**: The Thread Composability Manager follows offensive programming style,
meaning that incorrect usage of the Thread Composability Manager's API results
in undefined behavior of the system. For example, the Thread Composability
Manager does not check its input parameters and may crash or work in unexpected
manner in such cases. The debug version of the Thread Composability Manager
binary may assert incorrect API usages.

**[WIP\_DSGN]** Discuss necessity of :code:`TCM_RETURN_ERROR_UNKNOWN` in
presence of offensive programming style.

States of Permits 
------------------

The :code:`tcm_permit_state_t` structure describes various states of the
permit that the Thread Composability Manager can assign.

.. code:: cpp

    enum tcm_permit_states_t {
      TCM_PERMIT_STATE_VOID,
      TCM_PERMIT_STATE_INACTIVE,
      TCM_PERMIT_STATE_PENDING,
      TCM_PERMIT_STATE_IDLE,
      TCM_PERMIT_STATE_ACTIVE
    };

    typedef uint8_t tcm_permit_state_t;

+------------------------------------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------+
| Value                              | Description                                                                                                                                                     |
+====================================+=================================================================================================================================================================+
| :code:`TCM_PERMIT_STATE_VOID`     | No permit. Neither client owns any resources associted with the permit, nor does the Thread Composability Manager know about corresponding request existence.   |
+------------------------------------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`TCM_PERMIT_STATE_INACTIVE` | Client does not own and therefore should not use resources related to this permit.                                                                              |
+------------------------------------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`TCM_PERMIT_STATE_PENDING`  | Resources are not assigned to this permit, but will be assigned as soon as execution allows.                                                                    |
+------------------------------------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`TCM_PERMIT_STATE_IDLE`     | Resources are owned by the client, but they do not perform useful work.                                                                                         |
+------------------------------------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`TCM_PERMIT_STATE_ACTIVE`   | Resources are owned by the client, and they are used to perform useful work.                                                                                    |
+------------------------------------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------+

**[WIP\_DSGN]** Discuss necessity of :code:`TCM_PERMIT_STATE_VOID`.

Properties of Permits 
----------------------

The :code:`tcm_permit_flags_t` describes the properties of the permits.

.. code:: cpp

    typedef struct _tcm_permit_flags_t {
      bool stale : 1;
      bool rigid_concurrency : 1;
      bool exclusive : 1;
      int32_t reserved : 29;
    } tcm_permit_flags_t;

+---------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| Value                     | Description                                                                                                                                                                                                                                                            |
+===========================+========================================================================================================================================================================================================================================================================+
| :code:`stale`             | If :code:`true` indicates that the permit data is not reliable and should not be used.                                                                                                                                                                                 |
+---------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`rigid_concurrency` | If :code:`true` indicates that the permit concurrency cannot be changed while the permit is in active state.                                                                                                                                                           |
+---------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`exclusive`         | If :code:`true` indicates that the permit is in exclusive mode. Exclusive resources are not taken back by the Thread Composability Manager until the permit is released. Requests that ask for more resources than the platform has automatically becomes exclusive.   |
+---------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`reserved`          | Reserved bits for future use.                                                                                                                                                                                                                                          |
+---------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+

**[WIP\_IMPL]** Support for :code:`exclusive` mode.

Callback Type
-------------

The type of the function to pass into :code:`tcmConnect` function. This
function is called by the resource manager each time permit of the client
has been changed by the Thread Composability Manager due to API calls from
other clients or changes in other permits from the same client. It is not
called for permits, whose change is initiated by the the client itself, that
is due to calls to :code:`tcmIdlePermit`, :code:`tcmActivatePermit`,
:code:`tcmDeactivatePermit`.

The sole purpose of invoking this callback function by the resource
manager is to tell the client that the data of a particular permit has
been changed and that the client should take that into account. Client
may call :code:`tcmGetPermitData` inside callback function in order to get the
latest permit data.

.. code:: cpp

    typedef tcm_result_t (*tcm_callback_t)(tcm_permit_handle_t p, void* arg,
    tcm_callback_flags_t flags);

+---------------+------------------------------------------------------------------------------------+
| Value         | Description                                                                        |
+===============+====================================================================================+
| :code:`p`     | The unique permit handle, whose data has been changed.                             |
+---------------+------------------------------------------------------------------------------------+
| :code:`arg`   | The callback argument a client passed to the :code:`tcmRequestPermit` function.   |
+---------------+------------------------------------------------------------------------------------+
| :code:`flags` | The reasons of callback invocation.                                                |
+---------------+------------------------------------------------------------------------------------+

Callback Invocation Reasons 
----------------------------

The :code:`tcm_callbacks_flags_t` describes the reasons client callbacks
were invoked by the Thread Composability Manager.

.. code:: cpp

    typedef struct _tcm_callback_flags_t {
      bool new_concurrency : 1;
      bool new_state : 1;
      int32_t reserved : 30;
    } tcm_callback_flags_t;

+-------------------------+----------------------------------------------------------------------+
| Value                   | Description                                                          |
+=========================+======================================================================+
| :code:`new_concurrency` | If :code:`true` indicates that the permit got updated concurrency.   |
+-------------------------+----------------------------------------------------------------------+
| :code:`new_state`       | If :code:`true` indicates that the permit got updated state.         |
+-------------------------+----------------------------------------------------------------------+
| :code:`reserved`        | Reserved bits for future use.                                        |
+-------------------------+----------------------------------------------------------------------+

**[WIP\_IMPL]** Check and implement support for :code:`new_state`, if
necessary.

Permits 
--------

The :code:`tcm_permit_t` structure represents the permit data that is filled
in by the Thread Composability Manager. The client is responsible for allocating
and deallocating memory for objects of this structure, including the arrays
of necessary size.

.. code:: cpp

    typedef struct _tcm_permit_t {
      uint32_t* concurrencies;
      tcm_cpu_mask_t* cpu_masks;
      uint32_t size;
      tcm_permit_state_t state;
      tcm_permit_flags_t flags;
    } tcm_permit_t;

+-----------------------+--------------------------------------------------------------------------------------------------------------------+
| Field                 | Description                                                                                                        |
+=======================+====================================================================================================================+
| :code:`concurrencies` | The array of permitted concurrencies.                                                                              |
+-----------------------+--------------------------------------------------------------------------------------------------------------------+
| :code:`cpu_masks`     | The array of permitted masks. The array items correspond to respective items of the :code:`concurrencies` array.   |
+-----------------------+--------------------------------------------------------------------------------------------------------------------+
| :code:`size`          | The size of the arrays.                                                                                            |
+-----------------------+--------------------------------------------------------------------------------------------------------------------+
| :code:`state`         | The state of the permit.                                                                                           |
+-----------------------+--------------------------------------------------------------------------------------------------------------------+
| :code:`flags`         | The flags of the permit data.                                                                                      |
+-----------------------+--------------------------------------------------------------------------------------------------------------------+

**[WIP\_IMPL]** Support for :code:`cpu_masks` and more than one concurrency.

Constraints of Permits 
-----------------------

**[WIP\_IMPL]** Add support for constraints.

Constraints allow specifying subset of resources for the Thread Composability
Manager to make a choice from when it assigns resources as a response to
request of the client.

**Note**: The less restrictive the resource request is the more
composable execution is going to be. Therefore, the client should aim
not specifying any constraints unless absolutely necessary.

The subset of resources can be specified either using high-level
description or low-level mask. For high-level description the client
specifies values for :code:`numa_id`, :code:`core_type_id`, and :code:`threads_per_core`
struct fields. For low-level mask the client speicifies the mask field.
In case both low-level and high-level description are specified, the
Thread Composability Manager uses low-level mask.

Objects of :code:`tcm_cpu_constraints_t` type are required to be initialized
using :code:`TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER`:

.. code:: cpp

    tcm_cpu_constraints_t constraints =
    TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;

The following fields can be assigned specific value chosen by the
client, in which case the meaning is:

+---------------------------------------+---------------------------------------------------------------------------------------+
| Field                                 | Semantics of assigning a specific value                                               |
+=======================================+=======================================================================================+
| :code:`numa_id`, :code:`core_type_id` | Requesting the resources from the item with the index equal to the specified value.   |
+---------------------------------------+---------------------------------------------------------------------------------------+
| :code:`threads_per_core`              | Specifying the number of threads to use per core.                                     |
+---------------------------------------+---------------------------------------------------------------------------------------+

Besides specific values assigned by the client, these fields can be
assigned to special values. Special values are:

+------------------------+----------------------------------------------------------------------------------------------------------------------------------+
| Value                  | Description                                                                                                                      |
+========================+==================================================================================================================================+
| :code:`tcm_automatic` | The Thread Composability Manager chooses the value based on the internal heuristics and current load of the platform.            |
+------------------------+----------------------------------------------------------------------------------------------------------------------------------+
| :code:`tcm_any`       | The Thread Composability Manager chooses one specific value based on the internal heuristics and current load of the platform.   |
+------------------------+----------------------------------------------------------------------------------------------------------------------------------+

.. code:: cpp

    typedef /*implementation-defined*/ tcm_cpu_mask_t;
    typedef /*implementation-defined*/ tcm_numa_node_t;
    typedef /*implementation-defined*/ tcm_core_type_t;

    const /*implementation-defined*/ tcm_automatic =/*implementation-defined*/;
    const /*implementation-defined*/ tcm_any =/*implementation-defined*/;

    typedef struct _tcm_cpu_constraints_t {
      int32_t min_concurrency;
      int32_t max_concurrency;
      tcm_cpu_mask_t mask;
      tcm_numa_node_t numa_id;
      tcm_core_type_t core_type_id;
      int32_t threads_per_core;
    } tcm_cpu_constraints_t;

+--------------------------+-------------------------------------------------------------------------------------------------------------------+
| Field                    | Description                                                                                                       |
+==========================+===================================================================================================================+
| :code:`min_concurrency`  | Minimum value of concurrency for the described hardware subset.                                                   |
+--------------------------+-------------------------------------------------------------------------------------------------------------------+
| :code:`max_concurrency`  | Maximum value of concurrency for the described hardware subset.                                                   |
+--------------------------+-------------------------------------------------------------------------------------------------------------------+
| :code:`mask`             | The low-level mask of the resources subset. If non-NULL, then it is preferred over high-level mask description.   |
+--------------------------+-------------------------------------------------------------------------------------------------------------------+
| :code:`numa_id`          | High-level mask description. The logical index of the NUMA node to restrict the search for resources within.      |
+--------------------------+-------------------------------------------------------------------------------------------------------------------+
| :code:`core_type_id`     | High-level mask description. The logical index of the core type to restrict the search for resources within.      |
+--------------------------+-------------------------------------------------------------------------------------------------------------------+
| :code:`threads_per_core` | High-level mask description. The number of threads per core to consider while searching for resources.            |
+--------------------------+-------------------------------------------------------------------------------------------------------------------+

**Note**: The logical indices used to enumerate NUMA nodes and core
types should correspond to logical indices used by the resource manager.

Priorities of Permit Requests 
------------------------------

**[WIP\_DSGN]** Design and implement support for request priorities.

Each resources request can be assigned a priority. Prioritized requests
are handled differently by the Resource Manager than non-prioritized
ones.

.. code:: cpp

    enum tcm_request_priorities_t {
      TCM_REQUEST_PRIORITY_LOW = (INT32_MAX / 4) * 1,
      TCM_REQUEST_PRIORITY_NORMAL = (INT32_MAX / 4) * 2,
      TCM_REQUEST_PRIORITY_HIGH = (INT32_MAX / 4) * 3
    };

    typedef int32_t tcm_request_priority_t;

Permit Requests 
----------------

The :code:`tcm_permit_request_t` structure is the main data structure of the
Thread Composability Manager's API that describes the resources to be
requested from the Thread Composability Manager.

.. code:: cpp

    typedef struct _tcm_permit_request_t {
      int32_t min_sw_threads;
      int32_t max_sw_threads;
      tcm_cpu_constraints_t* cpu_constraints;
      uint32_t constraints_size;
      tcm_request_priority_t priority;
      tcm_permit_flags_t flags;
      char reserved[4];
    } tcm_permit_request_t;

+--------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| Field                    | Description                                                                                                                                                      |
+==========================+==================================================================================================================================================================+
| :code:`min_sw_threads`   | The minimum number of software threads to fulfil. If it cannot be satisfied, the Thread Composability Manager returns :code:`TCM_PERMIT_STATE_PENDING` state.   |
+--------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`max_sw_threads`   | The maximum number of software threads desired.                                                                                                                  |
+--------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`cpu_constraints`  | The array of hardware constraints, where the Thread Composability Manager should look for available resources. NULL means no constraints imposed.                |
+--------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`constraints_size` | The size of the :code:`cpu_constraints` array.                                                                                                                   |
+--------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`priority`         | The priority of the request.                                                                                                                                     |
+--------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`flags`            | The properties of the request.                                                                                                                                   |
+--------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`reserved`         | The reserved memory for future use.                                                                                                                              |
+--------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------------+

Objects of :code:`tcm_permit_request_t` type are required to be initialized
using :code:`TCM_PERMIT_REQUEST_INITIALIZER`:

.. code:: cpp

    tcm_permit_request_t request = TCM_PERMIT_REQUEST_INITIALIZER;

After initializing, at least :code:`min_sw_threads` and :code:`max_sw_threads`
values must be modified before being passed into :code:`tcmRequestPermit`
function.

**Note**: The specified values for :code:`min_sw_threads` and :code:`max_sw_threads`
in the :code:`tcm_permit_request_t` should be compatible with the
:code:`min_concurrency` and :code:`max_concurrency` values in the
:code:`tcm_cpu_constraints_t` array if the latter is specified. Otherwise,
the behaviour is undefined.

The compatibility rule:

1. The sum of minimum concurrencies specified in the constraints array
   should be less or equal to the :code:`min_sw_threads` specified in the
   request.

1. The value of :code:`min_sw_threads` should less or equal to
   :code:`max_sw_threads`.

2. The value of :code:`max_sw_threads` should less or equal to the sum of
   maximum concurrencies specified in the constraints array.

In other words, let

-  :math:`m_{i}` be the :code:`min_concurrency` values from the :code:`cpu_constraints` array

-  :math:`M_{i}` be the :code:`max_concurrency` values from the :code:`cpu_constraints` array

-  :math:`N` - the value of :code:`constraints_size` field

-  :math:`m` - the value of :code:`min_sw_threads` field

-  :math:`M` - the value of :code:`max_sw_threads` field

then the compatibility rule can be written as:

.. math:: \sum_{i = 1}^{N}m_{i} \leq m \leq M \leq \sum_{i = 1}^{N}M_{i}

Call Sequence Diagrams 
=======================

The diagrams below depict interactions between user, runtimes and the
Thread Composability Manager arranged in time sequence.

Intel(R) oneAPI Threading Building Blocks 
------------------------------------------

Component interactions for the use case with two successive calls to
:code:`tbb::task_arena::execute()` are shown in 5.1.1.

.. code:: cpp

    void foo() {
      tbb::task_arena a;
      a.initialize();
      a.execute(λ1);
      a.execute(λ2);
    }

.. image:: images/image1.png

*Figure 1: Sequence diagram of interactions for oneTBB runtime in case of two successive calls to* :code:`tbb::task_arena::execute()`.

**TODO**: include sequence diagrams in case of 1) multiple arenas with
renegotiation mechanism involved; 2) constrained execution
(:code:`tbb::global_control` and/or taskset)

Intel(R) OpenMP 
----------------

**TODO**: include sequence diagram for OpenMP in case of 1) single and
multiple parallel regions with renegotiation mechanism involved; 2)
constrained execution (environment variables and/or taskset)

Component interactions for the use case with three successive calls with
renegotiation mechanism:

.. code:: cpp

    KMP_HW_SUBSET 1s x 2n/s x 4tile/n x 2cores

    #omp parallel for num_threads(2)
    for (i = 0; i < N; ++i) {
      #omp parallel for num_threads(4)
      for (j = 0; j < M; ++j) {
        #omp parallel for num_threads(2)
        for (k = 0; k < L; ++k) {
          f(i, j, k);
        }
      }
    }

.. image:: images/image2.png

Comments for the above diagram:

I see a couple of inconsistencies to resolve:

1. The example does not say anything about unavailability of all needed resources
   before we can see that on the diagram. I think it would be better to describe
   initial conditions of this example. E.g., the values of :code:`OMP_PLACES` and
   :code:`OMP_PROC_BIND` env vars, explain in words (or pseudocode) that some of
   the resources that are going to be requested by this example are occupied already.

2. In case of OpenMP, when user specifies exact num_threads to use, the Thread
   Composability Manager cannot give less than requested even if they are not
   currently available. Therefore, I don’t think the callback would ever be called
   here and subsequent :code:`zeRegisterThread()` call, which is made as part of
   response to appearance of new resources, would also be missing.

3. I think there should be much more :code:`zeRequestPermit()` calls (and hence calls
   to :code:`zeRegisterThread()`, :code:`zeUnregisterThread()`, and
   :code:`zeReleasePermit()`) – they are nested. While executing the second loop,
   two threads that work as part of the outer loop permit, calls
   :code:`zeRequestPermit()`, and then there are 8 calls to :code:`zeRequestPermit()`
   made for the last loop invoked by the 4+4 threads working as part of the two permits
   granted for the loop in the middle.

4. The user thread participates in the execution of parallel regions, however
   corresponding calls to :code:`zeRegisterThread()` and :code:`zeUnregisterThread()`
   are missing for it.

   Also, let’s discuss it on Monday meeting with everybody.


**TODO**: Compositions of oneTBB and OpenMP 
-------------------------------------------

[WIP] Resource Distribution Strategies 
=======================================

Sequential use case 
--------------------

For sequential use case it is expected that the new permit request is
made after the previous one has been released (or made idle,
deactivated). Otherwise, it is a concurrent use case.

Therefore, any resource distribution strategy that grants all necessary
resources to the sole requestor works for this case; an example is First
Come, First Served (FCFS).

Concurrent use case 
--------------------

To handle concurrent requests the Thread Composability Manager can apply
the following strategies for the distribution of the resources:

-  Uniform distribution of resources across the requests.

-  Distribution of the resources proportional to the resources demand.

-  Distribution of the resources proportional to the parallelism of the
   algorithms.

Let's :math:`P_{1}` denotes the resources requested first in time,
:math:`P_{2}` - resources requested second in time, and :math:`P` - the
number of resources available.

Possible heuristics:

1. First Come, First Served (FCFS) - the second client gets
   :math:`min(P - P_{1},P_{2})`.

2. If :math:`P - P_{1} < P_{2}`, then the second client may start the
   renegotiation process. It does the reduction of the resources
   allotted to the first client and therefore allowing the second client
   to use that freed part. The reduction is made in the proportion of
   requested resources.

    One client gets :math:`\frac{P_{1}}{P_{1} + P_{2}}P`, the second
    gets the rest.

3. Mix of the strategies to balance the resources usage.

    For example, the request may initiate the renegotiation process only
    if the number of available resources left after the first call is
    less than minimally required.

**Note**: By specifying zero as the value of minimum concurrency in the request,
the client says that it can postpone processing of the parallel region it asks
a permission for. Specifying more than zero as the value of minimum concurrency
allows indicating to the Thread Composability Manager that the client will use
at least that value of concurrency for processing of a parallel region and that
the Thread Composability Manager should take that into account.

Nested use case 
----------------

To avoid oversubscribing the system, the Thread Composability Manager should
satisfy [nested] requests in such a way that their total granted concurrency
does not result in utilization of more resources than the system has.

The Thread Composability Manager could use different strategies for that:

1. Deny requests for oversubscribing resources.

2. Renegotiate resources usage.

Let's consider two cases:

1. The outer request is not constrained.

   The outer loop requests for maximum resources usage. Therefore, any
   additional permit on the nested levels will result in system
   oversubscription.

2. The outer request is constrained.

   Example of the code:

*Listing 2.3.2: Example of the outermost request with constraints specified.*

.. code:: cpp

    tbb::task_arena a(P1);
    a.execute([&] {
      tbb::parallel_for(0, 100, [](int) {
        /*TBB threads working*/
        #pragma omp parallel for
        for(int i = 0; i < 100; ++i) {
        /*OpenMP threads working*/
        }
      });
    });

Two possible scenarios here:

1. The user wants to constrain the execution (including the nested
   parallelism) within the limits specified.

2. The user expects that the nested level will consume unoccupied
   resources.

**Note**: The Thread Composability Manager cannot distinguish between these two
scenarios on its own. For sake of backward compatible behavior, it will likely
assume the scenario #2 by default. In order to support the scenario #1, some
form of explicit hints seems required.

Agnostic 
~~~~~~~~

The layers are not aware of each other.

*Default scenario*: user has a heavy computing task and it is more or
less parallelizable.

**Ideas to try**:

-  Split the available resources in halves between outer and inner
   levels.

-  Consider the requests on the inner level as concurrent and split its
   half uniformly among them.

Perfect nesting 
~~~~~~~~~~~~~~~~

Outer iterations spread across sockets, inner take up the whole socket
they belong to.

The outer level requests indicate there is going to be nested requests
for permits (by default nothing should be indicated explicitly). The
nested calls get permits as usual.

**Example**:

-  Outer level request: :code:`max_sw_threads = 1`, :code:`NUMA = 1`;

-  Inner level occupies free resources within the current constraint.

Perfect concurrency 
~~~~~~~~~~~~~~~~~~~~

Single resource makes several requests to split the use of the resources
between them.

Inner requests are denied since the resources are occupied already and
the outer level explicitly disabled nested requests by specified
dedicated :code:`TCM_DISABLE_NESTED_REQUESTS` flag.

Implementation considerations
=============================

Support of Nested Permit Requests 
----------------------------------

[WIP] Implementation approach 
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The idea is to make the clients use thread local storage (TLS) of
participating threads so that whenever such a thread requests resources
from the Thread Composability Manager, the latter makes `an informed
decision <#nested-support-requirement>`__.

The structure that is kept in TLS is:

.. code:: cpp

    struct thread_state {
      tcm_permit_handle_t active_permits[];
    };

The :code:`active_permits` container is empty until a thread is registered as a
participant of some resources permit.

Once a thread registers itself to work as a paricipant, the
:code:`active_permits` list is updated with the permit, which the thread has
specified when was invoking the API.

Similarly, the unregistering API removes the latest permit from the
:code:`active_permits` container.

