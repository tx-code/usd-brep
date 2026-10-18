# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Trimmed conic definitions remain distinct from the topological edge interval."""
import math
import unittest

from tools.step_import_native_validation.tests import test_general


def trimmed_cylinder_source(sense=True):
    data = test_general.cylinder_source()
    replacement = (b"#110=TRIMMED_CURVE('',#114,(PARAMETER_VALUE(0.0)),"
                   b"(PARAMETER_VALUE(6.283185307179586)),.T.,.PARAMETER.);\n"
                   b"#114=ELLIPSE('',#106,3.,3.);")
    data = data.replace(b"#110=CIRCLE('',#106,3.);", replacement)
    if not sense:
        data = data.replace(b"(PARAMETER_VALUE(0.0)),(PARAMETER_VALUE(6.283185307179586)),.T.,.PARAMETER.",
                            b"(PARAMETER_VALUE(6.283185307179586)),(PARAMETER_VALUE(0.0)),.F.,.PARAMETER.")
        data = data.replace(b"#130=EDGE_CURVE('',#120,#120,#110,.T.)", b"#130=EDGE_CURVE('',#120,#120,#110,.F.)")
    return data


def elliptical_cylinder_source():
    # Independent elliptical cylinder: x radius 4, y radius 2, height 7.
    # Extrude four standard rational quadratic ellipse quadrants. sqrt(1/2)
    # is rounded as a source decimal; its error is far below the fixture budget.
    data = trimmed_cylinder_source().replace(b"#114=ELLIPSE('',#106,3.,3.)", b"#114=ELLIPSE('',#106,4.,2.)")
    data = data.replace(b"#111=CIRCLE('',#107,3.)", b"#111=ELLIPSE('',#107,4.,2.)")
    data = data.replace(b"(0.,3.,0.)", b"(4.,0.,0.)").replace(b"(0.,3.,7.)", b"(4.,0.,7.)")
    net, records, weights = [], [], []
    xy = [(4, 0), (4, 2), (0, 2), (-4, 2), (-4, 0), (-4, -2), (0, -2), (4, -2), (4, 0)]
    for i, (x, y) in enumerate(xy):
        row = []
        for j, z in enumerate([0, 7]):
            index = 600 + i * 2 + j
            records.append(f"#{index}=CARTESIAN_POINT('',({x}.,{y}.,{z}.));\n")
            row.append(f"#{index}")
        net.append("(" + ",".join(row) + ")")
        weight = "1." if i % 2 == 0 else "0.7071067811865475244"
        weights.append(f"({weight},{weight})")
    surface = ("(BOUNDED_SURFACE() B_SPLINE_SURFACE(2,1,(" + ",".join(net)
               + "),.UNSPECIFIED.,.T.,.F.,.F.) "
               "B_SPLINE_SURFACE_WITH_KNOTS((3,2,2,2,3),(2,2),(0.,1.,2.,3.,4.),(0.,1.),.UNSPECIFIED.) "
               "GEOMETRIC_REPRESENTATION_ITEM() RATIONAL_B_SPLINE_SURFACE((" + ",".join(weights)
               + ")) REPRESENTATION_ITEM('ellipse extrusion') SURFACE())")
    data = data.replace(b"CYLINDRICAL_SURFACE('',#106,3.)", surface.encode("ascii"))
    return data.replace(b"ENDSEC;\nEND-ISO", "".join(records).encode("ascii") + b"ENDSEC;\nEND-ISO")


class TrimmedNativeTests(unittest.TestCase):
    invoke_bytes = test_general.GeneralTests.invoke_bytes

    def test_trimmed_ellipse_keeps_source_sense_and_independent_cylinder_volume(self):
        for sense in [True, False]:
            with self.subTest(sense=sense):
                data = trimmed_cylinder_source(sense)
                run, result = self.invoke_bytes(data)
                self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
                self.assertAlmostEqual(63 * math.pi, result["model"]["material_volume"]["value_mm3"], delta=63 * math.pi * 2e-4)
                self.assertEqual((3, 3, 2), tuple(result["model"][k] for k in ["faces", "edges", "vertices"]))
                self.assertEqual(data.decode("ascii"), result["source_inspection"]["source"]["input"]["raw"])
                traces = [x for x in result["parameter_mappings"] if x["kind"] == "TrimmedConicSourceDomain"]
                self.assertEqual(1, len(traces))
                self.assertEqual(("110", "114", "130"), (traces[0]["source_geometry"], traces[0]["source_basis_geometry"], traces[0]["source_use"]))

    def test_dual_representations_are_checked_even_when_parameter_is_master(self):
        source = trimmed_cylinder_source().replace(b"(PARAMETER_VALUE(0.0))", b"(PARAMETER_VALUE(0.0),#102)")
        run, result = self.invoke_bytes(source)
        self.assertNotEqual(0, run.returncode)
        self.assertEqual("GeneralTrimmedRepresentationsConflict", result["diagnostics"][-1]["key"])
        self.assertIsNone(result["model"])

    def test_unequal_axes_and_prefixed_angle_units_keep_independent_ellipse_volume(self):
        original = elliptical_cylinder_source()
        for milli in [False, True]:
            with self.subTest(milliradian=milli):
                data = original
                if milli:
                    data = data.replace(b"SI_UNIT($,.RADIAN.)", b"SI_UNIT(.MILLI.,.RADIAN.)")
                    data = data.replace(b"PARAMETER_VALUE(6.283185307179586)", b"PARAMETER_VALUE(6283.185307179586)")
                run, result = self.invoke_bytes(data)
                self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
                self.assertAlmostEqual(56 * math.pi, result["model"]["material_volume"]["value_mm3"],
                                       delta=56 * math.pi * 2e-4)
                self.assertEqual({"pointer_reports": 0, "level2_reports": 0}, result["validations"])

    def test_source_trim_restriction_is_not_discarded_in_favour_of_edge_vertices(self):
        source = trimmed_cylinder_source().replace(b"PARAMETER_VALUE(6.283185307179586)", b"PARAMETER_VALUE(3.141592653589793)")
        run, result = self.invoke_bytes(source)
        self.assertNotEqual(0, run.returncode)
        self.assertEqual("GeneralTrimmedClosedEdgeRequiresFullConic", result["diagnostics"][-1]["key"])
        self.assertIsNone(result["model"])

    def test_partial_trim_compares_both_forms_and_preserves_edge_subset_and_sense(self):
        original = test_general.partitioned_cylinder_source()
        for sense in [True, False]:
            for master in [b"PARAMETER", b"CARTESIAN", b"UNSPECIFIED"]:
                with self.subTest(sense=sense, master=master):
                    # The physical edge is the third quadrant; the wrapper may
                    # be wider. All truth comes from the analytic cylinder.
                    first, last = b"PARAMETER_VALUE(3.141592653589793),#302", b"#303,PARAMETER_VALUE(4.71238898038469)"
                    if not sense:
                        first, last = last, first
                    replacement = (b"#308=TRIMMED_CURVE('',#350,(" + first + b"),(" + last + b"),"
                                   + (b".T." if sense else b".F.") + b",." + master + b".);\n"
                                   b"#350=CIRCLE('',#306,3.);")
                    source = original.replace(b"#308=CIRCLE('',#306,3.);", replacement)
                    if not sense:
                        source = source.replace(b"#320=EDGE_CURVE('',#310,#311,#308,.T.)",
                                                b"#320=EDGE_CURVE('',#310,#311,#308,.F.)")
                    run, result = self.invoke_bytes(source)
                    self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
                    self.assertAlmostEqual(63 * math.pi, result["model"]["material_volume"]["value_mm3"],
                                           delta=63 * math.pi * 2e-4)
                    self.assertEqual((4, 7, 6), tuple(result["model"][k] for k in ["faces", "edges", "vertices"]))

    def test_trimmed_basis_and_multi_period_ranges_remain_explicitly_unsupported(self):
        source = trimmed_cylinder_source()
        for changed, key in [
            (source.replace(b"#114=ELLIPSE('',#106,3.,3.);", b"#114=LINE('',#102,#113);"),
             "GeneralTrimmedBasisUnsupported"),
            (source.replace(b"6.283185307179586", b"12.566370614359172"), "GeneralTrimmedConicExtentUnestablished"),
            (source.replace(b"#114=ELLIPSE('',#106,3.,3.);", b"#114=ELLIPSE('',#106,1.E-14,3.);"),
             "GeneralTrimmedConicRadiusRange"),
        ]:
            with self.subTest(key=key):
                run, result = self.invoke_bytes(changed)
                self.assertNotEqual(0, run.returncode)
                self.assertEqual(key, result["diagnostics"][-1]["key"])
                self.assertIsNone(result["model"])

    def test_edge_may_use_a_subset_but_cannot_extend_the_source_trim(self):
        original = test_general.partitioned_cylinder_source()
        for end, expected in [(b"6.283185307179586", None), (b"4.0", "GeneralTrimmedEdgeOutsideSourceDomain")]:
            with self.subTest(end=end):
                replacement = (b"#308=TRIMMED_CURVE('',#350,(PARAMETER_VALUE(3.141592653589793)),"
                               b"(PARAMETER_VALUE(" + end + b")),.T.,.PARAMETER.);\n#350=CIRCLE('',#306,3.);")
                source = original.replace(b"#308=CIRCLE('',#306,3.);", replacement)
                run, result = self.invoke_bytes(source)
                if expected:
                    self.assertNotEqual(0, run.returncode)
                    self.assertEqual(expected, result["diagnostics"][-1]["key"])
                    self.assertIsNone(result["model"])
                else:
                    self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
                    self.assertAlmostEqual(63 * math.pi, result["model"]["material_volume"]["value_mm3"],
                                           delta=63 * math.pi * 2e-4)
