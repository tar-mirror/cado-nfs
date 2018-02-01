
# avx
message(STATUS "Testing whether avx code can be used")
if (HAVE_SSE41)
    try_run(avx_runs avx_compiles
        ${PROJECT_BINARY_DIR}/config
        ${PROJECT_SOURCE_DIR}/config/avx.c)
    if(avx_compiles)
        if (avx_runs MATCHES FAILED_TO_RUN)
            message(STATUS "Testing whether avx code can be used -- No")
            set (HAVE_AVX 0)
        else()
            message(STATUS "Testing whether avx code can be used -- Yes")
            set (HAVE_AVX 1)
        endif()
    else()
        try_run(avx_runs avx_compiles
            ${PROJECT_BINARY_DIR}/config
            ${PROJECT_SOURCE_DIR}/config/avx.c
            COMPILE_DEFINITIONS -mavx)
        if(avx_compiles)
            if (avx_runs MATCHES FAILED_TO_RUN)
                message(STATUS "Testing whether avx code can be used -- No")
                set (HAVE_AVX 0)
            else()
                message(STATUS "Testing whether avx code can be used -- Yes, with -mavx")
                set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mavx")
                set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx")
                set (HAVE_AVX 1)
            endif()
        else()
            message(STATUS "Testing whether avx code can be used -- No")
            set (HAVE_AVX 0)
        endif()
    endif()
else()
message(STATUS "Testing whether avx code can be used -- skipped")
endif()
