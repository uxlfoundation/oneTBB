/*
   Copyright (C) 2023 Intel Corporation

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

*/
#pragma once

#include "executors.h"
#include <tuple>

namespace detail {
template <typename Client>
  struct function_apply_wrapper {
      template<typename... Args>
      Client operator()(Args&&... args) {
          return Client::make_client(args...);
      }
  };
}

template<typename F, typename IndicesTuple>
void nested_impl(F&& f, IndicesTuple inds) {
  std::apply(std::forward<F>(f), inds);
}

template<typename Client0, typename... ClientRest,
  typename Functor, typename IndicesTuple, typename ExecutionData, typename ... ExecutionDataRest>
void nested_impl(Functor&& f, IndicesTuple idx, ExecutionData exec, ExecutionDataRest... rest) {
    auto func = [&](int i) {
      nested_impl<ClientRest...>(std::forward<Functor>(f), std::tuple_cat(idx, std::make_tuple(i)), rest...);
    };

    auto& data_size = std::get<0>(exec);
    auto& arguments = std::get<1>(exec);
    auto client = std::apply(detail::function_apply_wrapper<Client0>{}, arguments);

    client.bulk_execute(data_size, func);
}

template<typename... Clients, typename F, typename... ExecutionData>
void nested(F&& f, ExecutionData... exec_data) {
  std::tuple<> t;
  nested_impl<Clients...>(std::forward<F>(f), t, exec_data...);
}
