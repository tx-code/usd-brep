# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
add_executable(stitch_face tests.cpp)
target_link_libraries(stitch_face PRIVATE SM_API)
foreach(mode box plane adjacent foreign boolean)
    add_test(NAME stitch_face_${mode} COMMAND stitch_face ${mode})
    set_tests_properties(stitch_face_${mode} PROPERTIES TIMEOUT 30)
endforeach()
