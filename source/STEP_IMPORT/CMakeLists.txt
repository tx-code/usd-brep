# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.22)
project(STEP_IMPORT LANGUAGES CXX)
enable_testing()
add_subdirectory(../STEP_PLANAR source-planar)

set(STEP_IMPORT_NATIVE_DIR "" CACHE PATH "Matching SMLib/SM_API SDK with release/ and debug/ libraries and DLLs")
if(NOT TARGET SMLib OR NOT TARGET SM_API)
    if(NOT MSVC OR NOT STEP_IMPORT_NATIVE_DIR)
        message(FATAL_ERROR "Supply matching SMLib/SM_API targets, or STEP_IMPORT_NATIVE_DIR for the Windows SDK")
    endif()
    foreach(native SMLib SM_API)
        add_library(${native} SHARED IMPORTED)
        set_target_properties(${native} PROPERTIES IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
            MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE)
        foreach(config RELEASE DEBUG)
            string(TOLOWER "${config}" folder)
            set_target_properties(${native} PROPERTIES
                IMPORTED_IMPLIB_${config} "${STEP_IMPORT_NATIVE_DIR}/${folder}/${native}.lib"
                IMPORTED_LOCATION_${config} "${STEP_IMPORT_NATIVE_DIR}/${folder}/${native}.dll")
        endforeach()
    endforeach()
endif()

add_library(STEP_IMPORT STATIC src/StepImport.cpp src/StepImportExecution.cpp src/StepImportPlan.cpp src/StepImportNative.cpp src/StepImportReport.cpp)
target_compile_features(STEP_IMPORT PUBLIC cxx_std_20)
target_include_directories(STEP_IMPORT PUBLIC inc)
target_include_directories(STEP_IMPORT SYSTEM PRIVATE ../SMLib/inc ../SM_API/inc)
target_link_libraries(STEP_IMPORT PUBLIC STEP_PLANAR PRIVATE SM_API SMLib)
if(WIN32)
    target_link_libraries(STEP_IMPORT PRIVATE Psapi)
endif()
target_compile_definitions(STEP_IMPORT PRIVATE NOMINMAX UNICODE _UNICODE SM_GRAPHICS_CALLBACKS=1 NL_DEBUG_CODE
    $<$<CONFIG:Debug>:SM_DEBUG_CODE=1;SM_GFX_CODE=1>)
if(MSVC)
    target_compile_options(STEP_IMPORT PRIVATE /W4 /WX /permissive- /utf-8 /fp:strict)
else()
    target_compile_options(STEP_IMPORT PRIVATE -fno-fast-math -ffp-contract=off)
endif()

add_executable(step_native_import app/step_import.cpp)
target_link_libraries(step_native_import PRIVATE STEP_IMPORT)

option(STEP_IMPORT_BUILD_TESTS "Build native importer interface contracts" ON)
if(STEP_IMPORT_BUILD_TESTS)
    add_executable(step_import_tests tests/StepImportTests.cpp)
    target_link_libraries(step_import_tests PRIVATE STEP_IMPORT)
    add_test(NAME step_import_contracts COMMAND step_import_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/step_import_validation/corpus/ap203e2-planar/v1/cases")
    add_executable(step_import_hole_tests tests/StepImportHoleTests.cpp)
    target_link_libraries(step_import_hole_tests PRIVATE STEP_IMPORT)
    add_test(NAME step_import_hole_contracts COMMAND step_import_hole_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/step_import_validation/corpus/ap203e2-planar/v1/cases")
    add_executable(step_import_box_tests tests/StepImportBoxTests.cpp)
    target_link_libraries(step_import_box_tests PRIVATE STEP_IMPORT)
    add_test(NAME step_import_box_contracts COMMAND step_import_box_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/step_import_validation/corpus/ap203e2-planar/v1/cases")
    add_executable(step_import_surface_tests tests/StepImportSurfaceTests.cpp)
    target_link_libraries(step_import_surface_tests PRIVATE STEP_IMPORT)
    add_test(NAME step_import_surface_contracts COMMAND step_import_surface_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/step_import_validation/corpus/ap203e2-planar/v1/cases")
    add_executable(step_import_unit_tests tests/StepImportUnitTests.cpp)
    target_link_libraries(step_import_unit_tests PRIVATE STEP_IMPORT)
    add_test(NAME step_import_unit_contracts COMMAND step_import_unit_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/step_import_validation/corpus/ap203e2-planar/v1/cases")
    add_executable(step_import_declaration_tests tests/StepImportDeclarationTests.cpp)
    target_link_libraries(step_import_declaration_tests PRIVATE STEP_IMPORT)
    add_test(NAME step_import_declaration_contracts COMMAND step_import_declaration_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/step_import_validation/corpus/ap203e2-planar/v1/cases")
    add_executable(step_import_lifecycle_tests tests/StepImportLifecycleTests.cpp)
    target_link_libraries(step_import_lifecycle_tests PRIVATE STEP_IMPORT)
    add_test(NAME step_import_lifecycle_contracts COMMAND step_import_lifecycle_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/step_import_validation/corpus/ap203e2-planar/v1/cases")
    add_executable(step_import_copy_tests tests/StepImportCopyTests.cpp)
    target_link_libraries(step_import_copy_tests PRIVATE STEP_IMPORT)
    target_include_directories(step_import_copy_tests SYSTEM PRIVATE ../SMLib/inc ../SM_API/inc)
    target_compile_definitions(step_import_copy_tests PRIVATE NOMINMAX UNICODE _UNICODE SM_GRAPHICS_CALLBACKS=1 NL_DEBUG_CODE
        $<$<CONFIG:Debug>:SM_DEBUG_CODE=1;SM_GFX_CODE=1>)
    add_test(NAME step_import_copy_contracts COMMAND step_import_copy_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/step_import_validation/corpus/ap203e2-planar/v1/cases")
    add_executable(step_import_sheet_tests tests/StepImportSheetTests.cpp)
    target_link_libraries(step_import_sheet_tests PRIVATE STEP_IMPORT)
    target_include_directories(step_import_sheet_tests SYSTEM PRIVATE ../SMLib/inc ../SM_API/inc)
    target_compile_definitions(step_import_sheet_tests PRIVATE NOMINMAX UNICODE _UNICODE SM_GRAPHICS_CALLBACKS=1 NL_DEBUG_CODE
        $<$<CONFIG:Debug>:SM_DEBUG_CODE=1;SM_GFX_CODE=1>)
    add_test(NAME step_import_sheet_contracts COMMAND step_import_sheet_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/open_shared_sheet.step")
    add_test(NAME step_import_coplanar_sheet_contracts COMMAND step_import_sheet_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/coplanar_shared_sheet.step" coplanar)
    foreach(shape convex triangular pentagonal concave collinear)
        add_test(NAME step_import_${shape}_sheet_contracts COMMAND step_import_sheet_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/${shape}_shared_sheet.step" ${shape})
        set_tests_properties(step_import_${shape}_sheet_contracts PROPERTIES TIMEOUT 60)
    endforeach()
    set_tests_properties(step_import_sheet_contracts step_import_coplanar_sheet_contracts PROPERTIES TIMEOUT 60)
    if(WIN32)
        add_executable(step_import_resources tests/StepImportResources.cpp)
        target_compile_definitions(step_import_resources PRIVATE NOMINMAX)
        target_link_libraries(step_import_resources PRIVATE STEP_IMPORT Psapi)
        add_test(NAME step_import_resources COMMAND step_import_resources
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/step_import_validation/corpus/ap203e2-planar/v1/cases/S203-P01/input.step")
        add_test(NAME step_import_hole_resources COMMAND step_import_resources
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/step_import_validation/corpus/ap203e2-planar/v1/cases/S203-P03/input.step")
        add_test(NAME step_import_box_resources COMMAND step_import_resources
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/step_import_validation/corpus/ap203e2-planar/v1/cases/S203-P04/input.step")
        add_test(NAME step_import_surface_resources COMMAND step_import_resources
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/step_import_validation/corpus/ap203e2-planar/v1/cases/S203-P05/input.step")
        add_test(NAME step_import_unit_resources COMMAND step_import_resources
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/step_import_validation/corpus/ap203e2-planar/v1/cases/S203-P11/input.step")
        add_test(NAME step_import_declaration_resources COMMAND step_import_resources
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/step_import_validation/corpus/ap203e2-planar/v1/cases/S203-P08/input.step")
        add_test(NAME step_import_lifecycle_resources COMMAND step_import_resources
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/step_import_validation/corpus/ap203e2-planar/v1/cases/S203-P01/input.step" lifecycle)
        add_test(NAME step_import_sheet_resources COMMAND step_import_resources
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/open_shared_sheet.step")
        add_test(NAME step_import_sheet_lifecycle_resources COMMAND step_import_resources
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/open_shared_sheet.step" lifecycle)
        add_test(NAME step_import_coplanar_sheet_resources COMMAND step_import_resources
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/coplanar_shared_sheet.step")
        add_test(NAME step_import_coplanar_sheet_lifecycle_resources COMMAND step_import_resources
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/coplanar_shared_sheet.step" lifecycle)
        foreach(shape convex triangular pentagonal concave collinear)
            add_test(NAME step_import_${shape}_sheet_resources COMMAND step_import_resources
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/${shape}_shared_sheet.step")
            add_test(NAME step_import_${shape}_sheet_lifecycle_resources COMMAND step_import_resources
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/${shape}_shared_sheet.step" lifecycle)
        endforeach()
    endif()
endif()
if(TARGET step_import_lifecycle_tests)
    set_tests_properties(step_import_lifecycle_contracts step_import_copy_contracts PROPERTIES TIMEOUT 60)
endif()

foreach(target step_native_import step_import_tests step_import_resources step_import_hole_tests step_import_box_tests step_import_surface_tests step_import_unit_tests step_import_declaration_tests step_import_lifecycle_tests step_import_copy_tests step_import_sheet_tests)
    if(TARGET ${target})
        if(WIN32)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SMLib>" "$<TARGET_FILE_DIR:${target}>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SM_API>" "$<TARGET_FILE_DIR:${target}>")
        endif()
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
    endif()
endforeach()
