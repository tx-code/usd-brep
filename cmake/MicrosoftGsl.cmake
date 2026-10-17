# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
include_guard(GLOBAL)

# One offline, pinned header tree shared by all standalone STEP entrypoints.
add_library(usd_brep_gsl INTERFACE)
target_include_directories(usd_brep_gsl SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/../third_party/microsoft-gsl/include")
target_compile_features(usd_brep_gsl INTERFACE cxx_std_14)
