-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
-- SPDX-License-Identifier: Apache-2.0
project "STEP_IMPORT"
    kind "StaticLib"
    cppdialect "C++20"
    location (workspace_dir.."/%{prj.name}")
    files { "src/*.cpp", "src/*.h", "inc/*.h" }
    includedirs { root.."/source/STEP_SOURCE/inc", root.."/source/STEP_SEMANTICS/inc", root.."/source/STEP_PLANAR/inc",
                  root.."/source/STEP_IMPORT/inc", root.."/source/SMLib/inc", root.."/source/SM_API/inc" }
    links { "STEP_PLANAR", "STEP_SEMANTICS", "STEP_SOURCE", "SM_API", "SMLib" }
    defines { "NL_DEBUG_CODE" }
    filter { "system:windows" }
        links { "Psapi" }
        buildoptions { "/fp:strict", "/permissive-" }
    filter { "system:linux or macosx" }
        buildoptions { "-fno-fast-math", "-ffp-contract=off" }
    filter {}
if smlib_with_validation then
project "step_native_import"
    kind "ConsoleApp"
    cppdialect "C++20"
    location (workspace_dir.."/%{prj.name}")
    files { "app/*.cpp" }
    includedirs { root.."/source/STEP_SOURCE/inc", root.."/source/STEP_SEMANTICS/inc", root.."/source/STEP_PLANAR/inc",
                  root.."/source/STEP_IMPORT/inc" }
    links { "STEP_IMPORT", "STEP_PLANAR", "STEP_SEMANTICS", "STEP_SOURCE", "SM_API", "SMLib" }

end
