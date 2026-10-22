# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
add_executable(healing_status healing.cpp)
target_link_libraries(healing_status PRIVATE SM_API)
add_test(NAME healing_status COMMAND healing_status)
set_tests_properties(healing_status PROPERTIES TIMEOUT 30)
