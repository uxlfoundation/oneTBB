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
