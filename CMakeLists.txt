# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.24)
project(SMLibGeometry VERSION 1.0.0 LANGUAGES CXX)

option(SMLIB_BUILD_PYTHON "Build the stable geometry Python extension" ON)
option(SMLIB_BUILD_DEV "Build debugger extensions for the optional GUI" OFF)
option(SMLIB_BUILD_VALIDATION "Build native and Python validation tools" OFF)
option(SMLIB_BUILD_EXAMPLES "Build SDK examples" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
# A flat runtime directory makes native/Python consumers independent of source
# layout. Each build tree owns its outputs; no old SDK DLLs are reused.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
set(CMAKE_INSTALL_RPATH "$ORIGIN")

add_library(smlib_build_options INTERFACE)
# These macros affect kernel headers and must match in every native consumer.
target_compile_definitions(smlib_build_options INTERFACE NOMINMAX SM_GRAPHICS_CALLBACKS=1
    $<$<CONFIG:Debug>:SM_DEBUG_CODE=1;SM_GFX_CODE=1>
    $<$<PLATFORM_ID:Windows>:UNICODE;_UNICODE;NL_DEBUG_CODE>)
if(MSVC)
    target_compile_options(smlib_build_options INTERFACE /W4 /WX /utf-8 /bigobj /wd4244 /wd4305 /wd4267)
    target_compile_definitions(smlib_build_options INTERFACE _SCL_SECURE_NO_WARNINGS)
else()
    target_compile_options(smlib_build_options INTERFACE -Wall -Werror -Wno-write-strings)
endif()

file(GLOB kernel_sources CONFIGURE_DEPENDS source/SMLib/src/*.cpp)
add_library(SMLib SHARED ${kernel_sources})
target_include_directories(SMLib PUBLIC source/SMLib/inc)
target_link_libraries(SMLib PUBLIC smlib_build_options)
# The existing kernel's TBB paths are compile-time opt-ins, disabled here just
# as in stock sources. The default SDK uses its native allocation/serial paths.
if(MSVC)
    target_precompile_headers(SMLib PRIVATE source/SMLib/inc/StdAfx.h)
endif()

file(GLOB api_sources CONFIGURE_DEPENDS source/SM_API/src/*.cpp)
add_library(SM_API SHARED ${api_sources})
target_include_directories(SM_API PUBLIC source/SM_API/inc)
target_compile_definitions(SM_API PRIVATE SM_API_EXPORTS)
target_link_libraries(SM_API PUBLIC SMLib)
if(MSVC)
    target_precompile_headers(SM_API PRIVATE source/SM_API/inc/StdAfx.h)
endif()

# Standalone STEP subprojects keep their existing default tests. The SDK build
# makes those tests and inspection CLIs explicit choices instead.
set(STEP_BUILD_TOOLS ${SMLIB_BUILD_VALIDATION})
foreach(layer SOURCE SEMANTICS PLANAR IMPORT)
    set(STEP_${layer}_BUILD_TESTS ${SMLIB_BUILD_VALIDATION})
endforeach()
if(SMLIB_BUILD_VALIDATION)
    enable_testing()
endif()
add_subdirectory(source/STEP_IMPORT)
foreach(layer SOURCE SEMANTICS PLANAR IMPORT)
    target_link_libraries(STEP_${layer} PRIVATE smlib_build_options)
endforeach()

if(SMLIB_BUILD_PYTHON OR SMLIB_BUILD_DEV)
    # Reuse fetched headers/interpreter when available; a caller may instead
    # supply Python3_ROOT_DIR and pybind11_DIR for a standalone SDK build.
    if(NOT DEFINED Python3_ROOT_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/_build/target-deps/python")
        set(Python3_ROOT_DIR "${CMAKE_SOURCE_DIR}/_build/target-deps/python")
    endif()
    find_package(Python3 3.12 EXACT REQUIRED COMPONENTS Interpreter Development.Module)
    set(PYBIND11_FINDPYTHON ON)
    if(EXISTS "${CMAKE_SOURCE_DIR}/_build/target-deps/pybind11/include/pybind11/pybind11.h")
        add_subdirectory("${CMAKE_SOURCE_DIR}/_build/target-deps/pybind11" "pybind11" EXCLUDE_FROM_ALL)
    else()
        find_package(pybind11 CONFIG REQUIRED)
    endif()
endif()

if(SMLIB_BUILD_PYTHON)
    file(GLOB binding_sources CONFIGURE_DEPENDS source/SmPyLib/src/*.cpp)
    pybind11_add_module(_omni_solid MODULE NO_EXTRAS ${binding_sources})
    target_link_libraries(_omni_solid PRIVATE SM_API)
    add_custom_command(TARGET _omni_solid POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/source/SmPyLib/python/usd_brep"
            "$<TARGET_FILE_DIR:_omni_solid>/usd_brep"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/source/SmPyLib/stubs/usd_brep"
            "$<TARGET_FILE_DIR:_omni_solid>/usd_brep")
endif()

if(SMLIB_BUILD_DEV OR SMLIB_BUILD_VALIDATION)
    file(GLOB prog_sources CONFIGURE_DEPENDS tests/prog_test/src/*.cpp)
    list(REMOVE_ITEM prog_sources "${CMAKE_SOURCE_DIR}/tests/prog_test/src/prog_test_app.cpp")
    add_library(prog_test SHARED ${prog_sources})
    target_include_directories(prog_test PUBLIC tests/prog_test/inc)
    target_compile_definitions(prog_test PRIVATE _CRT_SECURE_NO_WARNINGS SM_CLASSIFY_UPDATE SM_TOLERANT_WATCH)
    target_link_libraries(prog_test PUBLIC SMLib)
    if(MSVC)
        target_compile_options(prog_test PRIVATE /MP4)
    endif()
    add_executable(prog_test_app tests/prog_test/src/prog_test_app.cpp)
    target_link_libraries(prog_test_app PRIVATE prog_test)
endif()
if(SMLIB_BUILD_DEV)
    if(NOT SMLIB_BUILD_PYTHON)
        message(FATAL_ERROR "SMLIB_BUILD_DEV requires SMLIB_BUILD_PYTHON for shared Python type registration")
    endif()
    file(GLOB dev_sources CONFIGURE_DEPENDS source/SmPyDevLib/src/*.cpp)
    pybind11_add_module(_smlib_dev MODULE NO_EXTRAS ${dev_sources} source/SmPyLib/src/SmPyError.cpp)
    target_include_directories(_smlib_dev PRIVATE source/SmPyLib/src)
    target_link_libraries(_smlib_dev PRIVATE SM_API)
    file(GLOB test_binding_sources CONFIGURE_DEPENDS source/SmPyLib/tests_binding/*.cpp)
    pybind11_add_module(_smlib_tests MODULE NO_EXTRAS ${test_binding_sources})
    target_link_libraries(_smlib_tests PRIVATE prog_test)
endif()
if(SMLIB_BUILD_VALIDATION)
    add_executable(planar_regions tests/planar_regions/regions.cpp)
    target_link_libraries(planar_regions PRIVATE SM_API)
    add_test(NAME planar_regions COMMAND planar_regions)
    file(GLOB api_test_sources CONFIGURE_DEPENDS tests/SM_API_test/src/*.cpp)
    add_executable(SM_API_test_app ${api_test_sources}
        Examples/SM_API/boolean_bounds.cpp Examples/SMLib/step_derivatives.cpp)
    target_include_directories(SM_API_test_app PRIVATE tests/SM_API_test/inc Examples/SM_API Examples/SMLib)
    target_compile_definitions(SM_API_test_app PRIVATE SM_API_TEST_STANDALONE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(SM_API_test_app PRIVATE SM_API)
    add_test(NAME sm_api COMMAND SM_API_test_app)
    set_tests_properties(sm_api PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/tests/SM_API_test")
    if(SMLIB_BUILD_PYTHON)
        add_test(NAME python_geometry COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/tools/agent/run_geometry_tests.py" --runtime "$<TARGET_FILE_DIR:_omni_solid>")
    endif()
endif()
if(SMLIB_BUILD_EXAMPLES)
    add_executable(sm_api_boolean_bounds_example Examples/SM_API/boolean_bounds.cpp)
    target_compile_definitions(sm_api_boolean_bounds_example PRIVATE SM_BOOLEAN_BOUNDS_EXAMPLE_STANDALONE)
    target_link_libraries(sm_api_boolean_bounds_example PRIVATE SM_API)
    add_executable(smlib_step_derivatives_example Examples/SMLib/step_derivatives.cpp)
    target_compile_definitions(smlib_step_derivatives_example PRIVATE SM_STEP_DERIVATIVES_EXAMPLE_STANDALONE)
    target_link_libraries(smlib_step_derivatives_example PRIVATE SM_API)
endif()
