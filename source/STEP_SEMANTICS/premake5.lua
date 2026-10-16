-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
-- SPDX-License-Identifier: Apache-2.0
project "STEP_SEMANTICS"
    kind "StaticLib"
    location (workspace_dir.."/%{prj.name}")
    files { "src/*.cpp", "src/*.h", "inc/*.h" }
    includedirs { root.."/source/STEP_SOURCE/inc", root.."/source/STEP_SEMANTICS/inc", root.."/source/STEP_SEMANTICS/src" }
    links { "STEP_SOURCE" }
project "step_semantic_inspect"
    kind "ConsoleApp"
    location (workspace_dir.."/%{prj.name}")
    files { "app/*.cpp" }
    includedirs { root.."/source/STEP_SOURCE/inc", root.."/source/STEP_SEMANTICS/inc" }
    links { "STEP_SEMANTICS", "STEP_SOURCE" }
