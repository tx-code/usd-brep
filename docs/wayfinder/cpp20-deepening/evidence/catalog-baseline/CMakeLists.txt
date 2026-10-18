# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.24)
project(StepCatalogBaseline LANGUAGES CXX)
if(NOT MSVC OR NOT WIN32)
    message(FATAL_ERROR "This allocation baseline is measured for MSVC/Windows only")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
get_filename_component(default_repository "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(REPOSITORY "${default_repository}" CACHE PATH "Source checkout to measure")
add_executable(catalog_baseline baseline.cpp
    "${REPOSITORY}/source/STEP_SEMANTICS/src/StepCatalog.cpp"
    "${REPOSITORY}/source/STEP_SEMANTICS/src/StepAp203GeneralCatalog.cpp"
    "${REPOSITORY}/source/STEP_SEMANTICS/src/StepAp214MbxCatalog.cpp"
    "${REPOSITORY}/source/STEP_SEMANTICS/src/StepAp214PresentationCatalog.cpp"
    "${REPOSITORY}/source/STEP_SEMANTICS/src/StepDateTimeCatalog.cpp"
    "${REPOSITORY}/source/STEP_SEMANTICS/src/StepRationalSplineCatalog.cpp"
    "${REPOSITORY}/source/STEP_SOURCE/src/StepSource.cpp")
target_include_directories(catalog_baseline PRIVATE
    "${REPOSITORY}/source/STEP_SEMANTICS/inc"
    "${REPOSITORY}/source/STEP_SEMANTICS/src"
    "${REPOSITORY}/source/STEP_SOURCE/inc"
    "${REPOSITORY}/third_party/microsoft-gsl/include")
target_compile_definitions(catalog_baseline PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(catalog_baseline PRIVATE /W4 /Gy)
target_link_options(catalog_baseline PRIVATE /OPT:REF)
