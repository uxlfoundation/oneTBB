/*
   Copyright (C) 2023 Intel Corporation

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

*/

#pragma once

#include "executors.h"
#include <tuple>

void sequenced() {
}

template<typename ExecutionData, typename... ExecutionDataRest>
void sequenced(ExecutionData&& e0, ExecutionDataRest&&... e_rest) {
  auto& data_size = std::get<0>(e0);
  auto& client = std::get<1>(e0);
  auto& functor = std::get<2>(e0);
  client.bulk_execute(data_size, functor);

  sequenced(std::forward<ExecutionDataRest>(e_rest)...);
}
