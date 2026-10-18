# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Locate the geometry SDK runtime for optional development tools."""
import glob
import os
import platform
import sys

_DLL_HANDLES = []

def _pick_platform():
    if sys.platform == "win32":
        return "windows-x86_64"
    if sys.platform == "darwin":
        return "macos-universal"
    return "linux-aarch64" if platform.machine().lower() in ("aarch64", "arm64") else "linux-x86_64"

def _pick_config(repo):
    forced = os.environ.get("SMLIB_BUILD_CONFIG", "").lower()
    if forced in ("debug", "release"):
        return forced
    for config in ("release", "debug"):
        if glob.glob(os.path.join(repo, "_build", _pick_platform(), config, "_omni_solid*")):
            return config
    return "release"

def runtime_directory(repo):
    return os.environ.get("SMLIB_RUNTIME_DIR") or os.path.join(repo, "_build", _pick_platform(), _pick_config(repo))

def ensure_runtime_env(repo):
    runtime = runtime_directory(repo)
    if runtime not in sys.path:
        sys.path.insert(0, runtime)
    if sys.platform == "win32" and hasattr(os, "add_dll_directory"):
        for directory in (runtime, os.path.join(repo, "_build", "target-deps", "python")):
            if os.path.isdir(directory):
                _DLL_HANDLES.append(os.add_dll_directory(directory))
