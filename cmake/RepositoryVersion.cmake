# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
include_guard(GLOBAL)

# VERSION is the full fork release identity. CMake's project VERSION accepts
# only the numeric core; wheels derive their prerelease spelling from the same
# file. No Python runtime is required by a native-only configure.
function(smlib_read_repository_version version_file)
    file(READ "${version_file}" _smlib_version)
    string(STRIP "${_smlib_version}" _smlib_version)
    string(LENGTH "${_smlib_version}" _smlib_version_length)
    if(_smlib_version_length GREATER 128 OR NOT _smlib_version MATCHES
        "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-(alpha|beta|rc)\\.([0-9]+))?$")
        message(FATAL_ERROR "Invalid VERSION: expected MAJOR.MINOR.PATCH[-alpha.N|-beta.N|-rc.N]")
    endif()
    set(_smlib_core "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    set(_smlib_components "${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}" "${CMAKE_MATCH_3}")
    if(NOT "${CMAKE_MATCH_6}" STREQUAL "")
        list(APPEND _smlib_components "${CMAKE_MATCH_6}")
    endif()
    foreach(_smlib_component IN LISTS _smlib_components)
        if(_smlib_component MATCHES "^0[0-9]+$")
            message(FATAL_ERROR "Invalid VERSION: numeric components must not contain leading zeros")
        endif()
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${version_file}")
    set(SMLIB_VERSION "${_smlib_version}" PARENT_SCOPE)
    set(SMLIB_VERSION_CORE "${_smlib_core}" PARENT_SCOPE)
endfunction()
