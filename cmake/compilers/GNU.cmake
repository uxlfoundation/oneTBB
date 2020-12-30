# Copyright (c) 2020 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

if(MINGW)
    set(TBB_LINK_DEF_FILE_FLAG "")
    set(TBB_DEF_FILE_PREFIX win${TBB_ARCH}-mingw)
else()
    set(TBB_LINK_DEF_FILE_FLAG -Wl,--version-script=)
    set(TBB_DEF_FILE_PREFIX lin${TBB_ARCH})
endif()

set(TBB_WARNING_LEVEL -Wall -Wextra $<$<BOOL:${TBB_STRICT}>:-Werror> -Wfatal-errors)
set(TBB_TEST_WARNING_FLAGS -Wshadow -Wcast-qual -Woverloaded-virtual -Wnon-virtual-dtor)

set(TBB_MMD_FLAG -MMD)
if (CMAKE_SYSTEM_PROCESSOR MATCHES "(amd64|AMD64|x86_64)")
    set(TBB_COMMON_COMPILE_FLAGS -mrtm)
    check_cxx_compiler_flag(-mwaitpkg WAITPKG_SUPPORTED)
    if (WAITPKG_SUPPORTED)
        set(TBB_LIB_COMPILE_FLAGS -mwaitpkg)
    endif()
endif()

if(UNIX)
    set(TBB_COMMON_LINK_LIBS dl)
endif()

# Ignore -Werror set through add_compile_options() or added to CMAKE_CXX_FLAGS if TBB_STRICT is disabled.
if (NOT TBB_STRICT AND COMMAND tbb_remove_compile_flag)
    tbb_remove_compile_flag(-Werror)
endif()

if (NOT ${CMAKE_CXX_COMPILER_ID} STREQUAL Intel)
    # gcc 6.0 and later have -flifetime-dse option that controls elimination of stores done outside the object lifetime
    set(TBB_DSE_FLAG $<$<NOT:$<VERSION_LESS:${CMAKE_CXX_COMPILER_VERSION},6.0>>:-flifetime-dse=1>)
endif()

if (NOT APPLE)
    set(TBB_WARNING_SUPPRESS -Wno-parentheses)
    # gcc 5.0 and later have -Wno-sized-deallocation options
    set(TBB_WARNING_SUPPRESS ${TBB_WARNING_SUPPRESS}
                             $<$<NOT:$<VERSION_LESS:${CMAKE_CXX_COMPILER_VERSION},5.0>>:-Wno-sized-deallocation>)
else()
    set(TBB_WARNING_SUPPRESS -Wno-non-virtual-dtor)
endif()

# Workaround for heavy tests
if ("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "mips")
    set(TBB_TEST_COMPILE_FLAGS ${TBB_TEST_COMPILE_FLAGS} -DTBB_TEST_LOW_WORKLOAD)
endif()

if (MINGW)
    set(TBB_COMMON_COMPILE_FLAGS ${TBB_COMMON_COMPILE_FLAGS} -U __STRICT_ANSI__ -D_WIN32_WINNT=0x0601)
    set(TBB_TEST_COMPILE_FLAGS ${TBB_TEST_COMPILE_FLAGS} -Wa,-mbig-obj)
endif()

# TBB malloc settings
set(TBBMALLOC_LIB_COMPILE_FLAGS -fno-rtti -fno-exceptions)
set(TBB_OPENMP_FLAG -fopenmp)
