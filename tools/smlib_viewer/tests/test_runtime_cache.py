# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools.smlib_viewer.runtime_cache import configure


@unittest.skipUnless(os.name == "nt", "Windows portable runtime")
class RuntimeCacheTests(unittest.TestCase):
    def test_reuse_only_for_same_portable_installation(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            executable = root / "public-viewer.exe"
            executable.write_bytes(b"public bootloader")
            with patch.object(sys, "frozen", True, create=True), \
                    patch.object(sys, "executable", str(executable)), \
                    patch.object(sys, "_MEIPASS", str(root / "runtime-a"), create=True), \
                    patch.dict(os.environ, {"LOCALAPPDATA": str(root / "user"), "MPLCONFIGDIR": "temporary"}):
                configure()
                first = Path(os.environ["MPLCONFIGDIR"])
                marker = first / "public-font-cache.json"
                marker.write_bytes(b"public font paths")
                configure()
                self.assertEqual(first, Path(os.environ["MPLCONFIGDIR"]))
                self.assertEqual(b"public font paths", marker.read_bytes())
                with patch.object(sys, "_MEIPASS", str(root / "runtime-b")):
                    configure()
                    self.assertNotEqual(first, Path(os.environ["MPLCONFIGDIR"]))
                executable.write_bytes(b"changed public bootloader")
                configure()
                self.assertNotEqual(first, Path(os.environ["MPLCONFIGDIR"]))
                self.assertEqual(b"public font paths", marker.read_bytes())
