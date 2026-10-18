# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Header-name preservation grants no schema, geometry or native qualification."""

import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import ap214_source, CORPUS

FLAG = "--preserve-opaque-header-name"


def legacy_header():
    data = ap214_source((CORPUS / "S203-P04/input.step").read_bytes())
    data, count = re.subn(
        rb"(FILE_NAME\(\s*')[^']*(')", lambda m: m[1] + b"part-\xc9\xa2\xc8\xc8.stp" + m[2], data, count=1
    )
    assert count == 1
    return data


class OpaqueHeaderNativeTests(unittest.TestCase):
    def invoke(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="step-native-opaque-") as directory:
            path = Path(directory) / "input.step"
            path.write_bytes(data)
            run = subprocess.run(
                [
                    os.environ["STEP_NATIVE_BINARY"],
                    "--fixture-limits",
                    "--quality",
                    "Q-AP214-development-mm-01",
                    str(path),
                    *options,
                ],
                capture_output=True,
                timeout=30,
            )
        return run, json.loads(run.stdout) if run.stdout else None

    def test_explicit_interop_keeps_unknown_header_name_and_exact_box(self):
        data = legacy_header()
        run, report = self.invoke(data, FLAG, "--interop-nongeometric")
        self.assertEqual(0, run.returncode, run.stderr or report)
        self.assertEqual((6, 12, 8), tuple(report["model"][k] for k in ["faces", "edges", "vertices"]))
        self.assertAlmostEqual(6000, report["model"]["material_volume"]["value_mm3"], places=5)
        raw = report["source_inspection"]["source"]
        self.assertEqual(data, raw["input"]["raw"].encode("latin1"))
        self.assertEqual("OpaqueString", raw["header"][1]["parameters"][0]["kind"])
        self.assertIsNone(raw["header"][1]["parameters"][0]["text"])

    def test_interop_alone_does_not_select_the_opaque_carrier(self):
        run, report = self.invoke(legacy_header(), "--interop-nongeometric")
        self.assertNotEqual(0, run.returncode)
        self.assertEqual("UnsupportedEncoding", report["diagnostics"][0]["key"])
        self.assertEqual(0, report["native_calls"])
        self.assertIsNone(report["model"])

    def test_opaque_flag_requires_separate_explicit_interoperability(self):
        run, report = self.invoke(legacy_header(), FLAG)
        self.assertEqual(2, run.returncode)
        self.assertIsNone(report)

    def test_unknown_old_oid_is_not_relabelled_as_supported_ap214(self):
        data = legacy_header().replace(b"1 0 10303 214 3 1 1", b"1 0 10303 214 1 1 1 1")
        run, report = self.invoke(data, FLAG, "--interop-nongeometric")
        self.assertEqual(2, run.returncode)
        self.assertEqual("Inspected", report["source_inspection"]["source"]["status"])
        self.assertEqual(0, report["native_calls"])
        self.assertIsNone(report["model"])
        selection = report["source_inspection"]["schema_selection"]
        self.assertIsNone(selection["qualified_profile"])

    def test_raw_data_bytes_and_bad_references_still_block_before_native(self):
        for data in [
            legacy_header().replace(b"CARTESIAN_POINT('',", b"CARTESIAN_POINT('\x81',", 1),
            legacy_header().replace(b"#100", b"#999999", 1),
        ]:
            with self.subTest(data=data[:20]):
                run, report = self.invoke(data, FLAG, "--interop-nongeometric")
                self.assertNotEqual(0, run.returncode)
                self.assertEqual(0, report["native_calls"])
                self.assertIsNone(report["model"])
