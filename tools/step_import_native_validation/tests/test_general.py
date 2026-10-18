# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""General native construction has its own scope; legacy qualification stays separate."""
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

CORPUS = Path(__file__).resolve().parents[2] / "step_import_validation/corpus/ap203e2-planar/v1/cases"


def cylinder_source(implicit_seam=False):
    # Independent analytic fixture: radius 3 mm, height 7 mm, seam at +Y.
    # Reuse only the product/unit carrier, not geometry or an importer output.
    prefix = (CORPUS / "S203-P04/input.step").read_bytes().split(b"#22=")[0]
    prefix = prefix.replace(
        b"AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF {1 0 10303 403 2 1 2}",
        b"AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}").replace(
        b"'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011",
        b"'automotive_design',2010")
    records = """#22=CLOSED_SHELL('',(#180,#181,#182));
#100=CARTESIAN_POINT('',(0.,0.,0.));
#101=CARTESIAN_POINT('',(0.,0.,7.));
#102=CARTESIAN_POINT('',(0.,3.,0.));
#103=CARTESIAN_POINT('',(0.,3.,7.));
#104=DIRECTION('',(0.,0.,1.));
#105=DIRECTION('',(1.,0.,0.));
#106=AXIS2_PLACEMENT_3D('',#100,#104,#105);
#107=AXIS2_PLACEMENT_3D('',#101,#104,#105);
#110=CIRCLE('',#106,3.);
#111=CIRCLE('',#107,3.);
#120=VERTEX_POINT('',#102);
#121=VERTEX_POINT('',#103);
#130=EDGE_CURVE('',#120,#120,#110,.T.);
#131=EDGE_CURVE('',#121,#121,#111,.T.);
#140=PLANE('',#106);
#141=PLANE('',#107);
#142=CYLINDRICAL_SURFACE('',#106,3.);
#150=ORIENTED_EDGE('',*,*,#130,.F.);
#151=ORIENTED_EDGE('',*,*,#131,.T.);
#152=ORIENTED_EDGE('',*,*,#130,.T.);
#154=ORIENTED_EDGE('',*,*,#131,.F.);
#160=EDGE_LOOP('',(#150));
#161=EDGE_LOOP('',(#151));
#170=FACE_OUTER_BOUND('',#160,.T.);
#171=FACE_OUTER_BOUND('',#161,.T.);
#172=FACE_OUTER_BOUND('',#162,.T.);
#180=ADVANCED_FACE('',(#170),#140,.F.);
#181=ADVANCED_FACE('',(#171),#141,.T.);
"""
    if implicit_seam:
        records += """#162=EDGE_LOOP('',(#152));
#163=EDGE_LOOP('',(#154));
#173=FACE_BOUND('',#163,.T.);
#182=ADVANCED_FACE('',(#172,#173),#142,.T.);
"""
    else:
        records += """#112=LINE('',#102,#113);
#113=VECTOR('',#104,7.);
#132=EDGE_CURVE('',#120,#121,#112,.T.);
#153=ORIENTED_EDGE('',*,*,#132,.T.);
#155=ORIENTED_EDGE('',*,*,#132,.F.);
#162=EDGE_LOOP('',(#152,#153,#154,#155));
#182=ADVANCED_FACE('',(#172),#142,.T.);
"""
    return prefix + records.encode("ascii") + b"ENDSEC;\nEND-ISO-10303-21;\n"


def bulged_box_source():
    # Only the top face changes. Its Bernstein height is
    # z=35+80*u*(1-u)*v*(1-v): the interior maximum is 40, while
    # every boundary remains at 35. The exact added volume is 4000/9.
    # An edge-only box therefore cannot satisfy this independent control.
    data = (CORPUS / "S203-P04/input.step").read_bytes()
    points = []
    for i, x in enumerate((2, 7, 12)):
        for j, y in enumerate((-3, 7, 17)):
            z = 55 if i == j == 1 else 35
            points.append(f"#{600 + 3 * i + j}=CARTESIAN_POINT('',({x}.,{y}.,{z}.));\n")
    surface = (b"B_SPLINE_SURFACE_WITH_KNOTS('analytic bulge',2,2,"
               b"((#600,#601,#602),(#603,#604,#605),(#606,#607,#608)),"
               b".UNSPECIFIED.,.F.,.F.,.F.,(3,3),(3,3),(0.,1.),(0.,1.),.UNSPECIFIED.)")
    assert data.count(b"PLANE('',#513)") == 1
    data = data.replace(b"PLANE('',#513)", surface)
    # The old placement stays an explicitly retained auxiliary item.
    data = data.replace(b"'body',(#21),#12", b"'body',(#21,#513),#12")
    return data.replace(b"ENDSEC;\nEND-ISO", "".join(points).encode("ascii") + b"ENDSEC;\nEND-ISO")


def conical_frustum_source():
    # Independent 3-4-5 frustum: radii 3/6, height 4, slant 5.
    # The STEP angle is atan(3/4); its decimal rounds far below the mm budget.
    data = cylinder_source().replace(b"(0.,0.,7.)", b"(0.,0.,4.)")
    data = data.replace(b"(0.,3.,7.)", b"(0.,6.,4.)")
    data = data.replace(b"CIRCLE('',#107,3.)", b"CIRCLE('',#107,6.)")
    data = data.replace(b"CYLINDRICAL_SURFACE('',#106,3.)",
                        b"CONICAL_SURFACE('',#106,3.,0.6435011087932843868)")
    data = data.replace(b"VECTOR('',#104,7.)", b"VECTOR('',#114,5.)")
    return data.replace(b"ENDSEC;\nEND-ISO", b"#114=DIRECTION('',(0.,3.,4.));\nENDSEC;\nEND-ISO")


def partitioned_cylinder_source():
    # Partition an analytic cylinder's side into a face with a rectangular UV
    # hole and an exact filling face on the SAME support. Material and total
    # area stay 63*pi/60*pi; the source intentionally has 4 faces/7 edges/6 VPs.
    data = cylinder_source().replace(b"(#180,#181,#182)", b"(#180,#181,#182,#344)")
    data = data.replace(b"#172=FACE_OUTER_BOUND", b"#172=FACE_BOUND")
    data = data.replace(b"#182=ADVANCED_FACE('',(#172),", b"#182=ADVANCED_FACE('',(#172,#342),")
    extra = b"""#300=CARTESIAN_POINT('',(0.,0.,2.));
#301=CARTESIAN_POINT('',(0.,0.,5.));
#302=CARTESIAN_POINT('',(-3.,0.,2.));
#303=CARTESIAN_POINT('',(0.,-3.,2.));
#304=CARTESIAN_POINT('',(0.,-3.,5.));
#305=CARTESIAN_POINT('',(-3.,0.,5.));
#306=AXIS2_PLACEMENT_3D('',#300,#104,#105);
#307=AXIS2_PLACEMENT_3D('',#301,#104,#105);
#308=CIRCLE('',#306,3.);
#309=CIRCLE('',#307,3.);
#310=VERTEX_POINT('',#302);
#311=VERTEX_POINT('',#303);
#312=VERTEX_POINT('',#304);
#313=VERTEX_POINT('',#305);
#314=VECTOR('',#104,3.);
#315=DIRECTION('',(0.,0.,-1.));
#316=VECTOR('',#315,3.);
#317=LINE('',#303,#314);
#318=LINE('',#305,#316);
#320=EDGE_CURVE('',#310,#311,#308,.T.);
#321=EDGE_CURVE('',#311,#312,#317,.T.);
#322=EDGE_CURVE('',#313,#312,#309,.T.);
#323=EDGE_CURVE('',#313,#310,#318,.T.);
#330=ORIENTED_EDGE('',*,*,#320,.T.);
#331=ORIENTED_EDGE('',*,*,#321,.T.);
#332=ORIENTED_EDGE('',*,*,#322,.F.);
#333=ORIENTED_EDGE('',*,*,#323,.T.);
#334=ORIENTED_EDGE('',*,*,#323,.F.);
#335=ORIENTED_EDGE('',*,*,#322,.T.);
#336=ORIENTED_EDGE('',*,*,#321,.F.);
#337=ORIENTED_EDGE('',*,*,#320,.F.);
#340=EDGE_LOOP('',(#330,#331,#332,#333));
#341=EDGE_LOOP('',(#334,#335,#336,#337));
#342=FACE_BOUND('',#341,.T.);
#343=FACE_OUTER_BOUND('',#340,.T.);
#344=ADVANCED_FACE('',(#343),#142,.T.);
"""
    return data.replace(b"ENDSEC;\nEND-ISO", extra + b"ENDSEC;\nEND-ISO")


class GeneralTests(unittest.TestCase):
    def invoke_bytes(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="step-general-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality",
                                  "Q-AP214-development-mm-01", str(path), *map(str, options)],
                                 capture_output=True, timeout=30)
        return run, json.loads(run.stdout) if run.stdout else None

    def invoke(self, case="S203-P04", *options):
        run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality",
                              "Q-AP214-development-mm-01", str(CORPUS / case / "input.step"),
                              *map(str, options)], capture_output=True, timeout=30)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_box_has_independent_analytic_measures_and_original_source(self):
        run, result = self.invoke()
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual("GeneralBrepDevelopment", result["scope"])
        self.assertFalse(result["full_schema_conformance"])
        self.assertEqual({"pointer_reports": 0, "level2_reports": 0}, result["validations"])
        model = result["model"]
        self.assertEqual((6, 12, 8), (model["faces"], model["edges"], model["vertices"]))
        self.assertAlmostEqual(6000, model["material_volume"]["value_mm3"], places=5)
        self.assertAlmostEqual(2200, model["area_mm2"], places=5)
        for expected, actual in zip(((2, -3, 5), (12, 17, 35)), model["bounds_mm"]):
            for a, b in zip(expected, actual):
                self.assertAlmostEqual(a, b, places=5)
        raw = (CORPUS / "S203-P04/input.step").read_bytes()
        self.assertEqual(raw.decode("ascii"), result["source_inspection"]["source"]["input"]["raw"])

    def test_surface_role_is_not_promoted_to_material(self):
        run, result = self.invoke("S203-P01")
        self.assertEqual(9, run.returncode)
        self.assertIsNone(result["model"])
        self.assertEqual(0, result["native_calls"])
        self.assertEqual("GeneralRequiresMaterialSolid", result["diagnostics"][-1]["key"])

    def test_tight_bounds_include_curved_face_interior_extrema(self):
        data = bulged_box_source()
        run, result = self.invoke_bytes(data)
        self.assertEqual(0, run.returncode, run.stderr or result)
        model = result["model"]
        self.assertEqual((6, 12, 8), tuple(model[key] for key in ("faces", "edges", "vertices")))
        self.assertAlmostEqual(6000 + 4000 / 9, model["material_volume"]["value_mm3"], delta=0.02)
        for expected, actual in zip(((2, -3, 5), (12, 17, 40)), model["bounds_mm"]):
            for a, b in zip(expected, actual):
                self.assertAlmostEqual(a, b, delta=1e-6)
        self.assertEqual(data.decode("ascii"), result["source_inspection"]["source"]["input"]["raw"])

    def test_native_call_limit_discards_private_candidate(self):
        run, result = self.invoke("S203-P04", "--max-native-calls", 3)
        self.assertEqual(3, run.returncode)
        self.assertIsNone(result["model"])
        self.assertEqual("NativeCallLimit", result["diagnostics"][-1]["key"])
        self.assertTrue(result["candidate_discarded"])

    def test_bounded_report_never_publishes_a_partial_model(self):
        run, result = self.invoke("S203-P04", "--max-report-bytes", 1)
        self.assertEqual(5, run.returncode)
        self.assertEqual(b"", run.stdout)
        self.assertIsNone(result)

    def test_raw_failures_keep_io_resource_and_carrier_categories(self):
        run, result = self.invoke("S203-P04", "--max-input-bytes", 1)
        self.assertEqual((3, "ResourceLimit"), (run.returncode, result["status"]))
        run, result = self.invoke("missing-file")
        self.assertEqual((4, "SourceFailure"), (run.returncode, result["status"]))
        run, result = self.invoke_bytes(b"PK\x03\x04not-part21")
        self.assertEqual((2, "Unsupported"), (run.returncode, result["status"]))

    def test_final_trim_measurements_and_parameter_maps_are_published(self):
        run, result = self.invoke_bytes(cylinder_source())
        self.assertEqual(0, run.returncode, run.stderr or result)
        measurements = [row for row in result["measurements"] if row["stage"] == "FinalInstalledUv"]
        self.assertEqual(3, len(measurements))
        self.assertTrue(all(row["value_mm"] < 0.001 for row in measurements))
        maps = result["parameter_mappings"]
        self.assertEqual(3, len(maps))
        self.assertEqual({0, 1, 2}, {row["target_edge_index"] for row in maps})
        self.assertEqual(2, sum(row["kind"] == "ConicRationalParameter" for row in maps))
        for row in maps:
            self.assertIn("native_edge_interval", row["parameters"])

    def test_analytic_cylinder_source_seam_is_aligned_without_new_topology(self):
        run, result = self.invoke_bytes(cylinder_source())
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual([], result["representation_normalizations"])
        model = result["model"]
        self.assertEqual((3, 3, 2), (model["faces"], model["edges"], model["vertices"]))
        self.assertAlmostEqual(60 * math.pi, model["area_mm2"], delta=60 * math.pi * 2e-4)
        self.assertAlmostEqual(63 * math.pi, model["material_volume"]["value_mm3"], delta=63 * math.pi * 2e-4)

    def test_implicit_cylinder_seam_preserves_two_source_rings_and_records_one_cut(self):
        data = cylinder_source(implicit_seam=True)
        run, result = self.invoke_bytes(data)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual((3, 3, 2), tuple(result["model"][key] for key in ("faces", "edges", "vertices")))
        changes = result["representation_normalizations"]
        self.assertEqual(1, len(changes))
        self.assertEqual("182", changes[0]["source_face"])
        self.assertEqual(["172", "173"], changes[0]["source_bounds"])
        self.assertFalse(changes[0]["source_rewritten"])
        self.assertEqual(data.decode("ascii"), result["source_inspection"]["source"]["input"]["raw"])
        self.assertAlmostEqual(63 * math.pi, result["model"]["material_volume"]["value_mm3"], delta=63 * math.pi * 2e-4)

    def test_unmarked_cylinder_band_is_classified_after_its_explicit_seam_cut(self):
        data = cylinder_source(implicit_seam=True).replace(b"#172=FACE_OUTER_BOUND", b"#172=FACE_BOUND")
        explicit = cylinder_source().replace(b"#172=FACE_OUTER_BOUND", b"#172=FACE_BOUND")
        for source in (data, data.replace(b"(#172,#173)", b"(#173,#172)"), explicit):
            with self.subTest(source=source[-90:]):
                run, result = self.invoke_bytes(source)
                self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
                self.assertEqual((3, 3, 2), tuple(result["model"][key] for key in ("faces", "edges", "vertices")))
                self.assertAlmostEqual(63 * math.pi, result["model"]["material_volume"]["value_mm3"], delta=63 * math.pi * 2e-4)
                self.assertAlmostEqual(60 * math.pi, result["model"]["area_mm2"], delta=60 * math.pi * 2e-4)
                self.assertEqual(source.decode("ascii"), result["source_inspection"]["source"]["input"]["raw"])
                # A cut rectangle is derived from BOTH source rings. Neither
                # original periodic bound is falsely promoted to source outer.
                self.assertFalse(any(row["source_face"] == "182" for row in result["classified_outer_bounds"]))
                (region,) = [row for row in result["representation_normalizations"]
                             if row["kind"] == "NativeCylindricalCurveIntersectionContainment"]
                self.assertEqual("182", region["source_face"])
                self.assertEqual({"172", "173"} if source != explicit else {"172"}, set(region["source_bounds"]))
                self.assertFalse(region["source_rewritten"])

    def test_conical_frustum_control_has_independent_area_and_volume(self):
        run, result = self.invoke_bytes(conical_frustum_source())
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertAlmostEqual(84 * math.pi, result["model"]["material_volume"]["value_mm3"], delta=84 * math.pi * 2e-4)
        self.assertAlmostEqual(90 * math.pi, result["model"]["area_mm2"], delta=90 * math.pi * 2e-4)

    def test_unmarked_conical_frustum_preserves_the_same_material_region(self):
        source = conical_frustum_source().replace(b"#172=FACE_OUTER_BOUND", b"#172=FACE_BOUND")
        run, result = self.invoke_bytes(source)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual((3, 3, 2), tuple(result["model"][key] for key in ("faces", "edges", "vertices")))
        self.assertAlmostEqual(84 * math.pi, result["model"]["material_volume"]["value_mm3"], delta=84 * math.pi * 2e-4)
        self.assertAlmostEqual(90 * math.pi, result["model"]["area_mm2"], delta=90 * math.pi * 2e-4)
        self.assertEqual(source.decode("ascii"), result["source_inspection"]["source"]["input"]["raw"])

    def test_unmarked_region_keeps_orientation_and_support_guards(self):
        wrong = cylinder_source().replace(b"#172=FACE_OUTER_BOUND", b"#172=FACE_BOUND")
        wrong = wrong.replace(b"#182=ADVANCED_FACE('',(#172),#142,.T.)", b"#182=ADVANCED_FACE('',(#172),#142,.F.)")
        run, report = self.invoke_bytes(wrong)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report["model"])
        unsupported = bulged_box_source().replace(b"#516=FACE_OUTER_BOUND", b"#516=FACE_BOUND")
        run, report = self.invoke_bytes(unsupported)
        self.assertEqual(7, run.returncode)
        self.assertEqual("GeneralRegionClassificationPending", report["diagnostics"][-1]["key"])
        self.assertIsNone(report["model"])

    def test_unmarked_cylinder_region_preserves_inner_loop_and_shared_partition(self):
        data = partitioned_cylinder_source()
        for source in (data, data.replace(b"(#172,#342)", b"(#342,#172)")):
            with self.subTest(reversed_set=source != data):
                run, result = self.invoke_bytes(source)
                self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
                self.assertEqual((4, 7, 6), tuple(result["model"][key] for key in ("faces", "edges", "vertices")))
                self.assertAlmostEqual(63 * math.pi, result["model"]["material_volume"]["value_mm3"], delta=63 * math.pi * 2e-4)
                self.assertAlmostEqual(60 * math.pi, result["model"]["area_mm2"], delta=60 * math.pi * 2e-4)
                self.assertEqual(source.decode("ascii"), result["source_inspection"]["source"]["input"]["raw"])
