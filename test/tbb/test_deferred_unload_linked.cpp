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

//! \file test_deferred_unload_linked.cpp
//! \brief Negative control for test_deferred_unload.cpp: the same work, but oneTBB is a
//! DT_NEEDED dependency instead of being dlopen()ed. The bug does not reproduce in this
//! configuration, but the test must still exit with status 0.

extern "C" void run_work();

int main() {
    run_work();
    return 0;
}
