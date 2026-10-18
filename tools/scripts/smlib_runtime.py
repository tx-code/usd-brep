# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Locate the geometry SDK runtime for optional development tools."""

import glob
import os
from pathlib import Path
import sys

if __package__:
    from .smlib_build import host_platform, read_cache, runtime_directory as cmake_runtime_directory
else:
    from smlib_build import host_platform, read_cache, runtime_directory as cmake_runtime_directory

_DLL_HANDLES = []


def _pick_platform():
    return host_platform()


def _pick_config(repo):
    forced = os.environ.get("SMLIB_BUILD_CONFIG", "").lower()
    if forced in ("debug", "release"):
        return forced
    build_dir = Path(os.environ.get("SMLIB_CMAKE_BUILD_DIR", Path(repo) / "_build/cam"))
    cache = read_cache(build_dir)
    configured = cache.get("CMAKE_BUILD_TYPE", "").lower()
    if configured in ("debug", "release") and not cache.get("CMAKE_CONFIGURATION_TYPES"):
        return configured
    for config in ("release", "debug"):
        if glob.glob(str(cmake_runtime_directory(build_dir, config) / "_omni_solid*")):
            return config
    return "release"


def runtime_directory(repo, config=None):
    build_dir = Path(os.environ.get("SMLIB_CMAKE_BUILD_DIR", Path(repo) / "_build/cam"))
    return os.environ.get("SMLIB_RUNTIME_DIR") or str(cmake_runtime_directory(build_dir, config or _pick_config(repo)))


def ensure_runtime_env(repo):
    runtime = runtime_directory(repo)
    if runtime not in sys.path:
        sys.path.insert(0, runtime)
    if sys.platform == "win32" and hasattr(os, "add_dll_directory"):
        for directory in (runtime, os.path.join(repo, "_build", "target-deps", "python")):
            if os.path.isdir(directory):
                _DLL_HANDLES.append(os.add_dll_directory(directory))
