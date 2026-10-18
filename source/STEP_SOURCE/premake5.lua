-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
-- SPDX-License-Identifier: Apache-2.0

project "STEP_SOURCE"
    kind "StaticLib"
    cppdialect "C++20"
    location (workspace_dir.."/%{prj.name}")
    files { "src/*.cpp", "inc/*.h" }
    includedirs { root.."/source/STEP_SOURCE/inc", root.."/third_party/microsoft-gsl/include" }
    vpaths { [''] = "*.cpp" }

if smlib_with_validation then
project "step_inspect"
    kind "ConsoleApp"
    cppdialect "C++20"
    location (workspace_dir.."/%{prj.name}")
    files { "app/*.cpp" }
    includedirs { root.."/source/STEP_SOURCE/inc" }
    links { "STEP_SOURCE" }

end
