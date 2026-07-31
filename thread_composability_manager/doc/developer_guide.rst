Developer Guide
###############

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

The diagram below shows possible transitions of a permit state.

.. image:: ./resources/state_transitions.png
   :width: 400px
   :height: 200px
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

   // TODO: Adjust the thread pool and its usage in the main

   class client_thread_pool {
   public:
       template <typename Func>
       void parallel_for(int start, int end, const Func & f, const tcm_permit_t &expected_permit) {
           uint32_t concurrency;
           tcm_permit_t permit = make_void_permit(&concurrency);
           request_permit(permit, expected_permit);
           thread_pool_cv.notify_all();

           // parallel_for preparation
           int granted_concurrency = get_permit_concurrency(permit);
           int work_size = end - start;
           int base_task_size = std::max(1, work_size / granted_concurrency);
           int task_size_remainder = std::max(0, work_size - granted_concurrency * base_task_size);
           int s = start + base_task_size;

           // Submit work to workers
           std::vector<std::future<void>> task_futures;
           task_futures.reserve(granted_concurrency);
           for (int id = 1; id < granted_concurrency && s != end; ++id) {
               int task_size = task_size_remainder-- > 0 ? base_task_size + 1 : base_task_size;
               int e = s + task_size;
               task_futures.emplace_back(enqueue(f, s, e));
               s = e;
           }
           // External thread joins
           register_thread();
           f(start, start + base_task_size);
           wait(task_futures);
           unregister_thread();

           deactivate_permit();
       }

       template<typename F, typename... Args>
       std::future<void> enqueue(const F& func, Args&&... args) {
           task_t task{std::bind(func, std::forward<Args>(args)...)};
           auto future = task.get_future();
           {
               std::lock_guard<std::mutex> lock(task_deque_mutex);
               tasks.push_back(std::move(task));
           }
           task_deque_cv.notify_all();
           return future;
       }

       client_thread_pool(std::string rname, uint32_t min_threads, uint32_t max_threads)
           : runtime_name(rname), min_concurrency(min_threads), max_concurrency(max_threads)
       {
           client = connect_new_client(client_renegotiate, "", "tcmConnect " + runtime_name);
           initialize_thread_pool();
       }

       ~client_thread_pool() {
           {
               std::lock_guard<std::mutex> task_lock{task_deque_mutex};
               is_execution_canceled = true;
           }
           {
               std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
               max_joinable = -1;
           }
           thread_pool_cv.notify_all();
           task_deque_cv.notify_all();
           release_permit(ph, "", "tcmReleasePermit " + runtime_name);
           for (auto &worker : workers) {
               worker.join();
               g_num_created_threads -= 1;
           }
           disconnect_client(client, "", "tcmDisconnect " + runtime_name);
       }

   private:
       using task_t = std::packaged_task<void()>;
       void wait(std::vector<std::future<void>>& futures) {
           for (auto&& future : futures) {
               future.get();
           }
           std::lock_guard<std::mutex> lock(exception_mutex);
           if (pool_exception) {
               std::rethrow_exception(pool_exception);
           }
       }

       void request_permit(tcm_permit_t &permit, const tcm_permit_t &expected_permit) {
           tcm_permit_request_t request = TCM_PERMIT_REQUEST_INITIALIZER;
           request.min_sw_threads = min_concurrency;
           request.max_sw_threads = max_concurrency;
           auto r = tcmRequestPermit(client, request, &ph, &ph, &permit);
           if (!(check_success(r, "tcmRequestPermit " + runtime_name) && check_permit(expected_permit, permit))) {
               throw tcm_request_permit_error{};
           }
           if (permit.state == TCM_PERMIT_STATE_PENDING) {
               while (permit.state == TCM_PERMIT_STATE_PENDING) {
                   std::this_thread::yield();
                   get_permit_data(ph, permit, "", "tcmGetPermitData for ph=" + to_string(ph) + " by "
                                                   + runtime_name);
               }
           }
           std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
           max_joinable = get_permit_concurrency(permit)-1;
       }

       void deactivate_permit() {
           ::deactivate_permit(ph, "" ,"tcmDeactivatePermit " + runtime_name);
           std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
           max_joinable = 0;
       }

       void register_thread() {
           ::register_thread(ph, "", "tcmRegisterThread " + runtime_name);
       }

       void unregister_thread() {
           ::unregister_thread("", "tcmUnregisterThread " + runtime_name);
       }

       enum pool_state {thread_exit, thread_continue, thread_join};
       pool_state try_join_thread_pool() {
           std::unique_lock<std::mutex> join_lock(thread_pool_mutex);
           thread_pool_cv.wait(join_lock, [this]
                               { return max_joinable == -1 || joined_threads < max_joinable; });
           if (max_joinable == -1) {
               return pool_state::thread_exit;
           }
           if (joined_threads >= max_joinable) {
               return pool_state::thread_continue;
           }
           joined_threads += 1;
           return thread_join;
       }

       void exit_thread_pool() {
           std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
           joined_threads -= 1;
       }

       bool receive_task(task_t& task) {
           std::unique_lock<std::mutex> lock{task_deque_mutex};
           task_deque_cv.wait_for(lock, std::chrono::milliseconds{200} , [this]
                       { return !tasks.empty() || is_execution_canceled; });
           if (is_execution_canceled || tasks.empty()) {
               return false;
           }
           task = std::move(tasks.back());
           tasks.pop_back();
           return true;
       }

       bool need_to_leave() {
           std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
           return max_joinable == -1;
       }

       void initialize_thread_pool() {
           std::call_once(thread_pool_initilized, [this] {
               auto thread_routine = [this] {
                   while (true) {
                       try {
                           pool_state state = try_join_thread_pool();
                           if (state == pool_state::thread_exit) {
                               return;
                           } else if (state == pool_state::thread_continue) {
                               continue;
                           }
                           register_thread();
                           // Task execution loop
                           while (true) {
                               task_t task;
                               if (receive_task(task)) {
                                   task();
                               } else {
                                   break;
                               }
                           }
                           unregister_thread();
                           exit_thread_pool();
                           if (need_to_leave()) {
                               return;
                           }
                       }
                       catch (...) {
                           {
                               std::lock_guard<std::mutex> exception_lock(exception_mutex);
                               if (!pool_exception) {
                                   pool_exception = std::current_exception();
                               }
                           }
                           {
                               std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
                               max_joinable = -1;
                           }
                           {
                               std::lock_guard<std::mutex> task_lock{task_deque_mutex};
                               is_execution_canceled = true;
                           }
                           thread_pool_cv.notify_all();
                           task_deque_cv.notify_all();
                           return;
                       }
                   }
               };

               for (uint32_t i = 0;
                   i < max_concurrency - 1 && g_num_created_threads < g_max_threads;
                   ++i)
               {
                   if (g_num_created_threads.fetch_add(1) >= g_max_threads) {
                       g_num_created_threads -= 1;
                   }
                   workers.emplace_back(thread_routine);
               }
           });
       }
       // Auxiliary
       std::string runtime_name;
       // Details for permit request
       uint32_t min_concurrency;
       uint32_t max_concurrency;
       // Thread pool's internals
       std::once_flag thread_pool_initilized;
       std::vector<std::thread> workers;
       std::mutex thread_pool_mutex;
       std::condition_variable thread_pool_cv;
       int max_joinable{};
       int joined_threads{};
       bool is_execution_canceled{false};
       // Tasking internals
       std::deque<task_t> tasks;
       std::condition_variable task_deque_cv;
       std::mutex task_deque_mutex;
       std::mutex exception_mutex;
       std::exception_ptr pool_exception;
       // TCM related internals
       tcm_client_id_t client{};
       tcm_permit_handle_t ph{nullptr};
       static std::atomic_int g_num_created_threads;
       static constexpr int g_max_threads = 256;
   };

   std::atomic_int client_thread_pool::g_num_created_threads{0};

   int main() {
    std::string runtime_name = "outer client";
    uint32_t min_sw_threads = 1; uint32_t max_sw_threads = platform_tcm_concurrency();
    uint32_t expected_outer_concurrency = max_sw_threads;
    client_thread_pool outer{runtime_name, min_sw_threads, max_sw_threads};

    int data_size = platform_tcm_concurrency() * 10;
    std::vector<int> data(data_size, 0);

    tcm_permit_t expected_outer_permit = make_active_permit(&expected_outer_concurrency);
    outer.parallel_for(0, data_size, [&data, min_sw_threads, max_sw_threads](int begin, int end) {
      client_thread_pool inner{"inner client " + std::to_string(begin), min_sw_threads, max_sw_threads};
      uint32_t expected_inner_concurrency = 1;
      tcm_permit_t expected_inner_permit = make_active_permit(&expected_inner_concurrency);
      inner.parallel_for(begin, end, [&data] (int s, int e) {
        for (int i = s; i < e; ++ i) {
          data[i] += 1;
        }
      }, expected_inner_permit);
    }, expected_outer_permit);

    bool is_data_valid = std::all_of(data.begin(), data.end(), [](int x) { return x == 1; });
    check(is_data_valid, "Data is valid");
  }
