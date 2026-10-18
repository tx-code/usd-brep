:: SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
:: SPDX-License-Identifier: Apache-2.0
@echo off
call "%~dp0repo.bat" build -g -r --platform-target windows-x86_64 %*
exit /b %errorlevel%
