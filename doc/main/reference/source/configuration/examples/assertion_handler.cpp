/*
    Copyright (c) 2025 Intel Corporation
    Copyright (c) 2025 UXL Foundation Contributors

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

/*begin_assertion_handler_example*/
#include <oneapi/tbb/global_control.h>
#include <fstream>

auto default_handler = tbb::get_assertion_handler();

void wrapper_assertion_handler(const char* location, int line,
                               const char* expression, const char* comment) {
    // Execute a custom step before the default handler: log the assertion
    std::ofstream log{"example.log", std::ios::app};
    log << "Function: " << location << ", line: " << line << ", assertion "
        << expression << " failed: " << comment << "\n";
    log.close();

    default_handler(location, line, expression, comment);
}

int main() {
    // Set custom handler
    tbb::set_assertion_handler(wrapper_assertion_handler);

    // Use oneTBB normally - any assertion failures will use custom handler
    // ...

    // Restore the default handler
    tbb::set_assertion_handler(nullptr);
}
/*end_assertion_handler_example*/
