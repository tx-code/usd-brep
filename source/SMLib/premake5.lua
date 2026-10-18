-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
-- SPDX-License-Identifier: Apache-2.0

project "SMLib"
    kind "SharedLib"
    location (workspace_dir.."/%{prj.name}")
    -- SM_USE_TBB and SM_USE_TBB_SCALABLE are disabled in the stock kernel.
    -- Do not link an allocator/runtime from an unrelated scene-format SDK.
    
    -- Build PCH on Windows
    filter { "system:windows" }
        pchheader "StdAfx.h"
        pchsource "src/StdAfx.cpp"
        files { "src/StdAfx.cpp" }
        removeflags{ "NoPCH" }
    filter {}

    -- Regular build files
    files { "src/*.cpp", "inc/*.h" }
    includedirs {
        root.."/source/SMLib/inc",
    }
    vpaths { [''] = "*.cpp" }
    
    -- Compiler flags
    filter { "system:windows" }
        defines { "NL_DEBUG_CODE" }
        buildoptions {}
    filter { "system:macosx" }
        buildoptions (macos_common_buildoptions())
        linkoptions (macos_common_linkoptions())
    filter {}






