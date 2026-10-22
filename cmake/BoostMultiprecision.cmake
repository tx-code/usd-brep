# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
include_guard(GLOBAL)

# Pinned offline standalone headers. Keep this private to the unit arithmetic
# translation unit's owning target; no Boost type crosses an SDK interface.
add_library(usd_brep_multiprecision INTERFACE)
target_include_directories(usd_brep_multiprecision SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/../third_party/boost-multiprecision/include")
target_compile_definitions(usd_brep_multiprecision INTERFACE BOOST_MP_STANDALONE)
