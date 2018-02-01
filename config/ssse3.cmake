
# SSSE-3
message(STATUS "Testing whether ssse-3 code can be used")
if (HAVE_SSE3)
    try_run(ssse3_runs ssse3_compiles
        ${PROJECT_BINARY_DIR}/config
        ${PROJECT_SOURCE_DIR}/config/ssse3.c)
    if(ssse3_compiles)
        if (ssse3_runs MATCHES FAILED_TO_RUN)
            message(STATUS "Testing whether ssse-3 code can be used -- No")
            set (HAVE_SSSE3 0)
        else()
            message(STATUS "Testing whether ssse-3 code can be used -- Yes")
            set (HAVE_SSSE3 1)
        endif()
    else()
        try_run(ssse3_runs ssse3_compiles
            ${PROJECT_BINARY_DIR}/config
            ${PROJECT_SOURCE_DIR}/config/ssse3.c
            COMPILE_DEFINITIONS -mssse3)
        if(ssse3_compiles)
            if (ssse3_runs MATCHES FAILED_TO_RUN)
                message(STATUS "Testing whether ssse-3 code can be used -- No")
                set (HAVE_SSSE3 0)
            else()
                message(STATUS "Testing whether ssse-3 code can be used -- Yes, with -mssse3")
                set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mssse3")
                set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mssse3")
                set (HAVE_SSSE3 1)
            endif()
        else()
            message(STATUS "Testing whether ssse-3 code can be used -- No")
            set (HAVE_SSSE3 0)
        endif()
    endif()
else()
    message(STATUS "Testing whether ssse-3 code can be used -- skipped")
endif()
