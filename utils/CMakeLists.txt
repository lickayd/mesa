# Copyright (c) 2021-2022 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.15)

project(lm_utils C ASM)
enable_language(C ASM)

option(LM_UTILS_LIBRARY_TYPE_OBJECT "Create an object library instead of static/shared library" OFF)

if (${LM_TEST_ENABLE})
    include_directories(${acutest_SOURCE_DIR})
endif()

set(lm_utils_src src/lm_utils.c src/lm_utils_trace_conf.c)
if (NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    if (CMAKE_SYSTEM_PROCESSOR STREQUAL "armv8")
        include_directories(src/arm-optimized-routines/string/aarch64)
        include_directories(src/arm-optimized-routines/string/include)
        list(APPEND lm_utils_src src/arm-optimized-routines/string/aarch64/memcmp.S)
        list(APPEND lm_utils_src src/arm-optimized-routines/string/aarch64/memcpy.S)
        list(APPEND lm_utils_src src/arm-optimized-routines/string/aarch64/memset.S)

    elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7l")
        include_directories(src/arm-optimized-routines/string/arm)
        include_directories(src/arm-optimized-routines/string/include)
        list(APPEND lm_utils_src src/arm-optimized-routines/string/arm/memcpy.S)
        list(APPEND lm_utils_src src/arm-optimized-routines/string/arm/memset.S)

    endif ()
endif ()

list(APPEND lm_utils_src src/lm_utils_memcmp.c)
list(APPEND lm_utils_src src/lm_utils_memcpy.c)
list(APPEND lm_utils_src src/lm_utils_memmove.c)
list(APPEND lm_utils_src src/lm_utils_memset.c)

if (${LM_UTILS_LIBRARY_TYPE_OBJECT})
    add_library(lm_utils OBJECT ${lm_utils_src})
    target_compile_options(lm_utils PRIVATE -fPIC)
else ()
    add_library(lm_utils ${lm_utils_src})
endif ()

target_include_directories(lm_utils PRIVATE ${lm_utils_SOURCE_DIR}/include)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(lm_utils PRIVATE -DLMU_OS_LINUX)
endif ()

# Detect wheter the compiler either support __VA_OPT__ or the GNU extension for __VA_ARGS__
include(CheckCSourceCompiles)
include(CMakePushCheckState)

check_c_source_compiles([[
  #define FOO(...) __VA_OPT__(int) __VA_ARGS__
  int main(void) {
      int x = FOO() 4;
      FOO(y = 2);
      return x + y;
  }
]] HAS___VA_OPT__)

if(HAS___VA_OPT__)
    add_compile_definitions(LMU_PP_USE___VA_OPT__)
else()
    cmake_push_check_state(RESET)

    try_compile(HAS__VA_ARG__GNU_EXT
        SOURCE_FROM_CONTENT test_va_arg_gnu_ext.c [[
            #include <assert.h>
            #define SEQ() 2, 1, 0
            #define ARGN(_1, _2, N, ...) N
            #define COUNT_IMPL(_, ...) ARGN(__VA_ARGS__)
            #define COUNT(...) COUNT_IMPL(0, ##__VA_ARGS__, SEQ())
            int main(void) {
            _Static_assert(0 == COUNT());
            _Static_assert(1 == COUNT(foo));
            _Static_assert(2 == COUNT(foo, bar));
            return 0;
            }
        ]]
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS ON
        OUTPUT_VARIABLE OUTPUT
    )

    cmake_pop_check_state()

    if(NOT HAS__VA_ARG__GNU_EXT)
        message(FATAL_ERROR "${CMAKE_C_COMPILER} does not support __VA_ARG__ GNU extension.")
    else()
        set(CMAKE_C_EXTENSIONS ON)
    endif()
endif()

if (${LM_TEST_ENABLE})
    add_subdirectory(test)
endif()
