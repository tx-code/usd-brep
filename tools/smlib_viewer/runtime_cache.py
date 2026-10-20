# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Reuse font discovery in a fixed portable runtime; never cache model data."""
import hashlib
import os
from pathlib import Path
import sys


def configure():
    if not getattr(sys, "frozen", False) or os.name != "nt" or not os.environ.get("LOCALAPPDATA"):
        return
    try:
        executable = Path(sys.executable)
        runtime = Path(sys._MEIPASS).resolve()
        stamp = executable.stat()
        identity = f"{runtime}|{stamp.st_size}|{stamp.st_mtime_ns}"
        key = hashlib.sha256(identity.encode("utf-8")).hexdigest()[:24]
        cache = Path(os.environ["LOCALAPPDATA"]) / "BRepViewer/Cache" / key / "matplotlib"
        cache.mkdir(parents=True, exist_ok=True)
        # PyInstaller's pyi_rth_mplconfig.py intentionally uses a new directory
        # per launch because onefile extraction moves font paths. This onedir
        # edition has a fixed runtime. Key by runtime path + bootloader identity
        # so a moved/replaced installation never consumes stale absolute paths.
        # Set this before PyVista imports Matplotlib, including mesh workers.
        os.environ["MPLCONFIGDIR"] = str(cache)
    except OSError:
        pass  # A read-only user profile keeps the original temporary cache.
