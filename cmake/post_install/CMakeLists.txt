# Copyright (c) 2023 Intel Corporation

# Add code signing as post-install step.
if (DEFINED TCM_SIGNTOOL)
    file(TO_CMAKE_PATH "${TCM_SIGNTOOL}" TCM_SIGNTOOL)
    install(CODE "
    file(GLOB_RECURSE FILES_TO_SIGN \${CMAKE_INSTALL_PREFIX}/*tcm*${CMAKE_SHARED_LIBRARY_SUFFIX})
    execute_process(COMMAND ${TCM_SIGNTOOL} \${FILES_TO_SIGN} ${TCM_SIGNTOOL_ARGS})
")
endif()
