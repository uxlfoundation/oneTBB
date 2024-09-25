/*
    Copyright (C) 2023-2024 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#ifndef TCM_LIB_NAME
#error TCM_LIB_NAME must be defined to the path of the TCM library for this test to work.
#endif

#include "tcm.h"

#include "basic_test_utils.h"

#include <thread>
#include <atomic>
#include <cstdlib>              // for std::exit
#include <string>

#include <iostream>             // for std::cerr

#if __linux__
#include <dlfcn.h>              // for dlopen, dlsym
#elif _WIN32
#include <windows.h>
#else
#error Implementation of the test is not provided for this kind of OS.
#endif


typedef tcm_result_t (*tcm_connect_t)(tcm_callback_t, tcm_client_id_t*);
typedef tcm_result_t (*tcm_request_permit_t)(tcm_client_id_t, tcm_permit_request_t,
                                             void* /*callback_arg*/, tcm_permit_handle_t*,
                                             tcm_permit_t*);

tcm_connect_t tcm_connect{nullptr};
tcm_request_permit_t tcm_request_permit{nullptr};

const char* api_names[] = {"tcmConnect()", "tcmRequestPermit()"};

void load_tcm() {
#if __linux__
    void* tcm_handler = dlopen(TCM_LIB_NAME, /*flags*/ RTLD_NOW | RTLD_LOCAL);
#elif _WIN32
    HMODULE tcm_handler = LoadLibrary(TEXT(TCM_LIB_NAME));
#endif
    if (NULL == tcm_handler)
        return;

#if __linux__
    tcm_connect = (tcm_connect_t)dlsym(tcm_handler, "tcmConnect");
    tcm_request_permit = (tcm_request_permit_t)dlsym(tcm_handler, "tcmRequestPermit");
#elif _WIN32
    tcm_connect = (tcm_connect_t)GetProcAddress(tcm_handler, "tcmConnect");
    tcm_request_permit = (tcm_request_permit_t)GetProcAddress(tcm_handler, "tcmRequestPermit");
#endif
}

bool is_tcm_loaded_successfully() {
    bool load_successfully = true;

    void* api_pointers[] = {(void*)tcm_connect, (void*)tcm_request_permit};

    for (unsigned i = 0; i < sizeof(api_pointers) / sizeof(void*); ++i) {
        load_successfully &= check(api_pointers[i], std::string(api_names[i]) + " symbol was found");
    }

    return load_successfully;
}

struct test_hang_guard {
    test_hang_guard(std::atomic<bool>& release_main_thread_flag)
      : m_release_main_thread_flag(release_main_thread_flag)
    {
        if (!check(!m_release_main_thread_flag.load(std::memory_order_relaxed),
                   "The test_hang_guard correctly initialized"))
        {
            std::exit(-1);
        }
    }

    ~test_hang_guard() {
        m_release_main_thread_flag = true; // proceed with process termination
    }

    std::atomic<bool>& m_release_main_thread_flag;
};

int main() {
    std::atomic<bool> release_main_thread{false};
    bool is_test_succeeded{true};

    std::thread thr([&] {
        test_hang_guard thg(release_main_thread);

        load_tcm();
        is_test_succeeded &= check(is_tcm_loaded_successfully(), "TCM library loaded successfully");

        tcm_client_id_t id{};
        tcm_result_t r = tcm_connect(/*callback*/nullptr, &id);
        is_test_succeeded &= check_success(r, "Call to " + std::string(api_names[0]) + " succeeded");

        tcm_permit_request_t req = TCM_PERMIT_REQUEST_INITIALIZER;
        req.min_sw_threads = req.max_sw_threads = 1;
        tcm_permit_handle_t permit_handle{nullptr};
        uint32_t concurrency = 0;
        tcm_permit_t permit{
            &concurrency, /*cpu_masks*/nullptr, /*size*/1, TCM_PERMIT_STATE_VOID, /*flags*/{}
        };

        r = tcm_request_permit(id, req, /*callback_arg*/nullptr, &permit_handle, &permit);

        is_test_succeeded &= check_success(r, "Call to " + std::string(api_names[1]) + " succeeded");
        is_test_succeeded &= check(permit_handle,
                                   std::string(api_names[1]) + " returned valid permit handle");
        is_test_succeeded &= check(unsigned(req.max_sw_threads) == concurrency,
                                   "TCM distributed resources correctly");
    });

    thr.detach();

    while (!release_main_thread) { std::this_thread::yield(); }

    std::cout << "\nReleasing main thread hence terminating the test\n  while having "
        "TCM state initialized and resources distributed\n  to check if it results in error...\n";

    return is_test_succeeded ? 0 : -1;
}
