/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#pragma once

#include "executors.h"
#include "utils.h"
#include <tuple>
#include <vector>

class concurrent_runner {
  duration_logger& statistics;
  std::size_t iterations;

public:
  concurrent_runner(duration_logger& stat, std::size_t iters): statistics{stat}, iterations{iters} {}

  template <typename ExecContext, typename ExecTuple>
  void run_execution(ExecTuple exec_param) {
    auto& data_size = std::get<0>(exec_param);
    auto& concurrency = std::get<1>(exec_param);
    auto& functor = std::get<2>(exec_param);
    ExecContext exec_context{ concurrency };
    
    for (std::size_t i = 0; i < iterations; ++i)
      exec_context.bulk_execute(data_size, functor);
  }

  template<typename...>
  void run_impl(std::vector<std::thread>& thread_pool, std::atomic<int>& num_threads_to_wait, std::atomic<bool>& go_ahead) {
    while(num_threads_to_wait > 0)
    {}

    statistics.start();
    go_ahead = true;
    for (auto& thread : thread_pool) {
      thread.join();
    }
    statistics.stop();
  }

  template <typename C0, typename... CRest, typename ExecutionData, typename... ExecutionDataRest>
  void run_impl(std::vector<std::thread>& thread_pool, std::atomic<int>& num_threads_to_wait, std::atomic<bool>& go_ahead, 
    ExecutionData e0, ExecutionDataRest&&... args) 
  {
    thread_pool.emplace_back([&] {
        --num_threads_to_wait;
        while (!go_ahead)
        {}

        run_execution<C0>(e0);
    });

    run_impl<CRest...>(thread_pool, num_threads_to_wait, go_ahead, args...);
  }

  template<typename... Clients, typename ExecutionData, typename... ExecutionDataRest>
  void run(ExecutionData e0, ExecutionDataRest&&... e_rest) {
    std::atomic<int> num_threads_to_wait = sizeof...(e_rest) + 1;
    std::atomic<bool> go_ahead = false;
    std::vector<std::thread> data_list;
    run_impl<Clients...>(data_list, num_threads_to_wait, go_ahead, e0, e_rest...);
  }

};
