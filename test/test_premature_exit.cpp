/*
    Copyright (c) 2021-2023 Intel Corporation
*/

#ifndef TCM_LIB_PATH
#error TCM_LIB_PATH must be defined to the path of the TCM library for this test to work.
#endif

#include <thread>
#include <atomic>
#include <cstdlib>              // for std::exit

#include <iostream>             // for std::cerr

#if __linux__
#include <dlfcn.h>              // for dlopen, dlsym
#elif _WIN32
#include <windows.h>
#else
#error Implementation of the test is not provided for this kind of OS.
#endif

#include "tcm.h"


typedef tcm_result_t (*tcm_connect_t)(tcm_callback_t, tcm_client_id_t*);
typedef tcm_result_t (*tcm_request_permit_t)(tcm_client_id_t, tcm_permit_request_t,
                                             void* /*callback_arg*/, tcm_permit_handle_t*,
                                             tcm_permit_t*);

tcm_connect_t tcm_connect{nullptr};
tcm_request_permit_t tcm_request_permit{nullptr};

void load_tcm() {
#if __linux__
    void* tcm_handler = dlopen(TCM_LIB_PATH, /*flags*/ RTLD_NOW | RTLD_LOCAL);
#elif _WIN32
    HMODULE tcm_handler = LoadLibrary(TEXT(TCM_LIB_PATH));
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

bool is_tcm_load_failed() {
    bool load_failed = false;

    const char* api_names[] = {"tcmConnect()", "tcmRequestPermit()"};
    void* api_pointers[] = {(void*)tcm_connect, (void*)tcm_request_permit};

    for (unsigned i = 0; i < sizeof(api_pointers) / sizeof(void*); ++i) {
        if ( !api_pointers[i] ) {
            // TODO: reuse 'check' test facilities to check and report errors
            std::cerr << "******** ERROR ********: " << api_names[i] << " symbol was not found."
                      << std::endl;
            load_failed = true;
        }
    }

    return load_failed;
}

struct test_hang_guard {
    test_hang_guard(std::atomic<bool>& release_main_thread_flag)
      : m_release_main_thread_flag(release_main_thread_flag)
    {
        if (m_release_main_thread_flag.load(std::memory_order_relaxed)) {
            std::cerr << "******** ERROR ********: The test_hang_guard guards nothing" << std::endl;
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
    bool is_test_failed{false};

    std::thread thr([&] {
        test_hang_guard thg(release_main_thread);

        load_tcm();
        if (is_tcm_load_failed()) {
            is_test_failed = true;
            return;
        }

        tcm_client_id_t id{};
        tcm_result_t r = tcm_connect(/*callback*/nullptr, &id);
        if (r != TCM_RESULT_SUCCESS) {
            std::cerr << "******** ERROR ********: call to tcmConnect() failed." << std::endl;
            is_test_failed = true;
            return;
        }

        tcm_permit_request_t req = TCM_PERMIT_REQUEST_INITIALIZER;
        req.min_sw_threads = 0;
        req.max_sw_threads = 1;
        tcm_permit_handle_t permit_handle{nullptr};
        uint32_t concurrency = 0;
        tcm_permit_t permit{
            &concurrency, /*cpu_masks*/nullptr, /*size*/1, TCM_PERMIT_STATE_VOID, /*flags*/{}
        };
        r = tcm_request_permit(id, req, /*callback_arg*/nullptr, &permit_handle, &permit);
        if (r != TCM_RESULT_SUCCESS || !permit_handle) {
            std::cerr << "******** ERROR ********: call to tcmRequestPermit() failed." << std::endl;
            is_test_failed = true;
            return;
        }
    });

    thr.detach();

    while (!release_main_thread) { std::this_thread::yield(); }

    return int(is_test_failed);
}
