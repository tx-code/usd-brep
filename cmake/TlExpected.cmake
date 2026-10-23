# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
include_guard(GLOBAL)

# Offline, byte-pinned vocabulary for private STEP results. No upstream build,
# configure-time download or tl type is required by a public SDK header.
add_library(usd_brep_expected INTERFACE)
target_include_directories(usd_brep_expected SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/../third_party/tl-expected/include")
target_compile_features(usd_brep_expected INTERFACE cxx_std_20)
