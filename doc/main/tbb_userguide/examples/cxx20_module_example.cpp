/*
    Copyright (c) 2026 UXL Foundation Contributors

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/* begin_cxx20_modules_macro_example */
// It is assumed that the application is compiled with preview macros predefined
#include <oneapi/tbb/version.h>  // macros available here
import tbb;

static_assert(TBB_VERSION_MAJOR >= 2023, "Major version 2023 or later is required");

// Feature-test macros are also available
#ifdef TBB_HAS_FEATURE_X
// use feature X
#endif
/* end_cxx20_modules_macro_example */

/* begin_cxx20_modules_basic_example */
int main() {
    tbb::parallel_for(0, 100, [](int i) { /* ... */ });
}
/* end_cxx20_modules_basic_example */
