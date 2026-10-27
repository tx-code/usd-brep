# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
add_executable(revolution_definition definition.cpp)
target_link_libraries(revolution_definition PRIVATE SMLib)
add_test(NAME revolution_definition COMMAND revolution_definition "${CMAKE_CURRENT_BINARY_DIR}/output/$<CONFIG>")
set_tests_properties(revolution_definition PROPERTIES TIMEOUT 60)
