/*
    Copyright (c) 2005-2025 Intel Corporation
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

// Source file for miscellaneous entities that are infrequently referenced by
// an executing program, and implementation of which requires dynamic linking.

#include "misc.h"

#if !defined(__TBB_HardwareConcurrency)

#include "dynamic_link.h"
#include <stdio.h>
#include <limits.h>

#if _WIN32||_WIN64
#include <windows.h>
#if __TBB_WIN8UI_SUPPORT
#include <thread>
#endif
#else
#include <unistd.h>
#if __unix__
#include <mntent.h>
#include <memory>
#include <climits>
#if __linux__
#include <sys/sysinfo.h>
#endif
#include <cstring>
#include <sched.h>
#include <cerrno>
#elif __sun
#include <sys/sysinfo.h>
#elif __FreeBSD__
#include <cerrno>
#include <cstring>
#include <sys/param.h>  // Required by <sys/cpuset.h>
#include <sys/cpuset.h>
#endif
#endif

namespace tbb {
namespace detail {
namespace r1 {

#if __TBB_USE_OS_AFFINITY_SYSCALL

#if __unix__
// Handlers for interoperation with libiomp
static int (*libiomp_try_restoring_original_mask)();
// Table for mapping to libiomp entry points
static const dynamic_link_descriptor iompLinkTable[] = {
    DLD_NOWEAK( kmp_set_thread_affinity_mask_initial, libiomp_try_restoring_original_mask )
};
#endif

static void set_thread_affinity_mask( std::size_t maskSize, const basic_mask_t* threadMask ) {
#if __FreeBSD__ || __NetBSD__ || __OpenBSD__
    if( cpuset_setaffinity( CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, maskSize, threadMask ) )
#else /* __unix__ */
    if( sched_setaffinity( 0, maskSize, threadMask ) )
#endif
        // Here and below the error severity is lowered from critical level
        // because it may happen during TBB library unload because of not
        // waiting for workers to complete (current RML policy, to be fixed).
        // handle_perror( errno, "setaffinity syscall" );
        runtime_warning( "setaffinity syscall failed" );
}

static void get_thread_affinity_mask( std::size_t maskSize, basic_mask_t* threadMask ) {
#if __FreeBSD__ || __NetBSD__ || __OpenBSD__
    if( cpuset_getaffinity( CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, maskSize, threadMask ) )
#else /* __unix__ */
    if( sched_getaffinity( 0, maskSize, threadMask ) )
#endif
    runtime_warning( "getaffinity syscall failed" );
}

static basic_mask_t* process_mask;
static int num_masks;

void destroy_process_mask() {
    delete [] process_mask;
    process_mask = nullptr;
}

#define curMaskSize sizeof(basic_mask_t) * num_masks
affinity_helper::~affinity_helper() {
    if( threadMask ) {
        if( is_changed ) {
            set_thread_affinity_mask( curMaskSize, threadMask );
        }
        delete [] threadMask;
    }
}
void affinity_helper::protect_affinity_mask( bool restore_process_mask ) {
    if( threadMask == nullptr && num_masks ) { // TODO: assert num_masks validity?
        threadMask = new basic_mask_t [num_masks];
        std::memset( threadMask, 0, curMaskSize );
        get_thread_affinity_mask( curMaskSize, threadMask );
        if( restore_process_mask ) {
            __TBB_ASSERT( process_mask, "A process mask is requested but not yet stored" );
            is_changed = memcmp( process_mask, threadMask, curMaskSize );
            if( is_changed )
                set_thread_affinity_mask( curMaskSize, process_mask );
        } else {
            // Assume that the mask will be changed by the caller.
            is_changed = 1;
        }
    }
}
void affinity_helper::dismiss() {
    delete [] threadMask;
    threadMask = nullptr;
    is_changed = 0;
}
#undef curMaskSize

static std::atomic<do_once_state> hardware_concurrency_info;

static int theNumProcs;

// Linux control groups support
class cgroup_info {
public:
    static bool is_cpu_constrained(int& constrained_num_cpus) {
        static const int num_cpus = parse_cgroup_cpu_constraints();
        if (num_cpus == error_value || num_cpus == unlimited_num_cpus)
            return false;

        constrained_num_cpus = num_cpus;
        return true;
    }

private:
    static void close_mounts_file(std::FILE *file) { endmntent(file); };

    static void close_file(std::FILE *file) { std::fclose(file); };
    using unique_file_t = std::unique_ptr<FILE, decltype(&close_file)>;

    static constexpr int unlimited_num_cpus = INT_MAX;
    static constexpr int error_value = 0; // Some impossible value for the number of CPUs

    static int determine_num_cpus(long long cpu_quota, long long cpu_period) {
        if (0 == cpu_period)
            return error_value; // Avoid division by zero, use the default number of CPUs

        const long long num_cpus = (cpu_quota + cpu_period / 2) / cpu_period;
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
        std::snprintf(path, PATH_MAX, "%s/cpu.cfs_quota_us", dir);

        unique_file_t fd(std::fopen(path, "r"), &close_file);
        if (!fd)
            return false;

        long long cpu_quota = 0;
        if (std::fscanf(fd.get(), "%lld", &cpu_quota) != 1)
            return false;

        if (-1 == cpu_quota) {
            num_cpus = unlimited_num_cpus; // -1 quota means maximum available CPUs
            return true;
        }

        std::snprintf(path, PATH_MAX, "%s/cpu.cfs_period_us", dir);
        fd.reset(std::fopen(path, "r"));
        if (!fd)
            return false;

        long long cpu_period = 0;
        if (std::fscanf(fd.get(), "%lld", &cpu_period) != 1)
            return false;

        num_cpus = determine_num_cpus(cpu_quota, cpu_period);
        return num_cpus != error_value; // Return true if valid number of CPUs was determined
    }

    static bool try_read_cgroup_v2_num_cpus_from(const char* dir, int& num_cpus) {
        char path[PATH_MAX] = {0};
        std::snprintf(path, PATH_MAX, "%s/cpu.max", dir);

        unique_file_t fd(fopen(path, "r"), &close_file);
        if (!fd)
            return false;

        long long cpu_period = 0;
        char cpu_quota_str[16] = {0};
        if (std::fscanf(fd.get(), "%15s %lld", cpu_quota_str, &cpu_period) != 2)
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
            std::snprintf(dir, PATH_MAX, "%s/%s", mnt_dir, paths_cache.v2_relative_path);
            try_read_cgroup_v2_num_cpus_from(dir, num_cpus);
            return num_cpus;
        }

        __TBB_ASSERT(std::strncmp(mnt_type, "cgroup", 6) == 0, "Unexpected cgroup type");

        if (try_read_cgroup_v1_num_cpus_from(mnt_dir, num_cpus))
            return num_cpus; // Successfully read number of CPUs for cgroup v1

        if (!*paths_cache.v1_relative_path)
            cache_relative_path_for(cgroup_fd, paths_cache);

        std::snprintf(dir, PATH_MAX, "%s/%s", mnt_dir, paths_cache.v1_relative_path);
        try_read_cgroup_v1_num_cpus_from(dir, num_cpus);
        return num_cpus;
    }

    static int parse_cgroup_cpu_constraints() {
        using unique_mounts_file_t = std::unique_ptr<FILE, decltype(&close_mounts_file)>;

        // Reading /proc/self/mounts and /proc/self/cgroup anyway, so open them right away
        unique_mounts_file_t mounts_file_ptr(setmntent("/proc/self/mounts", "r"), &close_mounts_file);
        unique_file_t cgroup_file_ptr(fopen("/proc/self/cgroup", "r"), &close_file);
        if (!mounts_file_ptr || !cgroup_file_ptr)
            return error_value;

        cgroup_paths relative_paths_cache;
        struct mntent mntent;
        const std::size_t buffer_size = 4 * 4096; // TODO: Leave a comment why using such size
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

static void initialize_hardware_concurrency_info () {
    int err;
    int availableProcs = 0;
    int numMasks = 1;
    int maxProcs = sysconf(_SC_NPROCESSORS_ONLN);
    basic_mask_t* processMask;
    const std::size_t BasicMaskSize =  sizeof(basic_mask_t);
    for (;;) {
        const int curMaskSize = BasicMaskSize * numMasks;
        processMask = new basic_mask_t[numMasks];
        std::memset( processMask, 0, curMaskSize );
#if __FreeBSD__ || __NetBSD__ || __OpenBSD__
        // CPU_LEVEL_WHICH - anonymous (current) mask, CPU_LEVEL_CPUSET - assigned mask
        err = cpuset_getaffinity( CPU_LEVEL_WHICH, CPU_WHICH_PID, -1, curMaskSize, processMask );
        if ( !err || errno != ERANGE || curMaskSize * CHAR_BIT >= 16 * 1024 )
            break;
#else /* __unix__ */
        int pid = getpid();
        err = sched_getaffinity( pid, curMaskSize, processMask );
        if ( !err || errno != EINVAL || curMaskSize * CHAR_BIT >= 256 * 1024 )
             break;
#endif
        delete[] processMask;
        numMasks <<= 1;
    }
    if ( !err ) {
        // We have found the mask size and captured the process affinity mask into processMask.
        num_masks = numMasks; // do here because it's needed for affinity_helper to work
#if __unix__
        // For better coexistence with libiomp which might have changed the mask already,
        // check for its presence and ask it to restore the mask.
        dynamic_link_handle libhandle;
        if ( dynamic_link( "libiomp5.so", iompLinkTable, 1, &libhandle, DYNAMIC_LINK_GLOBAL ) ) {
            // We have found the symbol provided by libiomp5 for restoring original thread affinity.
            affinity_helper affhelp;
            affhelp.protect_affinity_mask( /*restore_process_mask=*/false );
            if ( libiomp_try_restoring_original_mask()==0 ) {
                // Now we have the right mask to capture, restored by libiomp.
                const int curMaskSize = BasicMaskSize * numMasks;
                std::memset( processMask, 0, curMaskSize );
                get_thread_affinity_mask( curMaskSize, processMask );
            } else
                affhelp.dismiss();  // thread mask has not changed
            dynamic_unlink( libhandle );
            // Destructor of affinity_helper restores the thread mask (unless dismissed).
        }
#endif
        for ( int m = 0; availableProcs < maxProcs && m < numMasks; ++m ) {
            for ( std::size_t i = 0; (availableProcs < maxProcs) && (i < BasicMaskSize * CHAR_BIT); ++i ) {
                if ( CPU_ISSET( i, processMask + m ) )
                    ++availableProcs;
            }
        }
        process_mask = processMask;
    }
    else {
        // Failed to get the process affinity mask; assume the whole machine can be used.
        availableProcs = (maxProcs == INT_MAX) ? sysconf(_SC_NPROCESSORS_ONLN) : maxProcs;
        delete[] processMask;
    }
    int num_procs = availableProcs > 0 ? availableProcs : 1; // Fail safety strap
    int cgroup_num_cpus = INT_MAX;
    if (cgroup_info::is_cpu_constrained(cgroup_num_cpus)) {
        // If cgroup is used, limit the number of processors to the constrained value.
        num_procs = std::min(num_procs, cgroup_num_cpus);
    }
    theNumProcs = num_procs;
    __TBB_ASSERT( theNumProcs <= sysconf(_SC_NPROCESSORS_ONLN), nullptr);
}

int AvailableHwConcurrency() {
    atomic_do_once( &initialize_hardware_concurrency_info, hardware_concurrency_info );
    return theNumProcs;
}

/* End of __TBB_USE_OS_AFFINITY_SYSCALL implementation */
#elif __ANDROID__

// Work-around for Android that reads the correct number of available CPUs since system calls are unreliable.
// Format of "present" file is: ([<int>-<int>|<int>],)+
int AvailableHwConcurrency() {
    FILE *fp = fopen("/sys/devices/system/cpu/present", "r");
    if (fp == nullptr) return 1;
    int num_args, lower, upper, num_cpus=0;
    while ((num_args = fscanf(fp, "%u-%u", &lower, &upper)) != EOF) {
        switch(num_args) {
            case 2: num_cpus += upper - lower + 1; break;
            case 1: num_cpus += 1; break;
        }
        fscanf(fp, ",");
    }
    fclose(fp);
    return (num_cpus > 0) ? num_cpus : 1;
}

#elif defined(_SC_NPROCESSORS_ONLN)

int AvailableHwConcurrency() {
    int n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? n : 1;
}

#elif _WIN32||_WIN64

static std::atomic<do_once_state> hardware_concurrency_info;

static const WORD TBB_ALL_PROCESSOR_GROUPS = 0xffff;

// Statically allocate an array for processor group information.
// Windows 7 supports maximum 4 groups, but let's look ahead a little.
static const WORD MaxProcessorGroups = 64;

struct ProcessorGroupInfo {
    DWORD_PTR   mask;                   ///< Affinity mask covering the whole group
    int         numProcs;               ///< Number of processors in the group
    int         numProcsRunningTotal;   ///< Subtotal of processors in this and preceding groups

    //! Total number of processor groups in the system
    static int NumGroups;

    //! Index of the group with a slot reserved for the first external thread
    /** In the context of multiple processor groups support current implementation
        defines "the first external thread" as the first thread to invoke
        AvailableHwConcurrency().

        TODO:   Implement a dynamic scheme remapping workers depending on the pending
                external threads affinity. **/
    static int HoleIndex;
};

int ProcessorGroupInfo::NumGroups = 1;
int ProcessorGroupInfo::HoleIndex = 0;

ProcessorGroupInfo theProcessorGroups[MaxProcessorGroups];
int calculate_numa[MaxProcessorGroups];  //Array needed for FindProcessorGroupIndex to calculate Processor Group when number of threads > number of cores to distribute threads evenly between processor groups
int numaSum;
struct TBB_GROUP_AFFINITY {
    DWORD_PTR Mask;
    WORD   Group;
    WORD   Reserved[3];
};

static DWORD (WINAPI *TBB_GetActiveProcessorCount)( WORD groupIndex ) = nullptr;
static WORD (WINAPI *TBB_GetActiveProcessorGroupCount)() = nullptr;
static BOOL (WINAPI *TBB_SetThreadGroupAffinity)( HANDLE hThread,
                        const TBB_GROUP_AFFINITY* newAff, TBB_GROUP_AFFINITY *prevAff );
static BOOL (WINAPI *TBB_GetThreadGroupAffinity)( HANDLE hThread, TBB_GROUP_AFFINITY* );

static const dynamic_link_descriptor ProcessorGroupsApiLinkTable[] = {
      DLD(GetActiveProcessorCount, TBB_GetActiveProcessorCount)
    , DLD(GetActiveProcessorGroupCount, TBB_GetActiveProcessorGroupCount)
    , DLD(SetThreadGroupAffinity, TBB_SetThreadGroupAffinity)
    , DLD(GetThreadGroupAffinity, TBB_GetThreadGroupAffinity)
};

static void initialize_hardware_concurrency_info () {
    suppress_unused_warning(TBB_ALL_PROCESSOR_GROUPS);
#if __TBB_WIN8UI_SUPPORT
    // For these applications processor groups info is unavailable
    // Setting up a number of processors for one processor group
    theProcessorGroups[0].numProcs = theProcessorGroups[0].numProcsRunningTotal = std::thread::hardware_concurrency();
#else /* __TBB_WIN8UI_SUPPORT */
    dynamic_link( "Kernel32.dll", ProcessorGroupsApiLinkTable,
                  sizeof(ProcessorGroupsApiLinkTable)/sizeof(dynamic_link_descriptor) );
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    DWORD_PTR pam, sam, m = 1;
    GetProcessAffinityMask( GetCurrentProcess(), &pam, &sam );
    int nproc = 0;
    for ( std::size_t i = 0; i < sizeof(DWORD_PTR) * CHAR_BIT; ++i, m <<= 1 ) {
        if ( pam & m )
            ++nproc;
    }
    int number_of_processors = (int)si.dwNumberOfProcessors;
    if (nproc > number_of_processors && TBB_GetThreadGroupAffinity) {
        // Sometimes on systems with multiple processor groups GetNativeSystemInfo
        // reports mask and processor count from the parent process
        TBB_GROUP_AFFINITY ga;
        if (TBB_GetThreadGroupAffinity(GetCurrentThread(), &ga)) {
            number_of_processors = (int)TBB_GetActiveProcessorCount(ga.Group);
        }
    }

    __TBB_ASSERT( nproc <= number_of_processors, nullptr);
    // By default setting up a number of processors for one processor group
    theProcessorGroups[0].numProcs = theProcessorGroups[0].numProcsRunningTotal = nproc;
    // Setting up processor groups in case the process does not restrict affinity mask and more than one processor group is present
    if ( nproc == number_of_processors && TBB_GetActiveProcessorCount ) {
        // The process does not have restricting affinity mask and multiple processor groups are possible
        ProcessorGroupInfo::NumGroups = (int)TBB_GetActiveProcessorGroupCount();
        __TBB_ASSERT( ProcessorGroupInfo::NumGroups <= MaxProcessorGroups, nullptr);
        // Fail safety bootstrap. Release versions will limit available concurrency
        // level, while debug ones would assert.
        if ( ProcessorGroupInfo::NumGroups > MaxProcessorGroups )
            ProcessorGroupInfo::NumGroups = MaxProcessorGroups;
        if ( ProcessorGroupInfo::NumGroups > 1 ) {
            TBB_GROUP_AFFINITY ga;
            if ( TBB_GetThreadGroupAffinity( GetCurrentThread(), &ga ) )
                ProcessorGroupInfo::HoleIndex = ga.Group;
            int nprocs = 0;
            int min_procs = INT_MAX;
            for ( WORD i = 0; i < ProcessorGroupInfo::NumGroups; ++i ) {
                ProcessorGroupInfo  &pgi = theProcessorGroups[i];
                pgi.numProcs = (int)TBB_GetActiveProcessorCount(i);
                if (pgi.numProcs < min_procs) min_procs = pgi.numProcs;  //Finding the minimum number of processors in the Processor Groups
                calculate_numa[i] = pgi.numProcs;
                __TBB_ASSERT( pgi.numProcs <= (int)sizeof(DWORD_PTR) * CHAR_BIT, nullptr);
                pgi.mask = pgi.numProcs == sizeof(DWORD_PTR) * CHAR_BIT ? ~(DWORD_PTR)0 : (DWORD_PTR(1) << pgi.numProcs) - 1;
                pgi.numProcsRunningTotal = nprocs += pgi.numProcs;
            }
            __TBB_ASSERT( nprocs == (int)TBB_GetActiveProcessorCount( TBB_ALL_PROCESSOR_GROUPS ), nullptr);

            calculate_numa[0] = (calculate_numa[0] / min_procs)-1;
            for (WORD i = 1; i < ProcessorGroupInfo::NumGroups; ++i) {
                calculate_numa[i] = calculate_numa[i-1] + (calculate_numa[i] / min_procs);
            }

            numaSum = calculate_numa[ProcessorGroupInfo::NumGroups - 1];

        }

    }
#endif /* __TBB_WIN8UI_SUPPORT */

    PrintExtraVersionInfo("Processor groups", "%d", ProcessorGroupInfo::NumGroups);
    if (ProcessorGroupInfo::NumGroups>1)
        for (int i=0; i<ProcessorGroupInfo::NumGroups; ++i)
            PrintExtraVersionInfo( "----- Group", "%d: size %d", i, theProcessorGroups[i].numProcs);
}

int NumberOfProcessorGroups() {
    __TBB_ASSERT( hardware_concurrency_info == do_once_state::initialized, "NumberOfProcessorGroups is used before AvailableHwConcurrency" );
    return ProcessorGroupInfo::NumGroups;
}

int FindProcessorGroupIndex ( int procIdx ) {
    int current_grp_idx = ProcessorGroupInfo::HoleIndex;
    if (procIdx >= theProcessorGroups[current_grp_idx].numProcs  && procIdx < theProcessorGroups[ProcessorGroupInfo::NumGroups - 1].numProcsRunningTotal) {
        procIdx = procIdx - theProcessorGroups[current_grp_idx].numProcs;
        do {
            current_grp_idx = (current_grp_idx + 1) % (ProcessorGroupInfo::NumGroups);
            procIdx = procIdx - theProcessorGroups[current_grp_idx].numProcs;

        } while (procIdx >= 0);
    }
    else if (procIdx >= theProcessorGroups[ProcessorGroupInfo::NumGroups - 1].numProcsRunningTotal) {
        int temp_grp_index = 0;
        procIdx = procIdx - theProcessorGroups[ProcessorGroupInfo::NumGroups - 1].numProcsRunningTotal; 
        procIdx = procIdx % (numaSum+1);  //ProcIdx to stay between 0 and numaSum

        while (procIdx - calculate_numa[temp_grp_index] > 0) {
            temp_grp_index = (temp_grp_index + 1) % ProcessorGroupInfo::NumGroups;
        }
        current_grp_idx = temp_grp_index;
    }
    __TBB_ASSERT(current_grp_idx < ProcessorGroupInfo::NumGroups, nullptr);

    return current_grp_idx;
}

void MoveThreadIntoProcessorGroup( void* hThread, int groupIndex ) {
    __TBB_ASSERT( hardware_concurrency_info == do_once_state::initialized, "MoveThreadIntoProcessorGroup is used before AvailableHwConcurrency" );
    if ( !TBB_SetThreadGroupAffinity )
        return;
    TBB_GROUP_AFFINITY ga = { theProcessorGroups[groupIndex].mask, (WORD)groupIndex, {0,0,0} };
    TBB_SetThreadGroupAffinity( hThread, &ga, nullptr);
}

int AvailableHwConcurrency() {
    atomic_do_once( &initialize_hardware_concurrency_info, hardware_concurrency_info );
    return theProcessorGroups[ProcessorGroupInfo::NumGroups - 1].numProcsRunningTotal;
}

/* End of _WIN32||_WIN64 implementation */
#else
    #error AvailableHwConcurrency is not implemented for this OS
#endif

} // namespace r1
} // namespace detail
} // namespace tbb

#endif /* !__TBB_HardwareConcurrency */
