:: SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
:: SPDX-License-Identifier: Apache-2.0
@echo off
call "%~dp0tools\packman\python.bat" "%~dp0tools\scripts\smlib_build.py" configure %*
exit /b %errorlevel%
