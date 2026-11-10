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

//! \file test_windows_dllmain_unload_linked.cpp
//! \brief Negative control for test_windows_dllmain_unload.cpp.
//!
//! oneTBB is a load-time dependency here, rather than being loaded and explicitly unloaded by a
//! host. The DllMain unload regression does not reproduce in this configuration, but the test must
//! still exit with status 0.

#define __TBB_NO_IMPLICIT_LINKAGE 1

extern "C" int run_work();

int main() {
    return run_work();
}
