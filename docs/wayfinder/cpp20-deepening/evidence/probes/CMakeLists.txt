# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.24)
project(Cpp20EvidenceProbes LANGUAGES CXX)

# Compile-only observations of the current headers/toolchain, not product tests
# or proof of runtime storage safety. All generated objects/logs stay in build.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
get_filename_component(repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(probe_includes
    "${repo_root}/source/SMLib/inc"
    "${repo_root}/source/STEP_SOURCE/inc"
    "${repo_root}/source/STEP_SEMANTICS/inc"
    "${repo_root}/source/STEP_PLANAR/inc"
    "${repo_root}/source/STEP_PLANAR/src")
set(probe_definitions)
if(WIN32)
    list(APPEND probe_definitions -DNOMINMAX -DUNICODE -D_UNICODE -DSM_GRAPHICS_CALLBACKS=1 -DNL_DEBUG_CODE)
endif()

set(report "{\n  \"compiler\": \"${CMAKE_CXX_COMPILER_ID}\",\n  \"version\": \"${CMAKE_CXX_COMPILER_VERSION}\",\n  \"standard\": 20,\n  \"probes\": [\n")
set(first TRUE)
function(observe name expected)
    try_compile(compiled
        "${CMAKE_BINARY_DIR}/checks/${name}"
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp"
        CMAKE_FLAGS "-DCMAKE_CXX_STANDARD=20" "-DCMAKE_CXX_STANDARD_REQUIRED=ON"
            "-DINCLUDE_DIRECTORIES:STRING=${probe_includes}"
        COMPILE_DEFINITIONS ${probe_definitions}
        OUTPUT_VARIABLE log)
    file(WRITE "${CMAKE_BINARY_DIR}/${name}.log" "${log}")
    if(compiled)
        set(value true)
    else()
        set(value false)
    endif()
    if(NOT first)
        string(APPEND report ",\n")
    endif()
    string(APPEND report "    {\"source\": \"${name}.cpp\", \"compiled\": ${value}}")
    set(report "${report}" PARENT_SCOPE)
    set(first FALSE PARENT_SCOPE)
    message(STATUS "${name}: compiled=${value} (expected ${expected})")
    if((expected AND NOT compiled) OR (NOT expected AND compiled))
        message(FATAL_ERROR "Unexpected observation for ${name}; inspect its build log before drawing conclusions")
    endif()
endfunction()

observe(type_traits TRUE)
observe(rule_registry TRUE)
observe(rational_default TRUE)
observe(rational_nonzero FALSE)
observe(transient_vector TRUE)
observe(persistent_vector FALSE)
observe(bitset_type TRUE)
observe(bitset_mutation FALSE)
observe(message_only_assertion TRUE)
string(APPEND report "\n  ]\n}\n")
file(WRITE "${CMAKE_BINARY_DIR}/observations.json" "${report}")
