:: SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
:: SPDX-License-Identifier: Apache-2.0
@echo off
setlocal
set "runtime=%SMLIB_RUNTIME_DIR%"
if not defined runtime set "runtime=%~dp0_build\windows-x86_64\release"
"%~dp0_build\target-deps\python\python.exe" "%~dp0tools\agent\run_geometry_tests.py" --runtime "%runtime%" --native %*
exit /b %errorlevel%
