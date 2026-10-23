# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(TorusValidationContracts LANGUAGES CXX)
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
add_executable(torus_validation torus.cpp)
target_compile_features(torus_validation PRIVATE cxx_std_20)
target_link_libraries(torus_validation PRIVATE SMLib)
target_compile_definitions(torus_validation PRIVATE NOMINMAX SM_GRAPHICS_CALLBACKS=1
    $<$<CONFIG:Debug>:SM_DEBUG_CODE=1;SM_GFX_CODE=1>
    $<$<PLATFORM_ID:Windows>:UNICODE;_UNICODE;NL_DEBUG_CODE>)
if(MSVC)
    target_compile_options(torus_validation PRIVATE /W4 /WX /permissive- /utf-8)
endif()
add_test(NAME torus_analytic_domain_contracts COMMAND torus_validation)
set_tests_properties(torus_analytic_domain_contracts PROPERTIES TIMEOUT 60)
if(WIN32 AND SMLIB_RUNTIME_DIR)
    set_tests_properties(torus_analytic_domain_contracts PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${SMLIB_RUNTIME_DIR}")
endif()
