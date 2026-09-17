/*
   Copyright (c) 2026 UXL Foundation Contributors

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#include "tcm.h"
#include "utils.h"

#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

/* begin synchronization data */
struct synch_data_t {
    std::mutex permit_mutex;
    std::condition_variable permit_cv;
    bool permit_activated{false};
};
/* end synchronization data */

/* begin negotiation callback */
tcm_result_t negotiation_callback(tcm_permit_handle_t ph, void* arg,
                                  tcm_callback_flags_t /*invocation_reason*/) {
    uint32_t grant = 0;
    tcm_permit_t permit = make_permit(grant);
    tcmGetPermitData(ph, &permit);
    if (permit.flags.stale)
        // Read data is being changed, wait for another callback invocation
        return TCM_RESULT_SUCCESS;

    synch_data_t& permit_data = *(synch_data_t*)arg;
    if (TCM_PERMIT_STATE_ACTIVE == permit.state) {
        // Permit has been activated, notify parallel region that requested
        // resources are available for use
        {
            std::lock_guard<std::mutex> lock(permit_data.permit_mutex);
            permit_data.permit_activated = true;
        }
        permit_data.permit_cv.notify_one();
    }

    return TCM_RESULT_SUCCESS;
}
/* end negotiation callback */

/* begin parallel compute example */
template <typename F> void parallel_compute(int start, int end, const F& f) {
    uint32_t grant = 0;
    tcm_client_id_t my_tcm_id = 0;
    tcm_result_t result = tcmConnect(negotiation_callback, &my_tcm_id);
    if (result != TCM_RESULT_SUCCESS) {
        std::printf("Unsuccessful tcmConnect. Check TCM_ENABLE is set to 1\n");
        std::abort();
    }

    tcm_permit_request_t request = TCM_PERMIT_REQUEST_INITIALIZER;
    request.min_sw_threads = 1;
    // Non-negotiable once permit is activated
    request.flags.rigid_concurrency = 1;
    synch_data_t callback_arg;
    tcm_permit_handle_t ph{nullptr};
    tcm_permit_t permit = make_permit(grant);
    result = tcmRequestPermit(my_tcm_id, request, &callback_arg, &ph, &permit);

    // Waiting for resources permit to be activated
    while (permit.flags.stale || permit.state == TCM_PERMIT_STATE_PENDING) {
        std::unique_lock<std::mutex> lock(callback_arg.permit_mutex);
        callback_arg.permit_cv.wait(
            lock, [&callback_arg] { return callback_arg.permit_activated; }
        );
        tcmGetPermitData(ph, &permit);
        callback_arg.permit_activated = false;
    }

    const int ws = end - start;
    const int thread_ws = ws / grant;
    const uint32_t remainder = ws - thread_ws * grant;

    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < grant; ++i) {
        int end = start + thread_ws + int(i < remainder);
        threads.emplace_back([start, end, &f, ph] {
            tcmRegisterThread(ph);
            for (int j = start; j < end; ++j) f(j);
            tcmUnregisterThread();
        });
        start = end;
    }

    for(auto& t : threads)
        t.join();

    tcmReleasePermit(ph);
    tcmDisconnect(my_tcm_id);
}
/* end parallel compute example */

static std::atomic<int> external_threads{10};
void thread_func() {
    --external_threads;
    while (external_threads > 0) ;

    parallel_compute(0, 1e3, [](int) {
        parallel_compute(0, 1e3, [](int) {});
    });
}

int main() {
    std::vector<std::thread> thrs;
    const int num = external_threads;
    for (int i = 0; i < num; ++i)
        thrs.emplace_back(thread_func);

    std::vector<int> data(1e6, 0);
    parallel_compute(0, data.size(), [&data](int idx) { data[idx] += 1; });

    for (int i = 0; i < num; ++i)
        thrs[i].join();

    const bool is_valid = std::all_of(
        data.cbegin(), data.cend(), [](int i) { return i == 1; });
    return is_valid ? 0 : -1;
}
