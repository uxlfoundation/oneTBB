/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#include "common/concurrent.h"
#include <vector>

using Type = double;

template<typename C0, typename C1, typename C2> 
int run_composition(duration_logger& stat, const std::string& chain_name, std::size_t concurrency, std::size_t data_size) {
  const int repetitions = 100;

  // input data creation
  std::vector<Type> init_data(3 * data_size, 1);
  Type* a0 = init_data.data();
  Type* a1 = a0 + data_size;
  Type* a2 = a1 + data_size;

  stat.iterations = repetitions;
  concurrent_runner exec(stat, repetitions);
  exec.run<C0, C1, C2>(std::make_tuple(data_size, concurrency, [a0](int i) { a0[i] = a0[i] * a0[i];}),
           std::make_tuple(data_size, concurrency, [a1](int i) { a1[i] = a1[i] * a1[i];}),
           std::make_tuple(data_size, concurrency, [a2](int i) { a2[i] = a2[i] * a2[i];})
           );

  int result = check_sequence(a0, data_size, /*expected*/1, "sequence 1")
            || check_sequence(a1, data_size, /*expected*/1, "sequence 2")
            || check_sequence(a2, data_size, /*expected*/1, "sequence 3");

  if (!result) {
    stat.dump(chain_name);
  }

  return result;
}

template <typename Client1, typename Client2>
int run_context_chains(duration_logger& stat, std::size_t concurrency, std::size_t data_size) {
  int result =
       run_composition<Client1, Client1, Client1>(stat, 
        Client1::name() + "-" + Client1::name() + "-" + Client1::name(), concurrency, data_size)

    || run_composition<Client1, Client1, Client2>(stat, 
        Client1::name() + "-" + Client1::name() + "-" + Client2::name(), concurrency, data_size)

    || run_composition<Client1, Client2, Client1>(stat, 
        Client1::name() + "-" + Client2::name() + "-" + Client1::name(), concurrency, data_size)

    || run_composition<Client1, Client2, Client2>(stat, 
        Client1::name() + "-" + Client2::name() + "-" + Client2::name(), concurrency, data_size)

    || run_composition<Client2, Client1, Client1>(stat, 
        Client2::name() + "-" + Client1::name() + "-" + Client1::name(), concurrency, data_size)

    || run_composition<Client2, Client1, Client2>(stat, 
        Client2::name() + "-" + Client1::name() + "-" + Client2::name(), concurrency, data_size)

    || run_composition<Client2, Client2, Client1>(stat, 
        Client2::name() + "-" + Client2::name() + "-" + Client1::name(), concurrency, data_size)

    || run_composition<Client2, Client2, Client2>(stat, 
        Client2::name() + "-" + Client2::name() + "-" + Client2::name(), concurrency, data_size);

  return result;
}



int main(int argc, char** argv) {
  argument_parser parser{argc, argv};

  std::vector<std::size_t> sizes{10000, 100000};
  if (parser.work_sizes.empty() == false) {
    sizes = parser.work_sizes;
  }

  std::size_t hw_concurrency = std::thread::hardware_concurrency();
  std::vector<std::size_t> concurrency {
    1,
    hw_concurrency / 3,
    hw_concurrency / 2,
    hw_concurrency
  };

  int result = 0;
  duration_logger statistics("concurrent", hw_concurrency, parser.print_to_stdout);
  for (auto& v_concurrency : concurrency) {
    statistics.max_concurrency = v_concurrency;
    for (std::size_t& v_size : sizes) {
      statistics.data_size = v_size;

      result = 
        result || run_context_chains<tcm::omp_client, tcm::tbb_client>(statistics, v_concurrency, v_size);
    }
  }

  return result;
}
