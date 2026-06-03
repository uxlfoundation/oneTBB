/*
   Copyright (c) 2024 Intel Corporation
   Copyright (c) 2026 UXL Foundation Contributors

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#ifndef __TCM_TESTS_BASIC_TEST_UTILS_HEADER
#define __TCM_TESTS_BASIC_TEST_UTILS_HEADER

/***************************************************************************************************
 * This file contains helper functions for testing infrastructure that depend only on standard C++
 * and OS API
 **************************************************************************************************/

#include "tcm/detail/_tcm_assert.h"

#include "tcm.h"

#include "test_exceptions.h"

#include <algorithm>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <ostream>
#include <string>
#include <sstream>
#include <thread>
#include <vector>

// MSVC Warning: Args can be incorrect: this does not match function name specification
__TCM_SUPPRESS_WARNING_WITH_PUSH(6387)
inline int SetEnv( const char *envname, const char *envval ) {
    __TCM_ASSERT( (envname && envval), "SetEnv requires two valid C strings" );
#if !(_MSC_VER || __MINGW32__ || __MINGW64__)
    // On POSIX systems use setenv
    return setenv(envname, envval, /*overwrite=*/1);
#elif __STDC_SECURE_LIB__>=200411
    // this macro is set in VC & MinGW if secure API functions are present
    return _putenv_s(envname, envval);
#else
    // If no secure API on Windows, use _putenv
    size_t namelen = strlen(envname), valuelen = strlen(envval);
    char* buf = new char[namelen+valuelen+2];
    strncpy(buf, envname, namelen);
    buf[namelen] = '=';
    strncpy(buf+namelen+1, envval, valuelen);
    buf[namelen+1+valuelen] = char(0);
    int status = _putenv(buf);
    delete[] buf;
    return status;
#endif
}

char* GetEnv(const char* envname) {
    __TCM_ASSERT(envname, "GetEnv requires valid C string");
    return std::getenv(envname);
}
__TCM_SUPPRESS_WARNING_POP


/***************************************************************************************************
 * TCM-specific helpers
 **************************************************************************************************/

tcm_permit_request_t make_request(int min_sw_threads = tcm_automatic,
                                  int max_sw_threads = tcm_automatic,
                                  tcm_cpu_constraints_t* constraints = nullptr, uint32_t size = 0,
                                  tcm_request_priority_t priority = TCM_REQUEST_PRIORITY_NORMAL,
                                  tcm_permit_flags_t flags = {})
{
    __TCM_ASSERT(!(!constraints ^ !size), "Inconsistent request.");

    tcm_permit_request_t req = TCM_PERMIT_REQUEST_INITIALIZER;

    req.min_sw_threads = min_sw_threads; req.max_sw_threads = max_sw_threads;
    req.cpu_constraints = constraints; req.constraints_size = size;
    req.priority = priority; req.flags = flags;

    return req;
}

tcm_permit_t make_permit(uint32_t* concurrencies, tcm_cpu_mask_t* cpu_masks = nullptr,
                         uint32_t size = 1, tcm_permit_state_t state = TCM_PERMIT_STATE_VOID,
                         tcm_permit_flags_t flags = {})
{
  __TCM_ASSERT(concurrencies, "Array of concurrencies cannot be nullptr.");
  return tcm_permit_t{concurrencies, cpu_masks, size, state, flags};
}

tcm_permit_t make_void_permit(uint32_t* concurrencies, tcm_cpu_mask_t* cpu_masks = nullptr,
                               uint32_t size = 1, tcm_permit_flags_t flags = {})
{
    return make_permit(concurrencies, cpu_masks, size, TCM_PERMIT_STATE_VOID, flags);
}

tcm_permit_t make_active_permit(uint32_t* concurrencies, tcm_cpu_mask_t* cpu_masks = nullptr,
                                 uint32_t size = 1, tcm_permit_flags_t flags = {})
{
  return make_permit(concurrencies, cpu_masks, size, TCM_PERMIT_STATE_ACTIVE, flags);
}

tcm_permit_t make_inactive_permit(uint32_t* concurrencies, tcm_cpu_mask_t* cpu_masks = nullptr,
                                  uint32_t size = 1, tcm_permit_flags_t flags = {})
{
  return make_permit(concurrencies, cpu_masks, size, TCM_PERMIT_STATE_INACTIVE, flags);
}

tcm_permit_t make_pending_permit(uint32_t* concurrencies, tcm_cpu_mask_t* cpu_masks = nullptr,
                                  uint32_t size = 1, tcm_permit_flags_t flags = {})
{
  return make_permit(concurrencies, cpu_masks, size, TCM_PERMIT_STATE_PENDING, flags);
}


/***************************************************************************************************
 * Helpers to make individual TCM tests independent
 **************************************************************************************************/
struct tests_run_statistics {
    unsigned num_run_tests;
    unsigned num_failed_tests;
};

class tcm_tests_data {
public:
    tcm_tests_data(unsigned reserved_num_tests = 128) {
        tests.reserve(reserved_num_tests);
    }

    // Keeping track of TCM clients to be able to automatically clean TCM state before each test
    void add_client(tcm_client_id_t client_id);
    void remove_client(tcm_client_id_t client_id);
    bool cleanup_test_state();

    // Keeping track of TCM tests
    using test_function_ptr = void (*)();
    struct test_info {
        const char* filepath;             // File where the test is defined
        unsigned line;                    // Line number in the file where the test is defined
        std::string test_description;     // Description of the test
        test_function_ptr test_func_addr; // The test itself
    };

    void add_test(test_info&& ti);
    tests_run_statistics run_tests();

    // Updating and reading status of a single test
    void mark_this_test_failed();
private:
    bool run_test(const test_function_ptr test);

    bool is_clean_run = true;
    std::vector<test_info> tests;

    std::mutex tcm_clients_mutex;
    std::vector<tcm_client_id_t> tcm_clients;
};

/***************************************************************************************************
 * Testing and reporting functions
 **************************************************************************************************/
template <bool skip_out_stream = false, bool skip_err_stream = false>
class tcm_test_logger_t {
public:
    tcm_test_logger_t() = default;

    template <typename T> void log(T&& msg) const {
        if constexpr (!skip_out_stream)
            log_to(out_stream(), std::forward<T>(msg));
    }

    template <typename T> void log_error(T&& msg) const {
        if constexpr (!skip_err_stream)
            log_to(err_stream(), std::forward<T>(msg));
    }

    template <typename T> void log_to(std::ostream& os, T&& msg) const {
        os << msg << std::endl;
    }
private:
    static std::ostream& out_stream() { return std::cout; }
    static std::ostream& err_stream() { return std::cerr; }

    tcm_test_logger_t(const tcm_test_logger_t&) = delete;
    tcm_test_logger_t& operator=(const tcm_test_logger_t&) = delete;
};

/* Global objects */
static tcm_test_logger_t</*skip_out_stream*/false, /*skip_err_stream*/false> logger;
static tcm_tests_data tcm_tests;

#define TEST_PREFACE(name, filename, linenumber)                                                   \
  static inline void tcm_test##linenumber();                                                       \
  static inline bool tcm_test_adder##linenumber = []() {                                           \
    tcm_tests.add_test({filename, linenumber, (name), &tcm_test##linenumber});                     \
    return true;                                                                                   \
  }();                                                                                             \
  static inline void tcm_test##linenumber()

#define TEST_AUX(name, filename, linenumber) TEST_PREFACE((name), filename, linenumber)
#define TEST(name) TEST_AUX((name), __FILE__, __LINE__)


bool check(bool b, const std::string& msg, unsigned num_indents = 0,
           const std::string& report_msg = "")
{
  const std::string indent(2 * num_indents, ' '); // Multiplied by two for clearer line distinction

  if (!b) {
    logger.log_error("***************** \n"
                     "*     ERROR     * " + indent + msg + "\n"
                     "***************** \n" + report_msg);
    tcm_tests.mark_this_test_failed();
  } else if (!msg.empty()) {
    logger.log("SUCCESS: " + indent + msg);
  }
  return b;
}

inline bool test_stop(bool b, const std::string& msg) {
  return check(b, "end " + msg);
}

inline bool test_fail(const std::string& msg) {
  return test_stop(false, msg);
}

inline void test_log(const std::string& msg) {
  logger.log(msg);
}

inline bool succeeded(tcm_result_t res) {
  return (TCM_RESULT_SUCCESS == res);
}

inline bool check_success(tcm_result_t res, const std::string& msg = "",
                          const std::string& report_msg = "")
{
  return check(succeeded(res), msg, /*num_indents*/0, report_msg);
}

inline bool check_fail(tcm_result_t res, const std::string& msg = "",
                       const std::string& report_msg = "")
{
  return check(!succeeded(res), msg, /*num_indents*/0, report_msg);
}

/***************************************************************************************************
 * Implementation of tcm_test_global_state's member functions
 **************************************************************************************************/
inline void tcm_tests_data::add_client(tcm_client_id_t client_id) {
    std::lock_guard<std::mutex> lock(tcm_clients_mutex);
    tcm_clients.push_back(client_id);
}

inline void tcm_tests_data::remove_client(tcm_client_id_t client_id) {
    std::lock_guard<std::mutex> lock(tcm_clients_mutex);
    std::vector<tcm_client_id_t>::iterator new_end =
        std::remove_if(tcm_clients.begin(), tcm_clients.end(),
                       [client_id](tcm_client_id_t const& cid) { return cid == client_id; });
    __TCM_ASSERT(new_end != tcm_clients.end(), "Not all clients were added");
    __TCM_ASSERT(tcm_clients.end() - new_end == 1, "More than one client with same ID removed");
    tcm_clients.erase(new_end, tcm_clients.end());
}

inline void tcm_tests_data::mark_this_test_failed() { is_clean_run = false; }

inline bool tcm_tests_data::cleanup_test_state() {
    std::vector<tcm_client_id_t> clients_dump;
    {
        std::lock_guard<std::mutex> lock(tcm_clients_mutex);
        clients_dump = std::move(tcm_clients);
        tcm_clients.clear();
    }

#if !TCM_TEST_SKIPS_TCM_USE
    for (unsigned i = 0; i < clients_dump.size(); ++i) {
        const auto c = clients_dump[i];
        const tcm_result_t r = tcmDisconnect(c);
        if (r != TCM_RESULT_SUCCESS) {
            logger.log_error("TEST CLEANUP ERROR: while disconnecting client " + std::to_string(c));
            mark_this_test_failed();
        }
    }
#endif

    const bool succeed = is_clean_run;
    is_clean_run = true;
    return succeed;
}

inline void tcm_tests_data::add_test(test_info&& ti) {
    tests.push_back(ti);
}

inline std::string get_filename_from_path(const char* filepath) {
    if (!filepath)
        return "<unknown filename>";
    std::string path{filepath};
#if _WIN32
    const char dir_separator = '\\';
#else
    const char dir_separator = '/';
#endif
    const std::size_t start_idx = path.rfind(dir_separator);
    if (start_idx == path.npos)
        return path;            // Did not find the directory separator
    return path.substr(start_idx + 1, path.length() - start_idx - 1);
}

inline void test_prolog(const tcm_tests_data::test_info& ti) {
    std::string status_msg = "*    TEST START: " + ti.test_description + "    *";
    std::string footer_msg = "* " + get_filename_from_path(ti.filepath) + ":" +
                             std::to_string(ti.line) + " *";
    const std::size_t length = std::max(status_msg.length(), footer_msg.length());
    auto header = std::string(length, '*');
    const std::size_t asterisk_length = (length - footer_msg.length()) / 2;
    auto footer_asterisks = std::string(asterisk_length, '*');
    std::string footer = footer_asterisks + footer_msg + footer_asterisks;
    logger.log("\n\n" + header + "\n" + status_msg + "\n" + footer);
}

inline void test_epilog(bool succeed, const tcm_tests_data::test_info& ti) {
    std::string status_message = "*    TEST PASSED: ";
    if (!succeed)
        status_message = "*    TEST FAILED: ";

    status_message += ti.test_description + "    *";
    auto asterisks = std::string(status_message.length(), '*');
    logger.log(asterisks + "\n" + status_message + "\n" + asterisks);
}

inline bool tcm_tests_data::run_test(const test_function_ptr test) {
    bool succeed = false;
    std::string exception_message;

    // Run the test in a separate thread to avoid side-effects related to unmatched
    // registering/unregistering calls for the main thread within the test
    std::thread test_invoker([&]() {
        try {
            test(); // Running the test
            succeed = true;
        } catch (tcm_exception const& e) {
            exception_message = "TCM exception was thrown while running the test. "
                                "Exception description: " + std::string(e.what());
        } catch (std::exception const& e) {
            exception_message = "Runtime exception was thrown while running the test. "
                                "Exception description: " + std::string(e.what());
        } catch (...) {
            exception_message = "Unknown exception was thrown while running the test. "
                                "Detailed description is not available.";
        }

        if (!exception_message.empty())
            logger.log_error(exception_message);

        // Clean the states of TCM and testing framework to make tests independent
        succeed &= cleanup_test_state();
    });
    test_invoker.join();

    return succeed;
}

inline tests_run_statistics tcm_tests_data::run_tests() {
    unsigned num_run_tests = 0, num_succeeded_tests = 0;

    for (const test_info& ti : tests) {
        ++num_run_tests;

        test_prolog(ti);
        const bool succeed = run_test(ti.test_func_addr);
        test_epilog(succeed, ti);

        if (succeed)
            ++num_succeeded_tests;
    }

    return tests_run_statistics{num_run_tests, num_run_tests - num_succeeded_tests};
}

inline void print_run_statistics(const tests_run_statistics& stats) {
    std::string table_header_line = " TESTS STATISTICS ";
    const std::size_t stats_line_length = 72;
    std::string dashes = std::string((stats_line_length - table_header_line.length()) / 2, '-');

    std::size_t description_size = 30, value_size = 4;
    std::stringstream ss;
    ss << "\n" + dashes + table_header_line + dashes + "\n";

    ss << std::right << std::setw(description_size) << "Number of tests executed: "
       << std::right << std::setw(value_size) << stats.num_run_tests << "\n";
    ss << std::right << std::setw(description_size) << "Number of tests failed: "
       << std::right << std::setw(value_size) << stats.num_failed_tests << "\n";

    ss << std::left << std::string(stats_line_length, '-') + "\n";

    logger.log(ss.str().c_str());
}

#ifndef TCM_TEST_OWN_MAIN
int main() {
    const tests_run_statistics stats = tcm_tests.run_tests();

    if (stats.num_run_tests != 0)
        print_run_statistics(stats);
    else
        logger.log("No tests were run.");

    return stats.num_failed_tests;
}
#endif

#endif // __TCM_TESTS_BASIC_TEST_UTILS_HEADER
