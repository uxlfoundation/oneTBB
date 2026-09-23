/*
   Copyright (c) 2026 UXL Foundation Contributors

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#ifndef TCM_EXAMPLES_UTILS_H
#define TCM_EXAMPLES_UTILS_H

#include "tcm.h"

inline
/* begin make_permit helper */
tcm_permit_t make_permit(uint32_t& grant) {
    return tcm_permit_t{
        &grant, /*cpu_masks*/nullptr, /*size*/1, /*state*/{}, /*flags*/{}
    };
}
/* end make_permit helper */

#endif // TCM_EXAMPLES_UTILS_H
