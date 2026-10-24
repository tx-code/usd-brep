# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
add_executable(healing_status healing.cpp)
target_link_libraries(healing_status PRIVATE SM_API)
add_test(NAME healing_status COMMAND healing_status)
set_tests_properties(healing_status PROPERTIES TIMEOUT 30)

add_executable(healing_uv_trim uv_trim.cpp)
target_link_libraries(healing_uv_trim PRIVATE SM_API)
foreach(mode failure disabled control)
    add_test(NAME healing_uv_trim_${mode} COMMAND healing_uv_trim ${mode})
    set_tests_properties(healing_uv_trim_${mode} PROPERTIES TIMEOUT 30)
endforeach()

add_executable(healing_degen_face degen_face.cpp)
target_link_libraries(healing_degen_face PRIVATE SM_API)
foreach(mode stage native api control vertex attached)
    add_test(NAME healing_degen_face_${mode} COMMAND healing_degen_face ${mode})
    set_tests_properties(healing_degen_face_${mode} PROPERTIES TIMEOUT 30)
endforeach()
