/*
    Copyright (C) 2005-2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
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
