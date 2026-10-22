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

    def test_person_name_lists_keep_analytic_geometry_and_per_element_diagnostics(self):
        from tools.step_semantic_validation.tests.test_opaque_identity import administrative_name_lists
        source = administrative_name_lists()
        run, result = self.invoke_bytes(source, "--interop-nongeometric", "--preserve-opaque-data-strings")
        self.assertEqual(0, run.returncode, run.stderr or result)
        self.assertEqual('Accepted', result['status'])
        self.assertEqual(source, result['source_inspection']['source']['input']['raw'].encode('latin1'))
        self.assertAlmostEqual(6000, result['model']['material_volume']['value_mm3'], delta=1e-5)
        preserved = result['source_inspection']['preserved_diagnostics']
        self.assertEqual({(4, 1), (4, 3), (5, 1), (6, 1)},
                         {tuple(d['parameter_path']) for d in preserved if d['key'] == 'OpaqueDataTextUnestablished'})

    def test_attached_administrative_text_preserves_analytic_box_and_source(self):
        from tools.step_semantic_validation.tests.test_opaque_identity import administrative_names
        source = administrative_names()
        run, result = self.invoke_bytes(source, "--interop-nongeometric", "--preserve-opaque-data-strings")
        self.assertEqual(0, run.returncode, run.stderr or result)
        self.assertEqual('Accepted', result['status'])
        self.assertEqual(source, result['source_inspection']['source']['input']['raw'].encode('latin1'))
        self.assertEqual((6, 12, 8), tuple(result['model'][key] for key in ['faces', 'edges', 'vertices']))
        self.assertAlmostEqual(6000, result['model']['material_volume']['value_mm3'], delta=1e-5)
        self.assertEqual(9, sum(d['key'] == 'OpaqueDataTextUnestablished'
                                for d in result['source_inspection']['preserved_diagnostics']))

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


class AsciiInvariantPurposeTests(unittest.TestCase):
    invoke_bytes = test_general.GeneralTests.invoke_bytes
    options = ("--interop-nongeometric", "--assume-ascii-invariant-data")

    def test_explicit_assumption_keeps_source_and_analytic_cylinder(self):
        source = opaque_source("representation")
        run, result = self.invoke_bytes(source, *self.options)
        self.assertEqual(11, run.returncode, run.stderr or result)
        self.assertEqual("AcceptedUnderInterpretation", result["status"])
        self.assertEqual("PreserveAsciiInvariantBytes", result["data_string_interpretation"])
        self.assertFalse(result["data_encoding_verified"])
        self.assertEqual(source, result["source_inspection"]["source"]["input"]["raw"].encode("latin1"))
        self.assertTrue(any(d["key"] == "OpaqueDataTextUnestablished"
                            for d in result["source_inspection"]["preserved_diagnostics"]))
        self.assertAlmostEqual(63 * math.pi, result["model"]["material_volume"]["value_mm3"], delta=0.04)
        self.assertEqual((3, 3, 2), tuple(result["model"][k] for k in ("faces", "edges", "vertices")))

    def test_known_reserved_names_still_reject_and_unused_option_is_not_applied(self):
        ordinary = opaque_source("representation").replace(b"\x80\xfe", b"ordinary")
        run, result = self.invoke_bytes(ordinary, *self.options)
        self.assertEqual(0, run.returncode, run.stderr or result)
        self.assertEqual("Accepted", result["status"])
        for name in (b"centre of mass", b"treatment result"):
            run, result = self.invoke_bytes(ordinary.replace(b"ordinary", name), *self.options)
            self.assertNotEqual(0, run.returncode)
            self.assertIsNone(result["model"])
            self.assertEqual(0, result["native_calls"])
            self.assertTrue(any(d["key"] == "UnsupportedRepresentationPurpose"
                                for d in result["source_inspection"]["diagnostics"]))

    def test_escape_bearing_opaque_name_and_physical_context_are_not_exempted(self):
        for source in (opaque_source("representation").replace(b"\x80\xfe", b"\x80\\X2\\0041\\X0\\"),
                       opaque_source("defining")):
            run, result = self.invoke_bytes(source, *self.options)
            self.assertNotEqual(0, run.returncode)
            self.assertIsNone(result["model"])
            self.assertEqual(0, result["native_calls"])

    def test_new_policy_requires_explicit_interoperability(self):
        run, result = self.invoke_bytes(opaque_source("representation"), "--assume-ascii-invariant-data")
        self.assertEqual(2, run.returncode)
        self.assertIsNone(result)

    def test_inverse_property_use_and_orphan_representation_remain_unpublished(self):
        source = opaque_source("representation")
        for extra in (b"#950=PROPERTY_DEFINITION('surface texture',$,#7);\n#951=PROPERTY_DEFINITION_REPRESENTATION(#950,#20);\n",
                      b"#950=REPRESENTATION('\x80\xfe',(#21),#12);\n"):
            run, result = self.invoke_bytes(source.replace(b"ENDSEC;\nEND-ISO", extra + b"ENDSEC;\nEND-ISO"), *self.options)
            self.assertNotEqual(0, run.returncode)
            self.assertIsNone(result["model"])
            self.assertEqual(0, result["native_calls"])

    def test_conflicting_policy_flags_and_report_exhaustion_do_not_publish(self):
        for flags in (("--assume-ascii-invariant-data", "--preserve-opaque-data-strings"),
                      ("--preserve-opaque-data-strings", "--assume-ascii-invariant-data")):
            run, result = self.invoke_bytes(opaque_source("representation"), "--interop-nongeometric", *flags)
            self.assertEqual(2, run.returncode)
            self.assertIsNone(result)
        run, result = self.invoke_bytes(opaque_source("representation"), *self.options, "--max-report-bytes", "512")
        self.assertEqual(5, run.returncode)
        self.assertIsNone(result)
