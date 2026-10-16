-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
-- SPDX-License-Identifier: Apache-2.0

project "STEP_SOURCE"
    kind "StaticLib"
    location (workspace_dir.."/%{prj.name}")
    files { "src/*.cpp", "inc/*.h" }
    includedirs { root.."/source/STEP_SOURCE/inc" }
    vpaths { [''] = "*.cpp" }

project "step_inspect"
    kind "ConsoleApp"
    location (workspace_dir.."/%{prj.name}")
    files { "app/*.cpp" }
    includedirs { root.."/source/STEP_SOURCE/inc" }
    links { "STEP_SOURCE" }
