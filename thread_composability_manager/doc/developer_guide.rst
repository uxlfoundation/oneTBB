TCM Developer Guide
###################

..
   * Composition Scenarios

     * General considerations for each of the composition type: what expected to happen with threads in each composition.
   * Draw a state machine diagram.
   * Give an example of concurrent thread pool from tests.

Usage Model
***********

Below is a simple example of a usage model a parallel runtime should follow to successfully use TCM.

#. Include :code:`tcm.h` header file and link the TCM library with the project.

   .. code-block:: cpp

       #include "tcm.h"

   *Note*: If necessary, adjust project settings so that the compiler can find :code:`tcm.h` header
   file and the TCM library when building and linking the project.

#. Register a client.

   .. code-block:: cpp

       tcm_client_id_t client_id;
       tcmConnect(client_callback, &client_id);

#. Describe the resources needed using the :code:`tcm_permit_request_t` data structure.

   .. code-block:: cpp

       tcm_permit_request_t request = TCM_PERMIT_REQUEST_INITIALIZER;

   *Note*: To describe a portion of platform resources adjust the fields of
   :code:`tcm_permit_request_t` object accordingly. Refer to description of
   :ref:`tcm_permit_request_t <tcm_permit_request_t>` data structure for more info.

#. Request a resources permit.

   .. code-block:: cpp

       uint32_t concurrency{};
       tcm_permit_t permit(&concurrency);
       tcm_permit_handle_t permit_handle = nullptr;
       tcmRequestPermit(client_id, request, &permit_handle, permit_handle, &permit);

   *Note*: The :code:`tcmRequestPermit` function might result in permit switched to :code:`PENDING`
   state, meaning that the requested resources are being used by another permit, and the requesting
   side should wait until TCM is able to satisfy the permit, hence activating it and notifying the
   client through invocation of a client callback.

#. Once the permit is activated, register that number of threads that were suggested by TCM.

   .. code-block:: cpp

       uint32_t suggested_concurrency = permit.concurrencies[0];

       // Wake up suggested_concurrency number of threads and register them with the permit
       tcmRegisterThread(permit_handle); // Invoked by each participating thread

#. Deactivate and activate the permit dependending on the resources usage model.

   .. code-block:: cpp

       // Once processing block ends, deactivate the permit
       tcmDeactivatePermit(permit_handle);

       // Activate permit when processing begins again
       tcmActivatePermit(permit_handle);

   *Note*: The activation of a permit might result in permit switched to :code:`PENDING` state,
   meaning that the requested resources are being used by another permit, and the requesting side
   should wait until TCM is able to satisfy the permit, hence activating it and notifying the
   client through invocation of a client callback.

#. Unregister threads and release permit once its resources are no longer needed.

   .. code-block:: cpp

       // By each thread, which was previously registered with the permit, run
       tcmUnregisterThread();

       // Invoke once
       tcmReleasePermit(permit_handle);

#. Disconnect from TCM when resources usage is no longer planned.

   .. code-block:: cpp

       tcmDisconnect(client_id);


**Warning**: When running application that uses TCM, set :code:`TCM_ENABLE=1` environment variable
to actually enable its use.

Refer to :doc:`api_reference` to find more information on TCM usage scenarios.

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

- Requested by language RT
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
providing details on transition of CPU resources between them.

Sequential Requests
===================

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

At every moment of time the resources are meant to be used by only one parallel runtime.

Although, this represents the simplest composition scenario, it is still can benefit from using
Thread Composability Manager. This is because usually resources are not released immediately after a
parallel region, but remain being used for some time anticipating new parallel work to appear soon.
It is important to notify TCM about such situation through a call to :code:`tcmIdlePermit` so that
permit resources can be re-used by subsequent requests from another runtime.

Concurrent Requests
===================

**Use Case:** Two or more clients request resources concurrently and independently. No client makes
new requests while holding one.

Possible scenarios:

1. *Independent requests*

   Requests are not coordinated and may compete for the same resources.

Example:

*Listing 2: Independent requests happening concurrently: one client requests for :math:`P_{1}`
resources, the other - for :math:`P_{2}`.*

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

   Multiple resource requests are spread across available resources with no oversubscription. For
   example, each request is done for cores in a separate NUMA domain.

Nested Requests
===============

**Use Case:** One or more clients request resources while using the permit from one of the previous
requests.

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

Sequential with Nested
----------------------

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


Example of Usage
****************

.. code:: cpp

   #include "tcm.h"

   #include <algorithm>
   #include <functional>
   #include <future>
   #include <iostream>
   #include <thread>
   #include <vector>
   #include <deque>

   tcm_result_t renegotiation_callback(tcm_permit_handle_t permit_handle, void* arg,
                                       tcm_callback_flags_t invocation_reason);
   class client_thread_pool {
   public:
       template <typename Func>
       void parallel_for(int start, int end, const Func & f) {
           const int grant = request_permit();
           thread_pool_cv.notify_all();

           // parallel_for preparation
           const int work_size = end - start;
           const int common_size = std::max(1, work_size / grant);
           int size_remainder = std::max(0, work_size - grant * common_size);
           int s = start + common_size;

           // Submit work to workers
           std::vector<std::future<void>> task_futures;
           task_futures.reserve(grant);
           for (int id = 1; id < grant && s != end; ++id) {
               const int subsize = size_remainder-- > 0 ? common_size + 1 : common_size;
               const int e = s + subsize;
               task_futures.emplace_back(enqueue(f, s, e));
               s = e;
           }

           // External thread joins
           tcmRegisterThread(ph);
           f(start, start + common_size);
           wait(task_futures);
           tcmUnregisterThread();

           deactivate_permit();
       }

       template<typename F, typename... Args>
       std::future<void> enqueue(const F& func, Args&&... args) {
           task_t task{std::bind(func, std::forward<Args>(args)...)};
           std::future<void> future = task.get_future();
           {
               std::lock_guard<std::mutex> lock(task_deque_mutex);
               tasks.push_back(std::move(task));
           }
           task_deque_cv.notify_one();
           return future;
       }

       client_thread_pool() {
           tcm_result_t result = tcmConnect(renegotiation_callback, &client_id);
           if (result != TCM_RESULT_SUCCESS) {
               std::cerr << "tcmConnect was unsuccessful. Check 'TCM_ENABLE' is set to 1\n";
               std::abort();
           }
           initialize_thread_pool();
       }

       ~client_thread_pool() {
           {
               std::lock_guard<std::mutex> task_lock{task_deque_mutex};
               is_execution_canceled = true;
           }
           {
               std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
               max_threads = -1;
           }
           thread_pool_cv.notify_all();
           task_deque_cv.notify_all();

           tcmReleasePermit(ph);

           for (auto &worker : workers)
               worker.join();

           tcmDisconnect(client_id);
       }

   private:
       using task_t = std::packaged_task<void()>;
       void wait(std::vector<std::future<void>>& futures) {
           for (auto&& future : futures)
               future.get();
       }

       uint32_t request_permit() {
           tcm_permit_t permit{};
           uint32_t grant = 0;
           permit.concurrencies = &grant;
           permit.size = 1;

           tcm_permit_request_t request = TCM_PERMIT_REQUEST_INITIALIZER;
           request.min_sw_threads = 1; // Minimum of one thread is required to perform work
           permit_changed = false;
           tcm_result_t r = tcmRequestPermit(client_id, request, /*callback_arg*/this, &ph,
                                             &permit);
           if (r != TCM_RESULT_SUCCESS) {
               std::cerr << "tcmRequestPermit returned error status: " << r << std::endl;
               std::abort();
           }

           while (permit.state == TCM_PERMIT_STATE_PENDING || permit.flags.stale) {
               // In case of requested permit oversubscribes or stale data is read, wait for
               // notification from TCM
               std::unique_lock<std::mutex> permit_lock(permit_mutex);
               permit_cv.wait(permit_lock, [this]{ return permit_changed; });

               tcmGetPermitData(ph, &permit);
               permit_changed = false;
           }
           std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
           max_threads = grant - /*num external threads*/1;
           return grant;
       }

       void deactivate_permit() {
           tcmDeactivatePermit(ph);
           std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
           max_threads = 0;
       }

       enum pool_state {thread_exit, thread_continue, thread_join};
       pool_state try_join_thread_pool() {
           std::unique_lock<std::mutex> join_lock(thread_pool_mutex);
           thread_pool_cv.wait(join_lock,
               [this]{ return max_threads == -1 || joined_threads < max_threads; });

           if (max_threads == -1)
               return pool_state::thread_exit;
           else if (joined_threads >= max_threads)
               return pool_state::thread_continue;

           joined_threads += 1;
           return thread_join;
       }

       void exit_thread_pool() {
           std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
           joined_threads -= 1;
       }

       bool receive_task(task_t& task) {
           std::unique_lock<std::mutex> lock{task_deque_mutex};
           task_deque_cv.wait_for(lock, std::chrono::milliseconds{200},
                                  [this] { return !tasks.empty() || is_execution_canceled; });
           if (is_execution_canceled || tasks.empty()) {
               return false;
           }
           task = std::move(tasks.back());
           tasks.pop_back();
           return true;
       }

       bool need_to_leave() {
           std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
           return max_threads == -1;
       }

       void initialize_thread_pool() {
           std::call_once(thread_pool_initialized, [this] {
               auto thread_routine = [this] {
                   while (true) {
                       pool_state state = try_join_thread_pool();
                       if (state == pool_state::thread_exit)
                           return;
                       else if (state == pool_state::thread_continue)
                           continue;

                       tcmRegisterThread(ph);
                       // Task execution loop
                       while (true) {
                           task_t task;
                           if (receive_task(task))
                               task();
                           else
                               break;
                       }
                       tcmUnregisterThread();

                       exit_thread_pool();
                       if (need_to_leave()) {
                           return;
                       }
                   }
               };

               for (unsigned int i = 0; i < std::thread::hardware_concurrency() - 1; ++i)
                   workers.emplace_back(thread_routine);
           });
       }

       // Thread pool internals
       std::once_flag thread_pool_initialized;
       std::vector<std::thread> workers;
       std::mutex thread_pool_mutex;
       std::condition_variable thread_pool_cv;
       int max_threads{};
       int joined_threads{};
       bool is_execution_canceled{false};

       // Tasking internals
       std::deque<task_t> tasks;
       std::condition_variable task_deque_cv;
       std::mutex task_deque_mutex;

       // TCM related internals
       tcm_client_id_t client_id{};
       tcm_permit_handle_t ph{nullptr};
       std::condition_variable permit_cv;
       std::mutex permit_mutex;
       bool permit_changed{false};
       friend tcm_result_t renegotiation_callback(tcm_permit_handle_t permit_handle, void* arg,
                                                  tcm_callback_flags_t invocation_reason);
   };

   tcm_result_t renegotiation_callback(tcm_permit_handle_t /*ph*/, void* arg,
                                       tcm_callback_flags_t invocation_reason)
   {
       if (invocation_reason.new_state) {
           client_thread_pool& myself = *(client_thread_pool*)arg;
           {
               std::lock_guard<std::mutex> lock(myself.permit_mutex);
               myself.permit_changed = true;
           }
           myself.permit_cv.notify_one();
       }

       return TCM_RESULT_SUCCESS;
   }

   int main() {
       const int data_size = 10 * std::thread::hardware_concurrency();
       std::vector<int> data(data_size, 0);

       client_thread_pool outer;
       outer.parallel_for(0, data_size, [&data](int begin, int end) {
           client_thread_pool inner{};
           inner.parallel_for(begin, end, [&data] (int s, int e) {
               for (int i = s; i < e; ++ i)
                   data[i] += 1;
           });
       });

       bool is_valid = std::all_of(data.begin(), data.end(), [](int x){ return x == 1; });
       return is_valid ? /*success*/ 0 : /*failure*/-1;
   }
