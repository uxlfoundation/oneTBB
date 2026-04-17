/*
   Copyright (C) 2005 Intel Corporation

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

*/

#ifndef TBB_examples_num_threads_H
#define TBB_examples_num_threads_H

#include "oneapi/tbb/task_arena.h"

namespace utility {
inline int get_default_num_threads() {
    return oneapi::tbb::this_task_arena::max_concurrency();
}
} // namespace utility

#endif /* TBB_examples_num_threads_H */
