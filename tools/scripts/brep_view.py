#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Launch the standalone viewer; no SDK Python extension is required."""
from pathlib import Path
import sys

if not getattr(sys, "frozen", False):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
else:
    # Application modules are ordinary files in the portable runtime. Avoid
    # writing into an installation and timestamp-cache reuse after quick edits.
    sys.dont_write_bytecode = True

from tools.smlib_viewer.cli import main

if __name__ == "__main__":
    raise SystemExit(main())
