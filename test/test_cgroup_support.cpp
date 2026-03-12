/*
    Copyright (C) 2026 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#if __linux__

#include "linux/cgroup_info.h"

#define TCM_TEST_SKIPS_TCM_USE 1
#include "basic_test_utils.h"
#include "concurrency_utils.h"

#include <sys/stat.h>

#include <fstream>
#include <cstdio>

using reference_cgroup_info = ::cgroup_info;

template <typename TestData>
using actual_cgroup_info = ::tcm::internal::cgroup_info<TestData>;

template <typename TestData>
void check_cpu_constraints(const bool expected_result, const int expected_num_cpus) {
    int actual_num_cpus = -1;
    const bool actual_result = actual_cgroup_info<TestData>::is_cpu_constrained(actual_num_cpus);

    check(expected_result == actual_result, "Return value is expected");
    check(expected_num_cpus == actual_num_cpus, "Determined number of CPUs is expected");
}

TEST("CPU resource constraint equals to the one from reference implementation") {
    int expected_num_cpus = -1;
    const bool expected_result = reference_cgroup_info::is_cpu_constrained(expected_num_cpus);

    check_cpu_constraints<tcm::internal::default_cgroup_settings>(expected_result,
                                                                  expected_num_cpus);
}

TEST("Non-existent /proc/self/cgroup file") {
    struct non_existent_cgroup_file {
        const char* proc_self_cgroup_path = "non-existent_cgroup_file_path";
        const char* sys_fs_cgroup_dir_path = "/sys/fs/cgroup";
        const char* proc_self_mounts_path = "fictitious-mounts-file-path";
    };
    check_cpu_constraints<non_existent_cgroup_file>(/*expected_result*/false, /*expected_num_cpus*/-1);
}

void prepare_proc_self_cgroup_file(const char* cgroup_filename, const std::vector<const char*> lines) {
    // Preparing test data
    std::ofstream cgroup{std::string(cgroup_filename)};
    for (auto const& line : lines)
        cgroup << line << std::endl;
}

void prepare_cpu_max_file(const char* dir_path, const char* cpu_quota, const char* cpu_period) {
    std::string cpu_max_file_path = std::string(dir_path) + "/cpu.max";
    std::ofstream cpu_max_file(cpu_max_file_path);
    cpu_max_file << cpu_quota << " " << cpu_period << std::endl;
}

void prepare_cgroup_v1_cpu_quota_file(const char* dir_path, const char* cpu_quota) {
    std::string quota_file_path = std::string(dir_path) + "/cpu.cfs_quota_us";
    std::ofstream quota_file(quota_file_path);
    quota_file << cpu_quota << std::endl;
}

void prepare_cgroup_v1_cpu_period_file(const char* dir_path, const char* cpu_period) {
    std::string period_file_path = std::string(dir_path) + "/cpu.cfs_period_us";
    std::ofstream period_file(period_file_path);
    period_file << cpu_period << std::endl;

}

void prepare_cgroup_v1_files(const char* dir_path, const char* cpu_quota, const char* cpu_period) {
    prepare_cgroup_v1_cpu_quota_file(dir_path, cpu_quota);
    prepare_cgroup_v1_cpu_period_file(dir_path, cpu_period);
}

/** Test data.
 *
 * Note that each test should likely be using its own type for the test data. This helps to force
 * the being tested functionality to re-read configuration files and thus avoid using the cached
 * ones.
 */
struct cgroup_test_data {
    const char* proc_self_cgroup_path = "proc_self_cgroup_file";
    const char* sys_fs_cgroup_dir_path = "sys_fs_cgroup_dir";
    const char* proc_self_mounts_path = "/proc/self/mounts";
};

TEST("cgroup v2 path finds correct CPU limitation") {
    struct test_data : cgroup_test_data {
        const char* cpu_quota = "300000";
        const char* cpu_period = "100000";
    } data;

    prepare_proc_self_cgroup_file(data.proc_self_cgroup_path, /*lines*/{"0::./."});
    mkdir(data.sys_fs_cgroup_dir_path, 0777);
    prepare_cpu_max_file(data.sys_fs_cgroup_dir_path, data.cpu_quota, data.cpu_period);

    check_cpu_constraints<test_data>(/*expected_result*/true, /*expected_num_cpus*/3);
}

TEST("'max' string as CPU quota means no CPU limitation for cgroup v2 path") {
    struct test_data : cgroup_test_data {
        const char* cpu_quota = "max";
        const char* cpu_period = "100000";
    } data;

    prepare_proc_self_cgroup_file(data.proc_self_cgroup_path, /*lines*/{"0::./."});
    mkdir(data.sys_fs_cgroup_dir_path, 0777);
    prepare_cpu_max_file(data.sys_fs_cgroup_dir_path, data.cpu_quota, data.cpu_period);

    check_cpu_constraints<test_data>(/*expected_result*/false, /*expected_num_cpus*/-1);
}

TEST("Wrong CPU quota for cgroup v2 path") {
    struct test_data : cgroup_test_data {
        const char* proc_self_mounts_path = "imaginary-mounts-file-path"; // Hiding base path

        const char* cpu_quota = "abracadabra";
        const char* cpu_period = "100000";
    } data;

    prepare_proc_self_cgroup_file(data.proc_self_cgroup_path, /*lines*/{"0::./."});
    mkdir(data.sys_fs_cgroup_dir_path, 0777);
    prepare_cpu_max_file(data.sys_fs_cgroup_dir_path, data.cpu_quota, data.cpu_period);

    check_cpu_constraints<test_data>(/*expected_result*/false, /*expected_num_cpus*/-1);
}

TEST("Zero CPU period in cpu.max avoids division by zero problem") {
    struct test_data : cgroup_test_data {
        const char* proc_self_mounts_path = "imaginary-mounts-file-path"; // Hiding base path

        const char* cpu_quota = "300000";
        const char* zero_cpu_period = "0";
    } data;

    prepare_proc_self_cgroup_file(data.proc_self_cgroup_path, /*lines*/{"0::./."});
    mkdir(data.sys_fs_cgroup_dir_path, 0777);
    prepare_cpu_max_file(data.sys_fs_cgroup_dir_path, data.cpu_quota, data.zero_cpu_period);

    check_cpu_constraints<test_data>(/*expected_result*/false, /*expected_num_cpus*/-1);
}

TEST("Negative CPU quota in cpu.max results in single resource") {
    struct test_data : cgroup_test_data {
        const char* cpu_quota = "-100000";
        const char* cpu_period = " 100000";
    } data;

    prepare_proc_self_cgroup_file(data.proc_self_cgroup_path, /*lines*/{"0::./."});
    mkdir(data.sys_fs_cgroup_dir_path, 0777);
    prepare_cpu_max_file(data.sys_fs_cgroup_dir_path, data.cpu_quota, data.cpu_period);

    check_cpu_constraints<test_data>(/*expected_result*/true, /*expected_num_cpus*/1);
}

TEST("Wrong lines for CPU controller and CPU quota in cgroup v1") {
    struct test_data : cgroup_test_data {
        const char* cpu_quota = "abcd";
        const char* cpu_period = " 100000";
    } data;

    prepare_proc_self_cgroup_file(data.proc_self_cgroup_path,
        /*lines*/
        {/*cgroup v2 indicator*/"0::./.",
         /*incorrect lines*/"cpu:/", "3:cpu", "42:3cpu",
         /*other controllers*/"3:cpuacct,cpuset:/tmp",
         /*cgroup v1 indicator*/"3:cputset,cpu:./."});

    mkdir(data.sys_fs_cgroup_dir_path, 0777);
    prepare_cgroup_v1_files(data.sys_fs_cgroup_dir_path, data.cpu_quota, data.cpu_period);

    check_cpu_constraints<test_data>(/*expected_result*/false, /*expected_num_cpus*/-1);
}

TEST("No cpu.cfs_quota file for cgroup v1") {
    struct test_data : cgroup_test_data {} data;

    prepare_proc_self_cgroup_file(data.proc_self_cgroup_path,
        /*lines*/ {/*cgroup v1 indicator*/"3:cputset,cpu:./."});

    std::remove((std::string(data.sys_fs_cgroup_dir_path) + "/cpu.cfs_quota_us").c_str());
    check_cpu_constraints<test_data>(/*expected_result*/false, /*expected_num_cpus*/-1);
}

TEST("No cpu.cfs_period file for cgroup v1") {
    struct test_data : cgroup_test_data {} data;

    prepare_proc_self_cgroup_file(data.proc_self_cgroup_path,
        /*lines*/ {/*cgroup v1 indicator*/"3:cputset,cpu:./."});

    prepare_cgroup_v1_cpu_quota_file(data.sys_fs_cgroup_dir_path, /*cpu_quota*/"800000");
    std::remove((std::string(data.sys_fs_cgroup_dir_path) + "/cpu.cfs_period_us").c_str());

    check_cpu_constraints<test_data>(/*expected_result*/false, /*expected_num_cpus*/-1);
}

TEST("Wrong cpu.cfs_period content for cgroup v1") {
    struct test_data : cgroup_test_data {} data;

    prepare_proc_self_cgroup_file(data.proc_self_cgroup_path,
        /*lines*/ {/*cgroup v1 indicator*/"3:cpu:./."});
    prepare_cgroup_v1_files(data.sys_fs_cgroup_dir_path, /*quota*/"100000", /*period*/"bad");

    check_cpu_constraints<test_data>(/*expected_result*/false, /*expected_num_cpus*/-1);
}

TEST("Unrestricted CPU limits in cgroup v1") {
    struct test_data : cgroup_test_data {
        const char* cpu_quota = "-1";
        const char* cpu_period = "100000";
    } data;

    prepare_proc_self_cgroup_file(data.proc_self_cgroup_path,
        /*lines*/ {/*cgroup v1 indicator*/"3:cputset,cpu:./."});

    mkdir(data.sys_fs_cgroup_dir_path, 0777);
    prepare_cgroup_v1_files(data.sys_fs_cgroup_dir_path, data.cpu_quota, data.cpu_period);

    check_cpu_constraints<test_data>(/*expected_result*/false, /*expected_num_cpus*/-1);
}

TEST("cgroup v1 path finds correct CPU limitation") {
    struct test_data : cgroup_test_data {
        const char* cpu_quota = "200000";
        const char* cpu_period = "100000";
    } data;

    prepare_proc_self_cgroup_file(data.proc_self_cgroup_path,
        /*lines*/ {/*cgroup v1 indicator*/"3:cputset,cpu:./."});

    mkdir(data.sys_fs_cgroup_dir_path, 0777);
    prepare_cgroup_v1_files(data.sys_fs_cgroup_dir_path, data.cpu_quota, data.cpu_period);

    check_cpu_constraints<test_data>(/*expected_result*/true, /*expected_num_cpus*/2);
}

#else

int main() {
    // Skipping the test on non-Linux systems
    return 0;
}

#endif                          // __linux__
