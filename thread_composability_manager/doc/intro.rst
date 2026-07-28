Introduction
============

Thread Composability Manager (TCM) is a project that helps different threading runtimes such as
oneTBB and OpenMP to co-exist by limiting platform oversubscription that could occur otherwise.

Design Principles
-----------------

The purpose of the Thread Composability Manager is to distribute CPU resources between multiple
clients. The clients can request resources in arbitrary order, including nesting of the requests.

General Principles
------------------

1. The Thread Composability Manager (TCM) is not aware what its clients are. It treats them equally
   providing the interface to ask for new resources, adjust usage and release previously permitted
   resources.

2. TCM does not allocate or deallocate resources. Its sole purpose is to coordinate resources usage
   across its clients. A client is expected to request a new portion of resources as demand for
   those appears, and to release these resources once the work is done and no new demand is
   foreseen. Threads that utilize these resources are created by the clients as needed.

   **Note:** TCM makes no assumptions about which threads – from the application or from a client’s
   thread pool – utilize the granted concurrency. Clients should adjust the concurrency value as
   needed to account for application threads that are going to participate in a parallel region.

3. It is responsibility of the client to follow the negotiated permits. TCM assumes its clients are
   well-behaved and neither ignore nor abuse their resource permits.

4. TCM resolves resource requests in accordance with global restrictions set for the process (such
   as affinity masks). In other words, TCM respects constraints on the resources imposed on the
   application.

5. TCM provides no “independent progress” guarantee for its clients, that is whether and when a
   request for resources is satisfied depends on the resource usage by other clients.

6. TCM provides no formal fairness guarantee for its clients, though the implementation applies fair
   strategies where appropriate.

7. In case of not being able to fully satisfy the request, TCM may:

   - Reject the request, that is permit no use of additional resources.

   - Partially satisfy the request, possibly by taking back some of earlier permitted resources from
     previous requests and thus balancing resource usage across its clients.

   **Note**: These situations are considered normal behaviour, not an error or exception.

8. In case of not being able to satisfy the requested minimum, TCM lets the client know this by
   assigning :code:`PENDING` state to the permit, allowing clients to wait until the necessary
   minimum becomes available.

9. If unsatisfied or partially satisfied requests exist and unused resources appear (e.g. released
   by another permit), TCM notifies the corresponding clients through a registered callback to
   better satisfy their requests.

10. TCM may also invoke the callback to revoke some resources previously granted above the requested
    minimum.

    **Note**: Since clients cannot immediately react to reduced set of resources which was initially
    negotiated, it is expected that these clients will reduce the resources usage as soon as
    execution allows. Depending on the chosen resource distribution strategy, it may happen that the
    system is oversubscribed for a limited time; TCM should however try avoiding that as much as
    possible.
