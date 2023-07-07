/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#include "common/nested.h"
#include "common/executors.h"
#include "utils.h"

using Type = double;
using Concurrency = std::pair<std::size_t, std::size_t>;

template<typename C0, typename C1, typename C0Args, typename C1Args>
int run_composition(duration_logger& stat, const std::string& chain_name, 
  std::size_t data_size, C0Args&& c0_args, C1Args&& c1_args) 
{
  const int repetitions = 100;
  stat.iterations = repetitions;

  // input data creation
  std::vector<Type> input(data_size * data_size, 1);
  Type* a = input.data();
  stat.start();
  for(int i = 0; i < repetitions; i++) {
    nested<C0, C1>([a, data_size](int i , int j) {
        int offset = i * data_size + j;
        a[offset] = a[offset] * a[offset];
      },
      std::make_tuple(data_size, std::forward<C0Args>(c0_args)),
      std::make_tuple(data_size, std::forward<C1Args>(c1_args))
    );
  }
  stat.stop();

  int check = check_sequence(a, data_size * data_size, /*expected*/1, "sequence 1");

  if (!check)
    stat.dump(chain_name);

  return check;
}

template <typename Client1, typename Client2, typename ArgsTuple1, typename ArgsTuple2>
int run_context_chains(duration_logger& stat, std::size_t data_size, ArgsTuple1&& args1, ArgsTuple2&& args2)
{
  int result =
       run_composition<Client1, Client1>(stat, Client1::name() + "-" + Client1::name(),
        data_size, std::forward<ArgsTuple1>(args1), std::forward<ArgsTuple2>(args2)) ||

       run_composition<Client1, Client2>(stat, Client1::name() + "-" + Client2::name(),
        data_size, std::forward<ArgsTuple1>(args1), std::forward<ArgsTuple2>(args2)) ||

       run_composition<Client2, Client1>(stat, Client2::name() + "-" + Client1::name(),
        data_size, std::forward<ArgsTuple1>(args1), std::forward<ArgsTuple2>(args2)) ||

      run_composition<Client2, Client2>(stat, Client2::name() + "-" + Client2::name(),
        data_size, std::forward<ArgsTuple1>(args1), std::forward<ArgsTuple2>(args2));
  return result;
}

int main(int argc, char** argv) {
  argument_parser parser{argc, argv};

  std::vector<std::size_t> sizes{10, 100, 1000};  /*dimension of a square matrix N x N*/
  if (parser.work_sizes.empty() == false) {
    sizes = parser.work_sizes;
  }

  omp_set_max_active_levels(3);  /*enable nesting for openMP parallel sections up to 3 levels*/
  const std::size_t hw_concurrency = std::thread::hardware_concurrency();
  std::vector<Concurrency> concurrency = { 
    /*{ client1 concurrency, client2 concurrency }*/
      { hw_concurrency / 4, hw_concurrency       },
      { hw_concurrency / 2, hw_concurrency       },
      { hw_concurrency,     hw_concurrency / 2   },
      { hw_concurrency,     hw_concurrency / 4   },
      { hw_concurrency,     hw_concurrency       }
  };

  int result = 0;
  duration_logger statistics("nested", hw_concurrency, parser.print_to_stdout);

  for (auto& v_concurrency : concurrency) {
    statistics.max_concurrency = v_concurrency.first + v_concurrency.second;
    statistics.concurrency_chain = std::to_string(v_concurrency.first) + "-" + std::to_string(v_concurrency.second);

    for (auto& v_size : sizes) {
      statistics.data_size = v_size;
      result =
        run_context_chains<tcm::tbb_client, tcm::omp_client>(statistics, v_size, 
          std::make_tuple(v_concurrency.first), std::make_tuple(v_concurrency.second)) ||

        run_composition<tcm::omp_client, tcm::serial_client>(statistics, 
          tcm::omp_client::name() + "-" + tcm::serial_client::name(), 
          v_size, std::make_tuple(v_concurrency.first), std::tuple<>{}) ||

        run_composition<tcm::tbb_client, tcm::serial_client>(statistics, 
          tcm::tbb_client::name() + "-" + tcm::serial_client::name(),
          v_size, std::make_tuple(v_concurrency.first), std::tuple<>{});
    }
  }

  return result;
}
