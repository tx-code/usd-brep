# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
add_executable(surface_point_solve tests.cpp)
target_link_libraries(surface_point_solve PRIVATE SM_API)
add_test(NAME surface_point_solve COMMAND surface_point_solve)
set_tests_properties(surface_point_solve PROPERTIES TIMEOUT 120)
