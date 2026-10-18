:: SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
:: SPDX-License-Identifier: Apache-2.0
@echo off
setlocal
set "runtime=%SMLIB_RUNTIME_DIR%"
if not defined runtime (
    set "tree=%SMLIB_CMAKE_BUILD_DIR%"
    if not defined SMLIB_CMAKE_BUILD_DIR set "tree=%~dp0..\..\_build\cam"
)
if not defined runtime set "runtime=%tree%\bin\Release"
set "PATH=%runtime%;%PATH%"
pushd "%~dp0"
"%runtime%\SM_API_test_app.exe" %*
set "result=%errorlevel%"
popd
exit /b %result%
