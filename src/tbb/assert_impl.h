/*
    Copyright (c) 2005-2025 Intel Corporation
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

#ifndef __TBB_assert_impl_H
#define __TBB_assert_impl_H

#include "oneapi/tbb/detail/_config.h"
#include "oneapi/tbb/detail/_utils.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#if _MSC_VER && _DEBUG
#include <crtdbg.h>
#endif
#if (__TBB_BUILD || __TBBBIND_BUILD) // only TBB and TBBBind use custom handler
#include <exception>
#endif

#include <mutex>

#if __TBBMALLOC_BUILD
namespace rml { namespace internal {
#else
namespace tbb {
namespace detail {
namespace r1 {
#endif

// Do not move the definition into the assertion_failure_impl function because it will require
// "magic statics". It will bring a dependency on C++ runtime on some platforms while assert_impl.h
// is reused in tbbmalloc that should not depend on C++ runtime. For the same reason, we cannot use
// std::call_once here.
static std::atomic<tbb::detail::do_once_state> assertion_state;

// TODO: consider extension for formatted error description string
/* [[noreturn]] */ static void assertion_failure_default(const char* location, int line,
                                                         const char* expression, const char* comment) {
#if __TBB_MSVC_UNREACHABLE_CODE_IGNORED
    // Workaround for erroneous "unreachable code" during assertion throwing using call_once
    #pragma warning (push)
    #pragma warning (disable: 4702)
#endif
    atomic_do_once([&](){
        std::fprintf(stderr, "Assertion %s failed (located in the %s function, line in file: %d)\n",
            expression, location, line);

        if (comment) {
            std::fprintf(stderr, "Detailed description: %s\n", comment);
        }
#if _MSC_VER && _DEBUG
        if (1 == _CrtDbgReport(_CRT_ASSERT, location, line, "tbb_debug.dll", "%s\r\n%s",
                               expression, comment?comment:"")) {
            _CrtDbgBreak();
        } else
#endif
        {
            std::fflush(stderr);
#if (__TBB_BUILD || __TBBBIND_BUILD) // only TBB and TBBBind use custom handler
            std::terminate();
#else
            std::abort();
#endif
        }
    }, assertion_state);
#if __TBB_MSVC_UNREACHABLE_CODE_IGNORED
    #pragma warning (pop)
#endif
}

namespace assertion_handler {
// Initial value is default handler
static std::atomic<assertion_handler_type> handler{nullptr};

#if (__TBB_BUILD || __TBBBIND_BUILD) // only TBB and TBBBind use custom handler
static assertion_handler_type set(assertion_handler_type new_handler) noexcept {
    return handler.exchange(new_handler ? new_handler : nullptr,
                            std::memory_order_acq_rel);
}
#endif

static assertion_handler_type get() noexcept {
    return handler.load(std::memory_order_acquire);
}

} // namespace assertion_handler

void terminate_on_user_exception() {
    assertion_handler_type curr_handler = assertion_handler::get();

    // default "exception in noexcept function" handler can report exception name
    // for any exception, so use it if one is not redefined
    if (!curr_handler)
        do_throw_noexcept([] { throw; });

    char buf[256] = { 0 };

    try {
        throw;
    } catch (std::exception &x) {
        std::snprintf(buf, sizeof(buf), "Terminating due to exception: %s with arguments: %s",
                      typeid(x).name(), x.what());
    } catch (...) {
        std::strncat(buf, "Unknown exception", sizeof(buf)-1);
    }
    __TBB_ASSERT_RELEASE(false, buf);
}

void __TBB_EXPORTED_FUNC assertion_failure(const char* location, int line,
                                           const char* expression, const char* comment) {
    assertion_handler_type curr_handler = assertion_handler::get();

    (curr_handler ? curr_handler : assertion_failure_default) (location, line, expression, comment);
}

//! Report a runtime warning.
void runtime_warning( const char* format, ... ) {
    char str[1024]; std::memset(str, 0, 1024);
    va_list args; va_start(args, format);
    vsnprintf( str, 1024-1, format, args);
    va_end(args);
    fprintf(stderr, "TBB Warning: %s\n", str);
}

#if __TBBMALLOC_BUILD
}} // namespaces rml::internal
#else
} // namespace r1
} // namespace detail
} // namespace tbb
#endif

#endif // __TBB_assert_impl_H

