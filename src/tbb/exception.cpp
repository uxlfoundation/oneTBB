/*
    Copyright (c) 2005-2023 Intel Corporation

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

#include "oneapi/tbb/detail/_exception.h"
#include "oneapi/tbb/detail/_assert.h"
#include "oneapi/tbb/detail/_template_helpers.h"

#include <cstring>
#include <cstdio>
#include <stdexcept> // std::runtime_error
#include <new>
#include <stdexcept>
#include <cstdarg>

#define __TBB_STD_RETHROW_EXCEPTION_POSSIBLY_BROKEN                             \
    (__GLIBCXX__ && __TBB_GLIBCXX_VERSION>=40700 && __TBB_GLIBCXX_VERSION<60000 && TBB_USE_EXCEPTIONS)

#if __TBB_STD_RETHROW_EXCEPTION_POSSIBLY_BROKEN
// GCC ABI declarations necessary for a workaround
#include <cxxabi.h>
#endif

namespace tbb {
namespace detail {
namespace r1 {

const char* bad_last_alloc::what() const noexcept(true) { return "bad allocation in previous or concurrent attempt"; }
const char* user_abort::what() const noexcept(true) { return "User-initiated abort has terminated this operation"; }
const char* missing_wait::what() const noexcept(true) { return "wait() was not called on the structured_task_group"; }


/*[[noreturn]]*/ void print_error_and_terminate(const char* format, ...)
{
    va_list vl;
    va_start(vl,format);
    std::vfprintf(stderr, format, vl);
    va_end(vl);
    std::fflush(stderr);
    std::terminate();
}

#if TBB_USE_EXCEPTIONS
    bool terminate_on_exception(); // defined in global_control.cpp and ipc_server.cpp

    static void do_terminate_on_exception(const char* ex)
    {
        print_error_and_terminate("'terminate_on_exception' enabled when throwing %s. Aborting.\n", ex);
    }
    static void do_terminate_on_exception(const char* ex, const char* msg)
    {
        print_error_and_terminate("'terminate_on_exception' enabled when throwing %s(%s). Aborting.\n", ex, msg);
    }

#   define DO_THROW(ex, ...) \
        if (terminate_on_exception()) { \
            do_terminate_on_exception(#ex, ##__VA_ARGS__); \
        } \
        throw ex(__VA_ARGS__)
#else /* !TBB_USE_EXCEPTIONS */
    static void do_exceptions_disabled_error(const char* ex)
    {
        print_error_and_terminate("Exception %s would have been thrown but exceptions are disabled.\n", ex);
    }
    static void do_exceptions_disabled_error(const char* ex, const char* msg)
    {
        print_error_and_terminate("Exception %s(%s) would have been thrown but exceptions are disabled.\n", ex, msg);
    }
#   define DO_THROW(ex, ...) do_exceptions_disabled_error(#ex, ##__VA_ARGS__)
#endif /* !TBB_USE_EXCEPTIONS */

void throw_exception ( exception_id eid ) {
    switch ( eid ) {
    case exception_id::bad_alloc: DO_THROW(std::bad_alloc); break;
    case exception_id::bad_last_alloc: DO_THROW(bad_last_alloc); break;
    case exception_id::user_abort: DO_THROW(user_abort); break;
    case exception_id::nonpositive_step: DO_THROW(std::invalid_argument, "Step must be positive"); break;
    case exception_id::out_of_range: DO_THROW(std::out_of_range, "Index out of requested size range"); break;
    case exception_id::reservation_length_error: DO_THROW(std::length_error, "Attempt to exceed implementation defined length limits"); break;
    case exception_id::missing_wait: DO_THROW(missing_wait); break;
    case exception_id::invalid_load_factor: DO_THROW(std::out_of_range, "Invalid hash load factor"); break;
    case exception_id::invalid_key: DO_THROW(std::out_of_range, "invalid key"); break;
    case exception_id::bad_tagged_msg_cast: DO_THROW(std::runtime_error, "Illegal tagged_msg cast"); break;
    case exception_id::unsafe_wait: DO_THROW(unsafe_wait, "Unsafe to wait further"); break;
    default: __TBB_ASSERT ( false, "Unknown exception ID" );
    }
    __TBB_ASSERT(false, "Unreachable code");
}

/* The "what" should be fairly short, not more than about 128 characters.
   Because we control all the call sites to handle_perror, it is pointless
   to bullet-proof it for very long strings.

   Design note: ADR put this routine off to the side in tbb_misc.cpp instead of
   Task.cpp because the throw generates a pathetic lot of code, and ADR wanted
   this large chunk of code to be placed on a cold page. */
void handle_perror( int error_code, const char* what ) {
    const int BUF_SIZE = 255;
    char buf[BUF_SIZE + 1] = { 0 };
    std::strncat(buf, what, BUF_SIZE);
    std::size_t buf_len = std::strlen(buf);
    if (error_code) {
        std::strncat(buf, ": ", BUF_SIZE - buf_len);
        buf_len = std::strlen(buf);
        std::strncat(buf, std::strerror(error_code), BUF_SIZE - buf_len);
        buf_len = std::strlen(buf);
    }
    __TBB_ASSERT(buf_len <= BUF_SIZE && buf[buf_len] == 0, nullptr);
    DO_THROW(std::runtime_error, buf);
}

#if __TBB_STD_RETHROW_EXCEPTION_POSSIBLY_BROKEN
// Runtime detection and workaround for the GCC bug 62258.
// The problem is that std::rethrow_exception() does not increment a counter
// of active exceptions, causing std::uncaught_exception() to return a wrong value.
// The code is created after, and roughly reflects, the workaround
// at https://gcc.gnu.org/bugzilla/attachment.cgi?id=34683

void fix_broken_rethrow() {
    struct gcc_eh_data {
        void *       caughtExceptions;
        unsigned int uncaughtExceptions;
    };
    gcc_eh_data* eh_data = punned_cast<gcc_eh_data*>( abi::__cxa_get_globals() );
    ++eh_data->uncaughtExceptions;
}

bool gcc_rethrow_exception_broken() {
    bool is_broken;
    __TBB_ASSERT( !std::uncaught_exception(),
        "gcc_rethrow_exception_broken() must not be called when an exception is active" );
    try {
        // Throw, catch, and rethrow an exception
        try {
            throw __TBB_GLIBCXX_VERSION;
        } catch(...) {
            std::rethrow_exception( std::current_exception() );
        }
    } catch(...) {
        // Check the bug presence
        is_broken = std::uncaught_exception();
    }
    if( is_broken ) fix_broken_rethrow();
    __TBB_ASSERT( !std::uncaught_exception(), nullptr);
    return is_broken;
}
#else
void fix_broken_rethrow() {}
bool gcc_rethrow_exception_broken() { return false; }
#endif /* __TBB_STD_RETHROW_EXCEPTION_POSSIBLY_BROKEN */

} // namespace r1
} // namespace detail
} // namespace tbb

