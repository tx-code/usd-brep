-- SPDX-License-Identifier: Apache-2.0
project "planar_regions"
    kind "ConsoleApp"
    location (workspace_dir.."/%{prj.name}")
    staticruntime "Off"
    files { "regions.cpp" }
    includedirs { root.."/source/SM_API/inc", root.."/source/SMLib/inc" }
    links { "SM_API", "SMLib" }
    filter { "system:windows" }
        defines { "NOMINMAX", "UNICODE", "_UNICODE" }
        buildoptions { "/utf-8" }
    filter { "system:macosx" }
        buildoptions (macos_common_buildoptions())
        linkoptions (macos_common_linkoptions())
    filter {}
