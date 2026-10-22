# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Preserved unreadable metadata cannot alter the exact synthetic box."""
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_opaque_identity import opaque_identity_box


class NativeOpaqueIdentityTests(unittest.TestCase):
    def test_full_native_box_keeps_geometry_and_original_unavailable_metadata(self):
        self.check_box(opaque_identity_box(), 5)

    def test_attached_opaque_face_caption_keeps_native_geometry(self):
        data, count = re.subn(rb"ADVANCED_FACE\('',", lambda _: b"ADVANCED_FACE('\x90',", opaque_identity_box(), count=1)
        self.assertEqual(1, count)
        self.check_box(data, 6)

    def test_all_attached_topology_captions_keep_native_geometry(self):
        pattern = rb"(ADVANCED_FACE|EDGE_CURVE|VERTEX_POINT|CLOSED_SHELL|FACE_OUTER_BOUND|EDGE_LOOP|ORIENTED_EDGE)\('[^']*',"
        data, count = re.subn(pattern, lambda m: m[1] + b"('\x91',", opaque_identity_box())
        self.assertGreater(count, 7)
        self.check_box(data, count + 5)

    def check_box(self, data, preserved_count):
        with tempfile.TemporaryDirectory(prefix="step-native-opaque-id-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run(
                [os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", str(path),
                 "--quality", "Q-AP214-development-mm-01", "--interop-nongeometric",
                 "--preserve-opaque-data-strings"], capture_output=True, timeout=30,
            )
        self.assertEqual(0, run.returncode, run.stderr or run.stdout)
        report = json.loads(run.stdout)
        self.assertEqual((6, 12, 8), tuple(report["model"][k] for k in ("faces", "edges", "vertices")))
        self.assertAlmostEqual(6000, report["model"]["material_volume"]["value_mm3"], delta=1e-5)
        self.assertAlmostEqual(2200, report["model"]["area_mm2"], delta=1e-5)
        self.assertEqual(data, report["source_inspection"]["source"]["input"]["raw"].encode("latin1"))
        self.assertEqual(preserved_count, sum(d["key"] == "OpaqueDataTextUnestablished"
                                for d in report["source_inspection"]["preserved_diagnostics"]))
