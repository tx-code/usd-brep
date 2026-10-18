-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
-- SPDX-License-Identifier: Apache-2.0

------------------------------------------------------------------------------------------------------------------------
------------------                                 Overview                                           ------------------
------------------------------------------------------------------------------------------------------------------------

-- This file defines functions that can be called to use a library
-- in a project. This generally sets up the include paths, library path,
-- and sometimes library to link. It's easier than maintaining
-- a mishmash of includedirs and whatever per-project.


------------------------------------------------------------------------------------------------------------------------
------------------                                 Libraries                                          ------------------
------------------------------------------------------------------------------------------------------------------------

function use_python()
    filter { "system:linux" }
        links { "python3.12"}
        libdirs { target_deps.."/python/lib" }
        runpathdirs { target_deps.."/python/lib" }
        externalincludedirs { target_deps.."/python/include/python3.12" }
    filter { "system:windows" }
        libdirs { target_deps.."/python/libs" }
        externalincludedirs { target_deps.."/python/include" }
    filter { "system:macosx" }
        links { "python3.12" }
        libdirs { target_deps.."/python/lib" }
        linkoptions { "-Wl,-rpath," .. target_deps .. "/python/lib" }
        externalincludedirs { target_deps.."/python/include/python3.12" }
    filter {}
end
