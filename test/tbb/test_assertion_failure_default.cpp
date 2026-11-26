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

#if defined(_WIN32) && defined(_MT) && !defined(_DLL) && !defined(__TBBMALLOC_BUILD)
// skip this configuration, because can't control error dialogs in runtime used by the dll
#define TBB_TEST_SKIP 1
#else
#define TBB_TEST_SKIP 0
#endif

#if defined(_WIN32) && !TBB_TEST_SKIP
#define DOCTEST_CONFIG_IMPLEMENT
#define TBB_TEST_DEFINE_MAIN 1
#endif

#include "common/test.h"

#if defined(__TBBMALLOC_BUILD)
#include "../../src/tbb/assert_impl.h"
#else
#include "tbb/global_control.h"
#endif

#include <csignal>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#include <memory>
#include <vector>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif
#else // defined(_WIN32)
#include <cerrno>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif // defined(_WIN32)

//! \file test_assertion_failure_default.cpp
//! \brief Test for the [internal] functionality

const char* const test_description = "default assertion test description";

void trigger_assertion(bool with_description) {
#if defined(__TBBMALLOC_BUILD)
    // this function is not exported, test it in whitebox mode
    rml::internal::assertion_failure("test_location", 42, "test_expression",
                                     with_description ? test_description : nullptr);
#else
    tbb::get_assertion_handler()("test_location", 42, "test_expression",
                                 with_description ? test_description : nullptr);
#endif
}

#if defined(_WIN32)

const char* const child_with_description = "--assertion-with-description";
const char* const child_without_description = "--assertion-without-description";

struct close_handle {
    void operator()(HANDLE handle) const {
        if (handle && handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
};

using unique_handle = std::unique_ptr<void, close_handle>;

std::string run_assertion(bool with_description) {
    constexpr DWORD capacity = 32768;
    std::vector<wchar_t> executable(capacity);
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), capacity);
    REQUIRE_MESSAGE((length > 0 && length < capacity),
                    "Could not locate the assertion test executable");

    const std::string argument = with_description ? child_with_description : child_without_description;
    std::vector<wchar_t> command{L'"'};
    command.insert(command.end(), executable.begin(), executable.begin() + length);
    command.insert(command.end(), {L'"', L' '});
    command.insert(command.end(), argument.begin(), argument.end());
    command.push_back(L'\0');

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE read_end = nullptr, write_end = nullptr;
    REQUIRE_MESSAGE(CreatePipe(&read_end, &write_end, &security, 0), "Could not create stderr pipe");
    unique_handle reader(read_end), writer(write_end);
    REQUIRE_MESSAGE(SetHandleInformation(reader.get(), HANDLE_FLAG_INHERIT, 0),
                    "Could not restrict stderr pipe inheritance");

    const HANDLE null_device = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE_MESSAGE(null_device != INVALID_HANDLE_VALUE, "Could not open NUL for child stdin and stdout");
    unique_handle null_stream(null_device);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    // we want to redirect the child's stderr to the pipe
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = null_stream.get();
    startup.hStdOutput = null_stream.get();
    startup.hStdError = writer.get();
    PROCESS_INFORMATION process{};
    REQUIRE_MESSAGE(CreateProcessW(executable.data(), command.data(), nullptr, nullptr, TRUE, 0,
                                   nullptr, nullptr, &startup, &process),
                    "Could not launch the assertion child");
    unique_handle child(process.hProcess), thread(process.hThread);
    writer.reset();

    std::string diagnostic;
    char buffer[256];
    DWORD count;
    while (ReadFile(reader.get(), buffer, sizeof(buffer), &count, nullptr)) {
        diagnostic.append(buffer, count);
    }
    const DWORD read_error = GetLastError();
    CHECK_MESSAGE(read_error == ERROR_BROKEN_PIPE, "Could not read child stderr");
    REQUIRE_MESSAGE(WaitForSingleObject(child.get(), INFINITE) == WAIT_OBJECT_0,
                    "Could not wait for the assertion child");
    DWORD status = 0;
    REQUIRE_MESSAGE(GetExitCodeProcess(child.get(), &status), "Could not read the child exit status");
    REQUIRE_MESSAGE(status != 0, "The assertion child did not terminate abnormally (expected non-zero exit status)");
    return diagnostic;
}

#else

std::string run_assertion(bool with_description) {
    int pipe_ends[2];
    REQUIRE_MESSAGE(pipe(pipe_ends) == 0, "Could not create stderr pipe");
    const pid_t child = fork();
    if (child == 0) {
        close(pipe_ends[0]);
        if (dup2(pipe_ends[1], STDERR_FILENO) == -1) {
            std::_Exit(EXIT_FAILURE);
        }
        if (pipe_ends[1] != STDERR_FILENO) {
            close(pipe_ends[1]);
        }
        const rlimit no_core_dumps{0, 0};
        if (setrlimit(RLIMIT_CORE, &no_core_dumps) != 0 || std::signal(SIGABRT, SIG_DFL) == SIG_ERR) {
            std::_Exit(EXIT_FAILURE);
        }
        trigger_assertion(with_description);
        std::_Exit(EXIT_SUCCESS);
    }
    close(pipe_ends[1]);
    if (child < 0) {
        close(pipe_ends[0]);
        FAIL("Could not fork the assertion child");
        return {};
    }

    std::string diagnostic;
    char buffer[256];
    std::ssize count;
    do {
        count = read(pipe_ends[0], buffer, sizeof(buffer));
        if (count > 0) {
            diagnostic.append(buffer, static_cast<std::size_t>(count));
        }
    } while (count > 0 || (count < 0 && errno == EINTR));
    close(pipe_ends[0]);

    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited == -1 && errno == EINTR);
    CHECK_MESSAGE(count == 0, "Could not read child stderr");
    REQUIRE_MESSAGE(waited == child, "Could not wait for the assertion child");
    CHECK_MESSAGE((WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT),
                  "The assertion child did not terminate with SIGABRT");
    return diagnostic;
}

#endif

bool has_line(const std::string& haystack, const std::string& needle) {
    for (std::size_t pos = haystack.find(needle); pos != std::string::npos; pos = haystack.find(needle, pos + 1)) {
        const std::size_t end = pos + needle.size();
        // we want needle to be a complete line, not part of another line
        if ((pos == 0 || haystack[pos - 1] == '\n') &&
            (haystack.compare(end, 1, "\n") == 0 || haystack.compare(end, 2, "\r\n") == 0 ||
             end == haystack.size())) {
            return true;
        }
    }
    return false;
}

//! Test for the default assertion handler behavior. Creates a child process to trigger the
//! assertion and captures its output.
//! \brief \ref error_guessing
TEST_CASE("Default assertion handler reports the failure and aborts" * doctest::skip(TBB_TEST_SKIP)) {
    bool with_description = false;
    SUBCASE("with description") { with_description = true; }
    SUBCASE("without description") {}

    const std::string diagnostic = run_assertion(with_description);
    INFO("Child stderr: " << diagnostic);
    CHECK(has_line(diagnostic, "Assertion test_expression failed (located in the test_location function, line in file: 42)"));
    if (with_description) {
        CHECK(has_line(diagnostic, std::string("Detailed description: ") + test_description));
    } else {
        CHECK(diagnostic.find("Detailed description:") == std::string::npos);
    }
}

#if TBB_TEST_DEFINE_MAIN

int main(int argc, char** argv) {
    if (argc == 2 &&
        (strcmp(argv[1], child_with_description) == 0 || strcmp(argv[1], child_without_description) == 0)) {
        // child process: suppress error dialogs, because we want to test assertion_failure_default
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#if defined(_MSC_VER)
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
        _CrtSetReportMode(_CRT_ASSERT, 0);
#endif
#endif
        trigger_assertion(strcmp(argv[1], child_with_description) == 0);
        return EXIT_SUCCESS;
    }
    return doctest::Context(argc, argv).run();
}

#endif // TBB_TEST_DEFINE_MAIN
