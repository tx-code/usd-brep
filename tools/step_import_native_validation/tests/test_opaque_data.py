# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Nondefining unknown labels do not alter analytic imported geometry."""
import math
import unittest

from tools.step_import_native_validation.tests import test_general


def opaque_source(field="layer"):
    data = test_general.cylinder_source().replace(b"{1 0 10303 214 3 1 1}", b"{1 0 10303 214 1 1 1 1}")
    if field == "layer":
        return data.replace(b"ENDSEC;\nEND-ISO",
                            b"#950=PRESENTATION_LAYER_ASSIGNMENT('\x80\xfe','',(#21));\nENDSEC;\nEND-ISO")
    if field == "product":
        return data.replace(b"'P0-PART','P0 analytic fixture'", b"'\x80\xfe','\x81\xff'")
    if field == "representation":
        return data.replace(b"'body',(#21)", b"'\x80\xfe',(#21)")
    if field == "defining":
        return data.replace(b"'automotive_design',2010", b"'\x80\xfe',2010")
    raise AssertionError(field)


class OpaqueNativeTests(unittest.TestCase):
    invoke_bytes = test_general.GeneralTests.invoke_bytes

    def test_selected_nondefining_text_keeps_the_whole_analytic_cylinder(self):
        for field, count in [("layer", 1), ("product", 2)]:
            with self.subTest(field=field):
                source = opaque_source(field)
                run, result = self.invoke_bytes(source, "--interop-nongeometric", "--preserve-opaque-data-strings")
                self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
                self.assertEqual("CheckedWithDiagnostics", result["source_inspection"]["status"])
                preserved = result["source_inspection"]["preserved_diagnostics"]
                self.assertEqual(count, sum(d["key"] == "OpaqueDataTextUnestablished" for d in preserved))
                self.assertEqual(source, result["source_inspection"]["source"]["input"]["raw"].encode("latin1"))
                self.assertAlmostEqual(63 * math.pi, result["model"]["material_volume"]["value_mm3"],
                                       delta=63 * math.pi * 2e-4)
                self.assertEqual((3, 3, 2), tuple(result["model"][k] for k in ["faces", "edges", "vertices"]))

    def test_data_option_requires_general_interoperability_and_does_not_follow_header_option(self):
        source = opaque_source()
        run, result = self.invoke_bytes(source, "--preserve-opaque-data-strings")
        self.assertEqual(2, run.returncode)
        self.assertIsNone(result)
        run, result = self.invoke_bytes(source, "--interop-nongeometric", "--preserve-opaque-header-name")
        self.assertNotEqual(0, run.returncode)
        self.assertEqual("UnsupportedEncoding", result["source_inspection"]["source"]["diagnostics"][0]["key"])
        self.assertIsNone(result["model"])

    def test_protocol_text_and_detached_layer_remain_fatal(self):
        detached = opaque_source().replace(b"'',(#21)", b"'',(#951)").replace(
            b"ENDSEC;\nEND-ISO", b"#951=CARTESIAN_POINT('',(100.,100.,100.));\nENDSEC;\nEND-ISO")
        for source in [opaque_source("defining"), opaque_source("representation"), detached]:
            with self.subTest(reason="unapproved meaning or owner"):
                run, result = self.invoke_bytes(source, "--interop-nongeometric", "--preserve-opaque-data-strings")
                self.assertNotEqual(0, run.returncode)
                self.assertIsNone(result["model"])
