# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
add_executable(bezier_deviation tests.cpp ../../source/SMLib/src/SmBezierDeviation.cpp)
target_include_directories(bezier_deviation PRIVATE ../../source/SMLib/src)
target_link_libraries(bezier_deviation PRIVATE SMLib)
add_test(NAME bezier_deviation COMMAND bezier_deviation)
set_tests_properties(bezier_deviation PROPERTIES TIMEOUT 60)
