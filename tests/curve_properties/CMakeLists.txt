# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.24)
    project(CurveProperties LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    get_filename_component(SMLIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    option(CURVE_HARNESS_BUILD_KERNEL "Build an isolated kernel from this source tree" OFF)
    if(CURVE_HARNESS_BUILD_KERNEL)
        file(GLOB kernel_sources CONFIGURE_DEPENDS "${SMLIB_ROOT}/source/SMLib/src/*.cpp")
        add_library(SMLib SHARED ${kernel_sources})
        target_include_directories(SMLib PUBLIC "${SMLIB_ROOT}/source/SMLib/inc")
        target_compile_features(SMLib PUBLIC cxx_std_20)
        target_compile_definitions(SMLib PUBLIC NOMINMAX SM_GRAPHICS_CALLBACKS=1
            $<$<CONFIG:Debug>:SM_DEBUG_CODE=1;SM_GFX_CODE=1>
            $<$<PLATFORM_ID:Windows>:UNICODE;_UNICODE;NL_DEBUG_CODE>)
        if(MSVC)
            target_compile_options(SMLib PUBLIC /W4 /WX /utf-8 /bigobj /wd4244 /wd4305 /wd4267)
            target_compile_definitions(SMLib PUBLIC _SCL_SECURE_NO_WARNINGS)
            target_precompile_headers(SMLib PRIVATE "${SMLIB_ROOT}/source/SMLib/inc/StdAfx.h")
        else()
            target_compile_options(SMLib PUBLIC -Wall -Werror -Wno-write-strings)
        endif()
        set_target_properties(SMLib PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
    else()
        set(SMLIB_LIBRARY_DIR "" CACHE PATH "Directory containing matching SMLib libraries")
        set(SMLIB_RUNTIME_DIR "${SMLIB_LIBRARY_DIR}" CACHE PATH "Matching native runtime")
        find_library(SMLIB_LINK_LIBRARY NAMES SMLib PATHS "${SMLIB_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)
        add_library(SMLib INTERFACE)
        target_include_directories(SMLib INTERFACE "${SMLIB_ROOT}/source/SMLib/inc")
        target_link_libraries(SMLib INTERFACE "${SMLIB_LINK_LIBRARY}")
        target_compile_definitions(SMLib INTERFACE NOMINMAX SM_GRAPHICS_CALLBACKS=1
            $<$<CONFIG:Debug>:SM_DEBUG_CODE=1;SM_GFX_CODE=1>
            $<$<PLATFORM_ID:Windows>:UNICODE;_UNICODE;NL_DEBUG_CODE>)
    endif()
    enable_testing()
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
endif()

add_executable(curve_property_probe probe.cpp)
target_link_libraries(curve_property_probe PRIVATE SMLib)
set_target_properties(curve_property_probe PROPERTIES BUILD_RPATH "$ORIGIN")
# Native-only SDK builds find the interpreter later in the parent CMake file.
if(NOT Python3_EXECUTABLE)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
endif()
add_test(NAME curve_properties COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/harness.py" check --probe "$<TARGET_FILE:curve_property_probe>")
set_tests_properties(curve_properties PROPERTIES TIMEOUT 120)
add_test(NAME curve_property_oracles COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_harness.py -v)
if(WIN32 AND SMLIB_RUNTIME_DIR)
    set_tests_properties(curve_properties PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${SMLIB_RUNTIME_DIR}")
endif()
