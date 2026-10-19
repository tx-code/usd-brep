# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
add_executable(conic_bounds bounds.cpp ../../source/SMLib/src/SmConicBounds.cpp)
target_include_directories(conic_bounds PRIVATE ../../source/SMLib/src)
target_compile_definitions(conic_bounds PRIVATE CONIC_BOUNDS_INTERNAL_TESTS)
target_link_libraries(conic_bounds PRIVATE SMLib)
add_test(NAME conic_bounds COMMAND conic_bounds)
set_tests_properties(conic_bounds PROPERTIES TIMEOUT 60)
