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

#if defined(_WIN32)
#define DOCTEST_CONFIG_IMPLEMENT
#endif
#include "common/test.h"

#include "tbb/global_control.h"

#include <csignal>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#include <memory>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif
#else
#include <cerrno>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

//! \file test_assertion_failure_default.cpp
//! \brief Test for the [internal] functionality

const char* const description = "default assertion test description";

void trigger_assertion(bool with_description) {
    tbb::get_assertion_handler()("test_location", 42, "test_expression",
                                 with_description ? description : nullptr);
}

#if defined(_WIN32)

const char* const child_with_description = "--assertion-with-description";
const char* const child_without_description = "--assertion-without-description";

struct close_handle {
    void operator()(HANDLE handle) const { CloseHandle(handle); }
};

using unique_handle = std::unique_ptr<void, close_handle>;

std::string run_assertion(bool with_description) {
    wchar_t executable[32768];
    constexpr DWORD capacity = sizeof(executable) / sizeof(*executable);
    const DWORD length = GetModuleFileNameW(nullptr, executable, capacity);
    REQUIRE_MESSAGE((length > 0 && length < capacity),
                    "Could not locate the assertion test executable");

    const std::string argument = with_description ? child_with_description : child_without_description;
    std::wstring command = L"\"" + std::wstring(executable, length) + L"\" ";
    command.append(argument.begin(), argument.end());

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
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = null_stream.get();
    startup.hStdOutput = null_stream.get();
    startup.hStdError = writer.get();
    PROCESS_INFORMATION process{};
    REQUIRE_MESSAGE(CreateProcessW(executable, &command[0], nullptr, nullptr, TRUE, 0,
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
    CHECK_MESSAGE(status == 3, "The assertion child did not exit with the Windows CRT abort status");
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
    ssize_t count;
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

bool has_line(const std::string& diagnostic, const std::string& line) {
    for (std::size_t pos = diagnostic.find(line); pos != std::string::npos; pos = diagnostic.find(line, pos + 1)) {
        const std::size_t end = pos + line.size();
        if ((pos == 0 || diagnostic[pos - 1] == '\n') &&
            (diagnostic.compare(end, 1, "\n") == 0 || diagnostic.compare(end, 2, "\r\n") == 0)) {
            return true;
        }
    }
    return false;
}

constexpr char test_name[] = "Default assertion handler reports the failure and aborts";

#if defined(_WIN32) && defined(_MT) && !defined(_DLL)

//! Test for the default assertion handler: skip in this configuration, because can't control error
//! dialogs in runtime used by dll.
//! \brief \ref interface \ref requirement
TEST_CASE(test_name * doctest::skip()) {}

#else // defined(_WIN32) && defined(_MT) && !defined(_DLL)

//! Test for the default assertion handler behavior. Creates a child process to trigger the
//! assertion and captures its output.
//! \brief \ref error_guessing
TEST_CASE(test_name) {
    bool with_description = false;
    SUBCASE("with description") { with_description = true; }
    SUBCASE("without description") {}

    const std::string diagnostic = run_assertion(with_description);
    INFO("Child stderr: ", diagnostic);
    CHECK(has_line(diagnostic, "Assertion test_expression failed (located in the test_location function, line in file: 42)"));
    if (with_description) {
        CHECK(has_line(diagnostic, std::string("Detailed description: ") + description));
    } else {
        CHECK(diagnostic.find("Detailed description:") == std::string::npos);
    }
}

#if defined(_WIN32)

int main(int argc, char** argv) {
    if (argc == 2) {
        // child process: suppress error dialogs, because we want to test assertion_failure_default
        const std::string argument = argv[1];
        if (argument == child_with_description || argument == child_without_description) {
            SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#if defined(_MSC_VER)
            _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
            _CrtSetReportMode(_CRT_ASSERT, 0);
#endif
#endif
            trigger_assertion(argument == child_with_description);
            return EXIT_SUCCESS;
        }
    }
    return doctest::Context(argc, argv).run();
}

#endif // defined(_WIN32)

#endif // defined(_WIN32) && defined(_MT) && !defined(_DLL)
