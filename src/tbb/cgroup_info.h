/*
    Copyright (c) 2025 UXL Foundation Сontributors

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

#ifndef _TBB_cgroup_info_H
#define _TBB_cgroup_info_H

#include <cstdio>
#include <climits>
#include <cstring>
#include <memory>

#include <mntent.h>

#include "oneapi/tbb/detail/_assert.h"

namespace tbb {
namespace detail {
namespace r1 {

// Linux control groups support
class cgroup_info {
public:
    static bool is_cpu_constrained(int& constrained_num_cpus) {
        static const int num_cpus = parse_cpu_constraints();
        if (num_cpus == error_value || num_cpus == unlimited_num_cpus)
            return false;

        constrained_num_cpus = num_cpus;
        return true;
    }

private:
    static void close_file(FILE *file) { fclose(file); };
    using unique_file_t = std::unique_ptr<FILE, decltype(&close_file)>;

    static constexpr int unlimited_num_cpus = INT_MAX;
    static constexpr int error_value = 0; // Some impossible value for the number of CPUs

    static int determine_num_cpus(long long cpu_quota, long long cpu_period) {
        if (0 == cpu_period)
            return error_value; // Avoid division by zero, use the default number of CPUs

        const long long num_cpus = (cpu_quota + cpu_period - 1) / cpu_period;
        return num_cpus > 0 ? int(num_cpus) : 1; // Ensure at least one CPU is returned
    }

    static constexpr std::size_t rel_path_size = 256; // Size of the relative path buffer
    struct cgroup_paths {
        char v1_relative_path[rel_path_size] = {0};
        char v2_relative_path[rel_path_size] = {0};
    };

    static const char* look_for_cpu_controller_path(const char* line, const char* last_char) {
        const char* path_start = line;
        while ((path_start = std::strstr(path_start, "cpu"))) {
            // At least ":/" must be at the end of line for a valid cgroups file
            if (line - path_start == 0 || last_char - path_start <= 3)
                break; // Incorrect line in the cgroup file, skip it

            const char prev_char = *(path_start - 1);
            if (prev_char != ':' && prev_char != ',') {
                ++path_start; // Not a valid "cpu" controller, continue searching
                continue;
            }

            const char next_char = *(path_start + 3);
            if (next_char != ':' && next_char != ',') {
                ++path_start; // Not a valid "cpu" controller, continue searching
                continue;
            }

            path_start = std::strchr(path_start + 3, ':') + 1;
            __TBB_ASSERT(path_start <= last_char, "Too long path?");
            break;
        }
        return path_start;
    }

    static void cache_relative_path_for(FILE* cgroup_fd, cgroup_paths& paths_cache) {
        char* relative_path = nullptr;
        char line[rel_path_size] = {0};
        const char* last_char = line + rel_path_size - 1;

        const char* path_start = nullptr;
        while (fgets(line, rel_path_size, cgroup_fd)) {
            path_start = nullptr;

            if (std::strncmp(line, "0::", 3) == 0) {
                path_start = line + 3; // cgroup v2 unified path
                relative_path = paths_cache.v2_relative_path;
            } else {
                // cgroups v1 allows comount multiple controllers against the same hierarchy
                path_start = look_for_cpu_controller_path(line, last_char);
                relative_path = paths_cache.v1_relative_path;
            }

            if (path_start)
                break;    // Found "cpu" controller path
        }

        std::strncpy(relative_path, path_start, rel_path_size);
        relative_path[rel_path_size - 1] = '\0'; // Ensure null-termination after copy

        char* new_line = std::strrchr(relative_path, '\n');
        if (new_line)
            *new_line = '\0';   // Ensure no new line at the end of the path is copied
    }

    static bool try_read_cgroup_v1_num_cpus_from(const char* dir, int& num_cpus) {
        char path[PATH_MAX] = {0};
        if (snprintf(path, PATH_MAX, "%s/cpu.cfs_quota_us", dir) < 0)
            return false;       // Failed to create path

        unique_file_t fd(fopen(path, "r"), &close_file);
        if (!fd)
            return false;

        long long cpu_quota = 0;
        if (fscanf(fd.get(), "%lld", &cpu_quota) != 1)
            return false;

        if (-1 == cpu_quota) {
            num_cpus = unlimited_num_cpus; // -1 quota means maximum available CPUs
            return true;
        }

        if (snprintf(path, PATH_MAX, "%s/cpu.cfs_period_us", dir) < 0)
            return false;       // Failed to create path;
        fd.reset(fopen(path, "r"));
        if (!fd)
            return false;

        long long cpu_period = 0;
        if (fscanf(fd.get(), "%lld", &cpu_period) != 1)
            return false;

        num_cpus = determine_num_cpus(cpu_quota, cpu_period);
        return num_cpus != error_value; // Return true if valid number of CPUs was determined
    }

    static bool try_read_cgroup_v2_num_cpus_from(const char* dir, int& num_cpus) {
        char path[PATH_MAX] = {0};
        if (snprintf(path, PATH_MAX, "%s/cpu.max", dir) < 0)
            return false;       // Failed to create path

        unique_file_t fd(fopen(path, "r"), &close_file);
        if (!fd)
            return false;

        long long cpu_period = 0;
        char cpu_quota_str[16] = {0};
        if (fscanf(fd.get(), "%15s %lld", cpu_quota_str, &cpu_period) != 2)
            return false;

        if (std::strncmp(cpu_quota_str, "max", 3) == 0) {
            num_cpus = unlimited_num_cpus;  // "max" means no CPU constraint
            return true;
        }

        errno = 0; // Reset errno before strtoll
        char* str_end = nullptr;
        long long cpu_quota = std::strtoll(cpu_quota_str, &str_end, /*base*/ 10);
        if (errno == ERANGE || str_end == cpu_quota_str)
            return false;

        num_cpus = determine_num_cpus(cpu_quota, cpu_period);
        return num_cpus != error_value; // Return true if valid number of CPUs was determined
    }

    static int parse_cgroup_entry(const char* mnt_dir, const char* mnt_type, FILE* cgroup_fd,
                                  cgroup_paths& paths_cache)
    {
        int num_cpus = error_value; // Initialize to an impossible value
        char dir[PATH_MAX] = {0};
        if (!std::strncmp(mnt_type, "cgroup2", 7)) { // Found cgroup v2 mount entry
            // At first, try reading CPU quota directly
            if (try_read_cgroup_v2_num_cpus_from(mnt_dir, num_cpus))
                return num_cpus; // Successfully read number of CPUs for cgroup v2

            if (!*paths_cache.v2_relative_path)
                cache_relative_path_for(cgroup_fd, paths_cache);

            // Now try reading including relative path
            if (snprintf(dir, PATH_MAX, "%s/%s", mnt_dir, paths_cache.v2_relative_path) >= 0)
                try_read_cgroup_v2_num_cpus_from(dir, num_cpus);
            return num_cpus;
        }

        __TBB_ASSERT(std::strncmp(mnt_type, "cgroup", 6) == 0, "Unexpected cgroup type");

        // TODO: Before opening the path, make sure the mnt_opts entry contains "cpu" controller
        if (try_read_cgroup_v1_num_cpus_from(mnt_dir, num_cpus))
            return num_cpus; // Successfully read number of CPUs for cgroup v1

        if (!*paths_cache.v1_relative_path)
            cache_relative_path_for(cgroup_fd, paths_cache);

        if (snprintf(dir, PATH_MAX, "%s/%s", mnt_dir, paths_cache.v1_relative_path) >= 0)
            try_read_cgroup_v1_num_cpus_from(dir, num_cpus);
        return num_cpus;
    }

    static int parse_cpu_constraints() {
        // Reading /proc/self/mounts and /proc/self/cgroup anyway, so open them right away
        unique_file_t cgroup_file_ptr(fopen("/proc/self/cgroup", "r"), &close_file);
        if (!cgroup_file_ptr)
            return error_value; // Failed to open cgroup file

        auto close_mounts_file = [](FILE* file) { endmntent(file); };
        using unique_mounts_file_t = std::unique_ptr<FILE, decltype(close_mounts_file)>;
        unique_mounts_file_t mounts_file_ptr(setmntent("/proc/self/mounts", "r"), close_mounts_file);
        if (!mounts_file_ptr)
            return error_value;

        // TODO: To avoid parsing /proc/self/mounts, read relative paths first and try opening
        //       necessary files in the most probable mount points (see RFC for details)
        cgroup_paths relative_paths_cache;
        struct mntent mntent;
        constexpr std::size_t buffer_size = 4096; // Allocate a buffer for reading mount entries
        char mount_entry_buffer[buffer_size];

        int found_num_cpus = error_value; // Initialize to an impossible value
        // Read the mounts file and cgroup file to determine the number of CPUs
        while (getmntent_r(mounts_file_ptr.get(), &mntent, mount_entry_buffer, buffer_size)) {
            if (std::strncmp(mntent.mnt_type, "cgroup", 6) == 0) {
                found_num_cpus = parse_cgroup_entry(mntent.mnt_dir, mntent.mnt_type,
                                                    cgroup_file_ptr.get(), relative_paths_cache);
                if (found_num_cpus != error_value)
                    break;
            }
        }
        return found_num_cpus;
    }
};

} // namespace r
} // namespace detail
} // namespace tbb

#endif // _TBB_cgroup_info_H
