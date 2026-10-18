# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Independent analytic ring torus; two exact source seams are shared twice."""
import math
import unittest

from tools.step_import_native_validation.tests import test_general


def toroidal_source():
    prefix = test_general.cylinder_source().split(b"#22=")[0]
    # Major radius 5, minor radius 2.5. The two source circles bound one
    # fundamental rectangle in UV; each seam has two opposite source uses.
    records = b"""#22=CLOSED_SHELL('',(#180));
#100=CARTESIAN_POINT('',(0.,0.,0.));
#101=CARTESIAN_POINT('',(5.,0.,0.));
#102=CARTESIAN_POINT('',(7.5,0.,0.));
#103=DIRECTION('',(0.,0.,1.));
#104=DIRECTION('',(1.,0.,0.));
#105=DIRECTION('',(0.,-1.,0.));
#106=AXIS2_PLACEMENT_3D('',#100,#103,#104);
#107=AXIS2_PLACEMENT_3D('',#101,#105,#104);
#110=CIRCLE('',#106,7.5);
#111=CIRCLE('',#107,2.5);
#120=VERTEX_POINT('',#102);
#130=EDGE_CURVE('',#120,#120,#110,.T.);
#131=EDGE_CURVE('',#120,#120,#111,.T.);
#140=TOROIDAL_SURFACE('',#106,5.,2.5);
#150=ORIENTED_EDGE('',*,*,#130,.T.);
#151=ORIENTED_EDGE('',*,*,#131,.T.);
#152=ORIENTED_EDGE('',*,*,#130,.F.);
#153=ORIENTED_EDGE('',*,*,#131,.F.);
#160=EDGE_LOOP('',(#150,#151,#152,#153));
#170=FACE_OUTER_BOUND('',#160,.T.);
#180=ADVANCED_FACE('',(#170),#140,.T.);
ENDSEC;
END-ISO-10303-21;
"""
    return prefix + records


class ToroidalTests(unittest.TestCase):
    # Reuse the real CLI invocation, without inheriting unrelated test methods.
    invoke_bytes = test_general.GeneralTests.invoke_bytes

    def test_ring_torus_preserves_seams_and_independent_measures(self):
        source = toroidal_source()
        run, result = self.invoke_bytes(source)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual((1, 2, 1), tuple(result["model"][k] for k in ("faces", "edges", "vertices")))
        self.assertAlmostEqual(62.5 * math.pi**2, result["model"]["material_volume"]["value_mm3"],
                               delta=62.5 * math.pi**2 * 2e-4)
        self.assertAlmostEqual(50 * math.pi**2, result["model"]["area_mm2"], delta=50 * math.pi**2 * 2e-4)
        for actual, expected in zip(result["model"]["bounds_mm"], ((-7.5, -7.5, -2.5), (7.5, 7.5, 2.5))):
            for a, b in zip(actual, expected):
                self.assertAlmostEqual(a, b, delta=1e-6)
        self.assertEqual(source.decode("ascii"), result["source_inspection"]["source"]["input"]["raw"])
        self.assertEqual({"pointer_reports": 0, "level2_reports": 0}, result["validations"])
        self.assertEqual({"130", "131"}, {m["source_use"] for m in result["parameter_mappings"]})
        self.assertEqual({0, 1}, {m["target_edge_index"] for m in result["parameter_mappings"]})
        self.assertEqual([], result["representation_normalizations"])

    def test_ring_torus_placement_and_equivalent_metre_source_keep_physical_measures(self):
        original = toroidal_source()
        shifted = original.replace(b"(0.,0.,0.)", b"(11.,-4.,2.)").replace(
            b"(5.,0.,0.)", b"(11.,1.,2.)").replace(b"(7.5,0.,0.)", b"(11.,3.5,2.)")
        shifted = shifted.replace(b"DIRECTION('',(0.,0.,1.))", b"DIRECTION('',(1.,0.,0.))").replace(
            b"#104=DIRECTION('',(1.,0.,0.))", b"#104=DIRECTION('',(0.,1.,0.))").replace(
            b"DIRECTION('',(0.,-1.,0.))", b"DIRECTION('',(0.,0.,-1.))")
        metres = original.replace(b".MILLI.,.METRE.", b"$,.METRE.").replace(
            b"(5.,0.,0.)", b"(0.005,0.,0.)").replace(b"(7.5,0.,0.)", b"(0.0075,0.,0.)").replace(
            b"CIRCLE('',#106,7.5)", b"CIRCLE('',#106,0.0075)").replace(
            b"CIRCLE('',#107,2.5)", b"CIRCLE('',#107,0.0025)").replace(
            b"TOROIDAL_SURFACE('',#106,5.,2.5)", b"TOROIDAL_SURFACE('',#106,0.005,0.0025)")
        for source, bounds in [(shifted, ((8.5, -11.5, -5.5), (13.5, 3.5, 9.5))),
                               (metres, ((-7.5, -7.5, -2.5), (7.5, 7.5, 2.5)))]:
            with self.subTest(metres=source == metres):
                run, result = self.invoke_bytes(source)
                self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
                self.assertAlmostEqual(62.5 * math.pi**2, result["model"]["material_volume"]["value_mm3"],
                                       delta=62.5 * math.pi**2 * 2e-4)
                self.assertAlmostEqual(50 * math.pi**2, result["model"]["area_mm2"], delta=50 * math.pi**2 * 2e-4)
                for actual, expected in zip(result["model"]["bounds_mm"], bounds):
                    for a, b in zip(actual, expected):
                        self.assertAlmostEqual(a, b, delta=1e-6)
                self.assertEqual(source.decode("ascii"), result["source_inspection"]["source"]["input"]["raw"])

    def test_unmarked_or_wrongly_oriented_torus_does_not_publish(self):
        source = toroidal_source().replace(b"#170=FACE_OUTER_BOUND", b"#170=FACE_BOUND")
        run, result = self.invoke_bytes(source)
        self.assertEqual(7, run.returncode)
        self.assertEqual("GeneralRegionClassificationPending", result["diagnostics"][-1]["key"])
        self.assertIsNone(result["model"])
        wrong = toroidal_source().replace(b"(#170),#140,.T.)", b"(#170),#140,.F.)")
        run, result = self.invoke_bytes(wrong)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(result["model"])

    def test_horn_and_spindle_sources_are_not_relabelled_as_invalid_step(self):
        for radii in [b"2.5,2.5", b"1.25,2.5"]:
            with self.subTest(radii=radii):
                source = toroidal_source().replace(b"TOROIDAL_SURFACE('',#106,5.,2.5)",
                                                   b"TOROIDAL_SURFACE('',#106," + radii + b")")
                run, result = self.invoke_bytes(source)
                self.assertEqual(2, run.returncode)
                self.assertEqual("Unsupported", result["status"])
                self.assertEqual("Checked", result["source_inspection"]["status"])
                self.assertEqual("GeneralToroidalRingRequired", result["diagnostics"][-1]["key"])
                self.assertIsNone(result["model"])

    def test_positive_torus_radius_below_kernel_range_is_a_clean_native_refusal(self):
        # Keep the ordinary seam curves: the malformed geometric combination
        # must not crash inside a surface constructor before later gap checks.
        for radius in [b"1.E-13", b"1.E-12"]:
            with self.subTest(radius=radius):
                source = toroidal_source().replace(b"TOROIDAL_SURFACE('',#106,5.,2.5)",
                                                   b"TOROIDAL_SURFACE('',#106,5.," + radius + b")")
                run, result = self.invoke_bytes(source)
                self.assertEqual(2, run.returncode)
                self.assertEqual("Checked", result["source_inspection"]["status"])
                self.assertEqual("GeneralToroidalKernelRadiusRange", result["diagnostics"][-1]["key"])
                self.assertIsNone(result["model"])
