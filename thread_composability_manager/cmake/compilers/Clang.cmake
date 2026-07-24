# Copyright (c) 2023 Intel Corporation
# Copyright (c) 2026 UXL Foundation Contributors
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Depfile options (e.g. -MD) are inserted automatically in some cases.
# Don't add -MMD to avoid conflicts in such cases.
if (NOT CMAKE_GENERATOR MATCHES "Ninja" AND NOT CMAKE_CXX_DEPENDS_USE_COMPILER)
    set(TCM_MMD_FLAG -MMD)
endif()

set(TCM_WARNING_LEVEL -Wall -Wextra -Wpedantic $<$<BOOL:${TCM_STRICT}>:-Werror>)
set(TCM_TEST_WARNING_FLAGS
    -Wshadow -Wcast-qual -Woverloaded-virtual -Wnon-virtual-dtor -Wno-error=deprecated-declarations)

set(TCM_COMPILE_DEFINITIONS "")
set(TCM_COMMON_COMPILE_FLAGS ${TCM_COMMON_COMPILE_FLAGS} $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>
    -Wformat -Wformat-security -Werror=format-security -fPIC -fstack-protector-strong)

if (NOT APPLE AND NOT ANDROID_PLATFORM AND CMAKE_SYSTEM_PROCESSOR MATCHES "(AMD64|amd64|i.86|x86)" AND NOT WIN32)
    set(TCM_LIB_COMPILE_FLAGS ${TCM_LIB_COMPILE_FLAGS} -fstack-clash-protection)
    if (NOT CF_PROTECTION_FULL_SUPPORTED)
        include(CheckCXXSourceCompiles)
        set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")
        set(CMAKE_REQUIRED_FLAGS "-fcf-protection=full")
        check_cxx_source_compiles("int main(int, char*[]) { return 0; }" CF_PROTECTION_FULL_SUPPORTED)
        unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
        unset(CMAKE_REQUIRED_FLAGS)
    endif()
    if (CF_PROTECTION_FULL_SUPPORTED)
      set(TCM_LIB_COMPILE_FLAGS ${TCM_LIB_COMPILE_FLAGS} -fcf-protection=full)
    else()
      message(WARNING "Compiler does not support -fcf-protection=full.")
    endif()
endif()

set(TCM_LIB_COMPILE_FLAGS ${TCM_LIB_COMPILE_FLAGS} $<$<NOT:$<CONFIG:Debug>>:-flto>)

set(TCM_LIB_LINK_FLAGS ${TCM_LIB_LINK_FLAGS} $<$<NOT:$<CONFIG:Debug>>:-flto> -Wl,-z,relro,-z,now,-z,noexecstack)
if (WIN32)
   # The "/DEPENDENTLOADFLAG:0x2000" restricts the loader to look for dependencies in current
   # working directory only if it is in the so-called "Safe load list".
   set(TCM_LIB_LINK_FLAGS ${TCM_LIB_LINK_FLAGS}
       $<$<NOT:$<VERSION_LESS:${CMAKE_CXX_COMPILER_VERSION},18.0>>:-Xlinker /DEPENDENTLOADFLAG:0x2000>)
endif()

set(TCM_COMMON_LINK_LIBS ${CMAKE_DL_LIBS})
