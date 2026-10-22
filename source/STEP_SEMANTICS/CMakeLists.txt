# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
project(STEP_SEMANTICS LANGUAGES CXX)
enable_testing()
add_subdirectory(../STEP_SOURCE source-reader)
add_library(STEP_SEMANTICS STATIC src/StepCatalog.cpp src/StepSemantics.cpp src/StepSemanticJson.cpp src/StepSchemaSelection.cpp
    src/StepGeometryDependencies.cpp src/StepSourceNumber.cpp src/StepUnitDependencies.cpp src/StepUnitRational.cpp)
target_sources(STEP_SEMANTICS PRIVATE src/StepSourceBindings.cpp src/StepProductStructure.cpp src/StepIndependentProducts.cpp
    src/StepDefinitionInspection.cpp)
target_include_directories(STEP_SEMANTICS PUBLIC inc PRIVATE src)
target_compile_features(STEP_SEMANTICS PUBLIC cxx_std_20)
target_link_libraries(STEP_SEMANTICS PUBLIC STEP_SOURCE PRIVATE usd_brep_gsl)
option(STEP_BUILD_TOOLS "Build STEP inspection CLIs" ON)
if(STEP_BUILD_TOOLS)
    add_executable(step_semantic_inspect app/step_semantics.cpp)
    target_link_libraries(step_semantic_inspect PRIVATE STEP_SEMANTICS)
endif()

option(STEP_SEMANTICS_BUILD_TESTS "Build source semantics contract tests" ON)
if(STEP_SEMANTICS_BUILD_TESTS)
    add_executable(step_semantics_tests tests/StepSemanticsTests.cpp)
    target_link_libraries(step_semantics_tests PRIVATE STEP_SEMANTICS)
    add_test(NAME step_semantics_contracts COMMAND step_semantics_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/step_import_validation/corpus/ap203e2-planar/v1/cases")
    add_executable(step_definition_inspection_tests tests/StepDefinitionInspectionTests.cpp)
    target_link_libraries(step_definition_inspection_tests PRIVATE STEP_SEMANTICS)
    add_test(NAME step_definition_inspection_contracts COMMAND step_definition_inspection_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../STEP_IMPORT/tests/fixtures/independent_distinct_parts.step"
        "${CMAKE_CURRENT_SOURCE_DIR}/../STEP_IMPORT/tests/fixtures/independent_two_parts.step")
    # These are acceptance tools, never product runtime dependencies. The sealed
    # snapshots cover every registered descriptor, including unused declarations.
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    get_filename_component(catalog_repo "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    add_executable(step_catalog_snapshot tests/catalog/StepCatalogSnapshot.cpp)
    add_executable(step_catalog_allocations tests/catalog/StepCatalogAllocations.cpp)
    foreach(target step_catalog_snapshot step_catalog_allocations)
        target_include_directories(${target} PRIVATE src)
        target_link_libraries(${target} PRIVATE STEP_SEMANTICS usd_brep_gsl)
    endforeach()
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/catalog-build-$<CONFIG>.txt" CONTENT
        "compiler=${CMAKE_CXX_COMPILER_ID}\ncompiler_version=${CMAKE_CXX_COMPILER_VERSION}\ngenerator=${CMAKE_GENERATOR}\nplatform=${CMAKE_GENERATOR_PLATFORM}\nconfiguration=$<CONFIG>\nstandard=20\nflags=${CMAKE_CXX_FLAGS}\nconfiguration_flags=$<$<CONFIG:Release>:${CMAKE_CXX_FLAGS_RELEASE}>$<$<CONFIG:Debug>:${CMAKE_CXX_FLAGS_DEBUG}>\ntarget_options=$<TARGET_PROPERTY:STEP_SEMANTICS,COMPILE_OPTIONS>\nipo=$<TARGET_PROPERTY:STEP_SEMANTICS,INTERPROCEDURAL_OPTIMIZATION>\nsnapshot=$<TARGET_FILE:step_catalog_snapshot>\nallocations=$<TARGET_FILE:step_catalog_allocations>\n")
    add_test(NAME step_catalog_snapshot_contracts COMMAND "${Python3_EXECUTABLE}" -m tools.step_catalog_validation check
        --snapshot $<TARGET_FILE:step_catalog_snapshot> --baseline "${CMAKE_CURRENT_SOURCE_DIR}/tests/catalog/baseline-product-structure")
    add_test(NAME step_catalog_tool_contracts COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s tools/step_catalog_validation/tests -v)
    set(compile_driver_arguments --build-dir "${CMAKE_CURRENT_BINARY_DIR}/catalog-compile-$<CONFIG>"
        --config $<CONFIG> --generator "${CMAKE_GENERATOR}")
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND compile_driver_arguments --architecture "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    add_test(NAME step_catalog_compile_driver COMMAND "${Python3_EXECUTABLE}"
        -m tools.step_catalog_validation.compile_contracts ${compile_driver_arguments})
    set_tests_properties(step_catalog_snapshot_contracts step_catalog_tool_contracts step_catalog_compile_driver
        PROPERTIES WORKING_DIRECTORY "${catalog_repo}")
    foreach(profile RANGE 0 4)
        add_test(NAME step_catalog_allocations_${profile} COMMAND step_catalog_allocations ${profile})
    endforeach()
endif()
foreach(target STEP_SEMANTICS step_semantic_inspect step_semantics_tests step_definition_inspection_tests step_catalog_snapshot step_catalog_allocations)
    if(TARGET ${target})
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
    endif()
endforeach()
