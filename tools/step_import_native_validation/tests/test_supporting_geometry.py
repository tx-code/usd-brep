# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Public synthetic construction geometry retains role, units and complete archive."""
import unittest

from tools.step_import_native_validation.tests import test_wireframe as wireframe_tests
from tools.step_semantic_validation.tests.test_supporting_geometry import supporting_source
from tools.step_semantic_validation.tests.test_ap214 import AP214
from tools.step_semantic_validation.tests.test_ap214_curve_style import MBX


class SupportingNativeTests(unittest.TestCase):
    invoke = wireframe_tests.LinkedWireframeNativeTests.invoke

    def test_complete_native_members_and_report_are_separate_from_material(self):
        for schema in (AP214, MBX):
            run, report = self.invoke(supporting_source(schema), write=True)
            self.assertEqual(0, run.returncode, run.stderr or report["diagnostics"])
            group, = report["supporting_geometry"]
            self.assertEqual(("960", "961", "ConstructiveSupportingGeometry", False), tuple(group[k] for k in
                             ("source_representation", "source_relationship", "role", "material_boundary")))
            self.assertEqual(["912", "940"], [x["source_id"] for x in group["curves"]])
            self.assertEqual(["911"], [x["source_id"] for x in group["points"]])
            self.assertEqual(["904", "970"], [x["source_id"] for x in group["frames"]])
            self.assertEqual(["972"], [x["source_id"] for x in group["planes"]])
            self.assertEqual([30., 40., 50.], group["planes"][0]["frame"]["origin_mm"])
            self.assertEqual([1., 0., 0.], group["planes"][0]["frame"]["z"])
            self.assertTrue(group["planes"][0]["unbounded_source_plane"])
            self.assertTrue(all(not x["is_source_surface"] for x in group["frames"]))
            self.assertNotIn("linked_wireframes", report)
            self.assertAlmostEqual(6000., report["model"]["material_volume"]["value_mm3"], places=5)

    def test_shared_members_use_one_native_definition_across_two_source_roles(self):
        run, report = self.invoke(supporting_source(wireframe=True), write=True)
        self.assertEqual(0, run.returncode, run.stderr or report["diagnostics"])
        wire, = report["linked_wireframes"]
        support, = report["supporting_geometry"]
        self.assertEqual(wire["curves"], support["curves"])
        self.assertEqual(wire["points"], support["points"])

    def test_centimetres_scale_positions_once_and_not_frame_directions(self):
        data = supporting_source().replace(b"SI_UNIT(.MILLI.,.METRE.)", b"SI_UNIT(.CENTI.,.METRE.)")
        run, report = self.invoke(data, write=True)
        self.assertEqual(0, run.returncode, run.stderr or report["diagnostics"])
        group, = report["supporting_geometry"]
        self.assertEqual([300., 400., 500.], group["planes"][0]["frame"]["origin_mm"])
        self.assertEqual([0., 1., 0.], group["frames"][1]["frame"]["x"])
        self.assertEqual([70., 80., 90.], group["points"][0]["point_mm"])

    def test_unbounded_line_and_bad_directed_link_never_publish_partial_body(self):
        for data, expected in ((supporting_source().replace(b"#911,#912,#940)", b"#911,#912,#940,#913)"),
                                "GeneralSupportingGeometryUnsupported"),
                               (supporting_source().replace(b"'',$,#900,#960", b"'',$,#960,#900"),
                                "GeneralSourceUnavailable")):
            run, report = self.invoke(data)
            self.assertNotEqual(0, run.returncode)
            self.assertIsNone(report["model"])
            self.assertIn(expected, [x["key"] for x in report["diagnostics"]])
            self.assertEqual(0, report["native_calls"])

    def test_shared_curve_retains_nonzero_residual_in_each_source_role(self):
        data = supporting_source(wireframe=True).replace(
            b"#914=CARTESIAN_POINT('',(0.,0.,0.))", b"#914=CARTESIAN_POINT('',(0.,0.0000001,0.))")
        run, report = self.invoke(data, write=True)
        self.assertEqual(0, run.returncode, run.stderr or report["diagnostics"])
        measurements = {x["kind"]: x["value_mm"] for x in report["measurements"]}
        self.assertGreater(measurements["MeasuredWireframeSourceTrimResidual"], 0)
        self.assertAlmostEqual(1e-7, measurements["MeasuredSupportingSourceTrimResidual"], delta=1e-14)
        for suffix in ("SourceTrimResidual", "DefinitionEndpointError"):
            self.assertEqual(measurements["MeasuredWireframe" + suffix], measurements["MeasuredSupporting" + suffix])

    def test_no_inapplicable_measurements_are_reported_as_zero_for_supporting_role(self):
        data = supporting_source(wireframe=True).replace(b"(#904,#970,#972,#911,#912,#940)",
                                                        b"(#904,#970,#972,#911)")
        run, report = self.invoke(data, write=True)
        self.assertEqual(0, run.returncode, run.stderr or report["diagnostics"])
        self.assertFalse(any(x["kind"].startswith("MeasuredSupporting") for x in report["measurements"]))
