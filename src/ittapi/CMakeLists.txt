include(FetchContent)

# Fetch the ittapi library if it doesn't exist
if(NOT TARGET ittapi::ittnotify)
        FetchContent_Declare(
                ittapi
                # https://github.com/intel/ittapi/archive/refs/tags/v3.25.1.tar.gz
                URL ${CMAKE_CURRENT_SOURCE_DIR}/ittapi-3.25.1.tar.gz
                URL_HASH
                        SHA256=866a5a75a287a7440760146f99bd1093750c3fb5bf572c3bff2d4795628ebc7c
        )
        FetchContent_MakeAvailable(ittapi)
        add_library(ittapi::ittnotify ALIAS ittnotify)
endif()

if(NOT TARGET ittapi::ittnotify)
        message(
                ERROR
                "Failed to add Intel® Instrumentation and Tracing Technology (ITT) library"
        )
endif()