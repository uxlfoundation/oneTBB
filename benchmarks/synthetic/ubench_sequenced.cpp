/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#include "common/sequence.h"
#include "common/utils.h"
#include <iostream>
#include <vector>

using Type = double;

template<typename C0, typename C1, typename C2>
int run_composition(duration_logger& stat, const std::string& chain_name, std::size_t data_size, C0& c0, C1& c1, C2& c2) {
  constexpr int repetitions = 100;

  // input data creation
  std::vector<Type> init_data(3 * data_size, 1);
  Type* a0 = init_data.data();
  Type* a1 = a0 + data_size;
  Type* a2 = a1 + data_size;

  stat.iterations = repetitions;
  stat.start();
  for(int i = 0; i < repetitions; i++){
    sequenced(std::make_tuple(data_size, c0, [a0](int i) { a0[i] = a0[i] * a0[i]; }),
              std::make_tuple(data_size, c1, [a1](int i) { a1[i] = a1[i] * a1[i]; }),
              std::make_tuple(data_size, c2, [a2](int i) { a2[i] = a2[i] * a2[i]; })
              );
  }
  stat.stop();

  int result = check_sequence(a0, data_size, /*expected*/1, "sequence 1")
            || check_sequence(a1, data_size, /*expected*/1, "sequence 2")
            || check_sequence(a2, data_size, /*expected*/1, "sequence 3");

  if (!result) {
    stat.dump(chain_name);
  }

  return result;
}

template <typename ExecContext1, typename ExecContext2>
int
run_context_chains(duration_logger& stat, std::size_t data_size, ExecContext1&& context1, ExecContext2&& context2)
{
  int result =
       run_composition(stat, 
        context1.name() + "-" + context1.name() + "-" + context1.name(), data_size, context1, context1, context1)
    || run_composition(stat, 
        context1.name() + "-" + context1.name() + "-" + context2.name(), data_size, context1, context1, context2)
    || run_composition(stat, 
        context1.name() + "-" + context2.name() + "-" + context1.name(), data_size, context1, context2, context1)
    || run_composition(stat, 
        context1.name() + "-" + context2.name() + "-" + context2.name(), data_size, context1, context2, context2)
    || run_composition(stat, 
        context2.name() + "-" + context1.name() + "-" + context1.name(), data_size, context2, context1, context1)
    || run_composition(stat, 
        context2.name() + "-" + context1.name() + "-" + context2.name(), data_size, context2, context1, context2)
    || run_composition(stat, 
        context2.name() + "-" + context2.name() + "-" + context1.name(), data_size, context2, context2, context1)
    || run_composition(stat, 
        context2.name() + "-" + context2.name() + "-" + context2.name(), data_size, context2, context2, context2);

  return result;
}

int main(int argc, char** argv) {
  argument_parser parser{argc, argv};
  
  std::vector<std::size_t> sizes{10000, 100000, 1000000};
  if (parser.work_sizes.empty() == false) {
    sizes = parser.work_sizes;
  }

  const std::size_t hw_concurrency = std::thread::hardware_concurrency();
  std::vector<std::size_t> concurrency = {
    1                 ,
    hw_concurrency / 2,
    hw_concurrency    ,
    2 * hw_concurrency
  };

  int result = 0;
  duration_logger statistics{"sequenced", hw_concurrency, parser.print_to_stdout};
  for (auto& v_concurrency : concurrency) {
    statistics.max_concurrency = v_concurrency;

    for (auto& v_size : sizes)
    {
      statistics.data_size = v_size;

      tcm::omp_client oec{v_concurrency};
      tcm::tbb_client tec{v_concurrency};
      result = result || run_context_chains(statistics, v_size, oec, tec);
    }
  }

  return result;
}
