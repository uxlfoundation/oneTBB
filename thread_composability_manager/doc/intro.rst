Introduction
============

Thread Composability Manager (TCM) is a project that helps different threading runtimes, such as
oneTBB and OpenMP, co-exist in one application by limiting platform oversubscription that could
occur otherwise.

Why TCM?
--------

Each threading runtime typically sizes its thread pool to the number of hardware threads available
on the platform. This works well while the runtime is the only one in the process, but modern
applications are often composed of libraries that use different runtimes, for example, an
application parallelized with oneTBB that calls into a math library parallelized with OpenMP. When
several runtimes are active at the same time, or when parallel regions of one runtime are nested
inside parallel regions of another, the total number of active threads can greatly exceed the number
of hardware threads. This situation is called *oversubscription*.

Oversubscription degrades performance: the operating system has to time-slice threads, which adds
context switching overhead, destroys cache locality, and can stall threads that hold locks or that
other threads wait for. As a result, an application composed of well-tuned parallel components may
run slower than its sequential counterpart.

Configuring each runtime independently, for example, by statically limiting the number of threads
of each runtime, does not solve the problem well. Static limits leave hardware idle when only one
runtime has work to do, and they require the application developer to know about every runtime used
inside every library.

TCM solves this by acting as a single, process-wide coordinator. Runtimes that integrate with TCM
ask it for resources before using them and give them back when they are no longer needed. TCM
dynamically distributes the available resources among the runtimes so that, together, they use the
platform fully without oversubscribing it. Application developers benefit without changing their
code, since the integration is done inside the runtimes. TCM targets general-purpose multi-core CPU
platforms, ranging from desktop systems to servers.

Key Concepts
------------

Resources
   CPU resources of the platform, that is, the hardware threads (logical processors) available to
   the process. The amount of resources is expressed in the number of software threads that can run
   simultaneously on them without oversubscribing the platform.

Client
   A threading runtime, such as oneTBB or OpenMP, or any other component that manages its own
   threads and connects to TCM to coordinate its resource usage with others.

Permit
   The object through which TCM communicates resource allocations to a client. A client requests a
   permit by describing the minimum and maximum amount of resources it needs, and TCM fills in the
   permit with the amount of resources the client is allowed to use. A permit has a state, for
   example, active, idle, inactive, or pending, which tells the client whether and how it can use
   the granted resources.

Concurrency
   The number of resources granted to a client in a permit, that is, the number of software threads
   the client is allowed to run in parallel.

Callback
   A function the client registers when connecting to TCM. TCM invokes it to notify the client that
   the permit has changed, for example, because more resources became available or because some
   resources must be returned.

Design Principles
-----------------

The purpose of the Thread Composability Manager is to distribute CPU resources between multiple
clients. The clients can request resources in arbitrary order, including nesting of the requests.
The following principles define how TCM behaves and what it expects from its clients.

#. The Thread Composability Manager (TCM) is not aware of what its clients are. It treats them
   equally, providing the interface to ask for new resources, adjust usage and release previously
   permitted resources.

#. TCM does not allocate or deallocate resources. Its sole purpose is to coordinate usage of
   resources across its clients. A client is expected to request a new portion of resources as
   demand for those appears, and to release these resources once the work is done and no new demand
   is foreseen. Threads that utilize these resources are created by the clients as needed.

   .. note:: TCM makes no assumptions about which threads – from the application or from a client’s
             thread pool – utilize the granted concurrency. Clients should adjust the concurrency
             value as needed, taking into account application threads that are going to participate
             in a parallel region.

#. It is the responsibility of the client to follow the negotiated permits. TCM assumes its clients
   are well-behaved and neither ignore nor abuse their resource permits.

#. TCM resolves resource requests in accordance with global restrictions set for the process (such
   as affinity masks). In other words, TCM respects constraints on the resources imposed on the
   application.

#. TCM provides no “independent progress” guarantee for its clients, that is whether and when a
   request for resources is satisfied depends on the resource usage by other clients.

#. TCM provides no formal fairness guarantee for its clients, though the implementation applies fair
   strategies where appropriate.

#. In case of not being able to fully satisfy the request, TCM may:

   - Reject the request, that is permitting no use of additional resources.

   - Partially satisfy the request, possibly by taking back some of the earlier permitted resources
     from previous requests and thus balancing resource usage across its clients.

   .. note:: These situations are considered normal behavior, not an error or exception.

#. In case of not being able to satisfy the requested minimum, TCM lets the client know this by
   assigning :code:`TCM_PERMIT_STATE_PENDING` state to the permit, allowing clients to wait until
   the necessary minimum becomes available.

#. Resource availability changes over time as clients request and release resources. To let clients
   follow these changes without polling, TCM rebalances resources asynchronously and notifies the
   affected clients through their registered callbacks. In particular, if unsatisfied or partially
   satisfied requests exist and unused resources appear (e.g. released by another permit), TCM
   invokes the callback of the corresponding clients to better satisfy their requests.

#. TCM may also invoke the callback to revoke some resources previously granted above the requested
   minimum, for example, to give them to another client whose minimum is not yet satisfied.

   .. note:: Since clients cannot immediately react to reduced set of resources which was initially
             negotiated, it is expected that these clients will reduce the resources usage as soon
             as execution allows. Depending on the chosen resource distribution strategy, it may
             happen that the system is oversubscribed for a limited time.

Where to Go Next
----------------

- :doc:`get_started` describes the prerequisites, how to build and install TCM, and how to enable it
  in an application.
- :doc:`developer_guide` explains the usage model a threading runtime should follow to integrate
  with TCM, the permit state transitions, and common composition scenarios. It also contains
  complete :ref:`usage examples <tcm_usage_examples>` that can serve as a quick start.
- :doc:`api_reference` provides the detailed description of TCM functions and data structures,
  including :ref:`permit requests <permit_requests>`.
