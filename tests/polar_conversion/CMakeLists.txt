# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(PolarConversionContracts LANGUAGES CXX)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    get_filename_component(SMLIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    set(SMLIB_LIBRARY_DIR "" CACHE PATH "Matching SMLib libraries")
    set(SMLIB_RUNTIME_DIR "${SMLIB_LIBRARY_DIR}" CACHE PATH "Matching runtime libraries")
    find_library(SMLib_LIBRARY NAMES SMLib PATHS "${SMLIB_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)
    add_library(SMLib UNKNOWN IMPORTED)
    set_target_properties(SMLib PROPERTIES IMPORTED_LOCATION "${SMLib_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SMLIB_ROOT}/source/SMLib/inc")
    enable_testing()
endif()
add_executable(polar_conversion polar.cpp)
target_compile_features(polar_conversion PRIVATE cxx_std_20)
target_link_libraries(polar_conversion PRIVATE SMLib)
target_compile_definitions(polar_conversion PRIVATE NOMINMAX SM_GRAPHICS_CALLBACKS=1
    $<$<CONFIG:Debug>:SM_DEBUG_CODE=1;SM_GFX_CODE=1>
    $<$<PLATFORM_ID:Windows>:UNICODE;_UNICODE;NL_DEBUG_CODE>)
if(MSVC)
    target_compile_options(polar_conversion PRIVATE /W4 /WX /permissive- /utf-8)
endif()
add_test(NAME polar_conversion_endpoints COMMAND polar_conversion)
set_tests_properties(polar_conversion_endpoints PROPERTIES TIMEOUT 60)
if(WIN32 AND SMLIB_RUNTIME_DIR)
    set_tests_properties(polar_conversion_endpoints PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${SMLIB_RUNTIME_DIR}")
endif()
