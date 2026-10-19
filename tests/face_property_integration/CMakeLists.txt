# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
add_executable(face_property_integration tests.cpp)
target_include_directories(face_property_integration PRIVATE ../../source/SMLib/src)
target_link_libraries(face_property_integration PRIVATE SM_API)
add_test(NAME face_property_integration COMMAND face_property_integration)
set_tests_properties(face_property_integration PROPERTIES TIMEOUT 120)
