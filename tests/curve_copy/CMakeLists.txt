# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(CurveCopyContracts LANGUAGES CXX)
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
add_executable(curve_copy_contracts curve_copy.cpp controls.cpp)
target_compile_features(curve_copy_contracts PRIVATE cxx_std_20)
target_link_libraries(curve_copy_contracts PRIVATE SMLib)
target_compile_definitions(curve_copy_contracts PRIVATE NOMINMAX SM_GRAPHICS_CALLBACKS=1
    $<$<CONFIG:Debug>:SM_DEBUG_CODE=1;SM_GFX_CODE=1>
    $<$<PLATFORM_ID:Windows>:UNICODE;_UNICODE;NL_DEBUG_CODE>)
if(MSVC)
    target_compile_options(curve_copy_contracts PRIVATE /W4 /WX /permissive- /utf-8)
endif()
foreach(mode IN ITEMS step_surface step_surface_borrowed
    wrapper_base_error wrapper_base_candidate_error wrapper_base_null wrapper_base_alias
    wrapper_base_wrong_context wrapper_base_wrong_type wrapper_base_throw
    surface_error uv_error surface_candidate_error uv_candidate_error surface_alias uv_alias surface_throw uv_throw
    ownership_0 ownership_1 ownership_2 ownership_3 definition_cold definition_warm)
    add_test(NAME curve_copy_${mode} COMMAND curve_copy_contracts ${mode})
    set_tests_properties(curve_copy_${mode} PROPERTIES TIMEOUT 30)
    if(WIN32 AND SMLIB_RUNTIME_DIR)
        set_tests_properties(curve_copy_${mode} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${SMLIB_RUNTIME_DIR}")
    endif()
endforeach()
foreach(mode IN ITEMS wrapper_archive_binary wrapper_archive_ascii)
    add_test(NAME curve_copy_${mode} COMMAND curve_copy_contracts ${mode} "${CMAKE_CURRENT_BINARY_DIR}/output/$<CONFIG>")
    set_tests_properties(curve_copy_${mode} PROPERTIES TIMEOUT 30)
    if(WIN32 AND SMLIB_RUNTIME_DIR)
        set_tests_properties(curve_copy_${mode} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${SMLIB_RUNTIME_DIR}")
    endif()
endforeach()
