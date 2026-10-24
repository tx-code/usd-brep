"""Source master composition has its own definition and image observations."""
import json
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214_entities import append_records
from tools.step_semantic_validation.tests.test_surface_curve import surface_curve_source
from tools.step_import_native_validation.tests.test_general import cylinder_source
from tools.step_import_native_validation.tests.test_revolution import rational_revolution_source


def spline_pcurve_source():
    # Plane S(u,v)=(2+v,-3+u,5). With a=(t+2)/10, the independently
    # authored quadratic is C(t)=(0,4a+6a*a), t in [-2,8]. It has the
    # same image as the finite auxiliary 3D segment, but different parameters.
    source = surface_curve_source().replace(
        b"#923=LINE('parameter line',#924,#925);",
        b"#923=B_SPLINE_CURVE_WITH_KNOTS('parameter quadratic',2,(#924,#927,#928),"
        b".UNSPECIFIED.,.F.,.F.,(3,3),(-2.,8.),.UNSPECIFIED.);")
    source = re.sub(rb"^#(?:925|926)=.*\n", b"", source, flags=re.M)
    # The two complete representations have the same finite point set, not
    # merely matching endpoint trims of an otherwise infinite auxiliary LINE.
    source = source.replace(b"#202=LINE('',#100,#201);",
                            b"#202=B_SPLINE_CURVE_WITH_KNOTS('',1,(#100,#101),.UNSPECIFIED.,.F.,.F.,(2,2),(0.,1.),.UNSPECIFIED.);")
    source = re.sub(rb"^#(?:200|201)=.*\n", b"", source, flags=re.M)
    return append_records(source, b"#927=CARTESIAN_POINT('',(0.,2.));\n#928=CARTESIAN_POINT('',(0.,10.));\n")


def axial_pcurve_source(revolution=False, milliradian=False):
    data = rational_revolution_source() if revolution else cylinder_source()
    angle = b"0." if revolution else b"1570.7963267948966192" if milliradian else b"1.5707963267948966192"
    if milliradian:
        data = data.replace(b"SI_UNIT($,.RADIAN.)", b"SI_UNIT(.MILLI.,.RADIAN.)")
    data = data.replace(b"#132=EDGE_CURVE('',#120,#121,#112,.T.)", b"#132=EDGE_CURVE('',#120,#121,#960,.T.)")
    data = data.replace(b"#112=LINE('',#102,#113);",
                        b"#112=B_SPLINE_CURVE_WITH_KNOTS('',1,(#102,#103),.UNSPECIFIED.,.F.,.F.,(2,2),(0.,1.),.UNSPECIFIED.);")
    data = re.sub(rb"^#113=.*\n", b"", data, flags=re.M)
    values = (b"-2.", b"0.", b"8.") if revolution else (b"0.", b"2.", b"7.")
    extra = b"""#960=SURFACE_CURVE('',#112,(#961),.PCURVE_S1.);
#961=PCURVE('',#142,#962);
#962=DEFINITIONAL_REPRESENTATION('',(#963),#970);
#963=B_SPLINE_CURVE_WITH_KNOTS('',2,(#964,#965,#966),.UNSPECIFIED.,.F.,.F.,(3,3),(-3.,9.),.UNSPECIFIED.);
#970=(GEOMETRIC_REPRESENTATION_CONTEXT(2) PARAMETRIC_REPRESENTATION_CONTEXT() REPRESENTATION_CONTEXT('','2D'));
"""
    for identity, v in zip((964, 965, 966), values):
        extra += b"#" + str(identity).encode() + b"=CARTESIAN_POINT('',(" + angle + b"," + v + b"));\n"
    return append_records(data, extra)


def surface_curve_trimmed_arc(trim_forward=True, edge_forward=True, master_3d=True, wrong_pcurve_sense=False):
    # The physical open patch is unchanged. Only the source wrapper/basis/edge
    # sense composition varies; this is public analytic input, not CAD data.
    path = Path(__file__).resolve().parents[3] / "source/STEP_IMPORT/tests/fixtures/source_pcurve_revolution_patch.step"
    data = path.read_bytes()
    low, high = b"1.8707963267948966", b"2.7707963267948966"
    if not trim_forward:
        low, high = high, low
    data = re.sub(rb"#112=TRIMMED_CURVE[^\n]*", b"#112=TRIMMED_CURVE('',#110,(PARAMETER_VALUE(" + low
                  + b")),(PARAMETER_VALUE(" + high + b")),." + (b"T" if trim_forward else b"F") + b".,.PARAMETER.);", data)
    basis_forward = trim_forward == edge_forward
    start, end = (b"120", b"121") if basis_forward else (b"121", b"120")
    data = data.replace(b"#130=EDGE_CURVE('',#120,#121,#112,.T.);",
                        b"#130=EDGE_CURVE('',#" + start + b",#" + end + b",#980,." + (b"T" if edge_forward else b"F") + b".);")
    data = data.replace(b"#150=ORIENTED_EDGE('',*,*,#130,.T.);",
                        b"#150=ORIENTED_EDGE('',*,*,#130,." + (b"T" if basis_forward else b"F") + b".);")
    first, last = (b"0.3", b"1.2") if trim_forward != wrong_pcurve_sense else (b"1.2", b"0.3")
    extra = b"#980=SURFACE_CURVE('',#112,(#981),." + (b"CURVE_3D" if master_3d else b"PCURVE_S1") + b""".);
#981=PCURVE('',#142,#982);
#982=DEFINITIONAL_REPRESENTATION('',(#983),#970);
#983=B_SPLINE_CURVE_WITH_KNOTS('',1,(#984,#985),.UNSPECIFIED.,.F.,.F.,(2,2),(0.,1.),.UNSPECIFIED.);
"""
    extra += b"#984=CARTESIAN_POINT('',(" + first + b",-2.));\n#985=CARTESIAN_POINT('',(" + last + b",-2.));\n"
    return append_records(data, extra)


class PcurveNativeTests(unittest.TestCase):
    def invoke(self, data):
        with tempfile.TemporaryDirectory(prefix="public-native-pcurve-") as folder:
            path = Path(folder) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", str(path),
                                  "--quality", "Q-AP214-development-mm-01", "--integration-relative-accuracy", "1e-8"],
                                 capture_output=True, timeout=60)
            self.assertEqual(data, path.read_bytes())
        self.assertTrue(run.stdout, run.stderr)
        return run, json.loads(run.stdout)

    def test_nonaffine_pcurve_master_keeps_original_parameters(self):
        run, report = self.invoke(spline_pcurve_source())
        self.assertEqual(0, run.returncode, report["diagnostics"])
        self.assertEqual("Accepted", report["status"])
        self.assertAlmostEqual(6000, report["model"]["material_volume"]["value_mm3"], delta=.001)
        mapping = next(m for m in report["parameter_mappings"] if m["kind"] == "SourcePcurveComposition")
        self.assertEqual([-2, 8], mapping["parameters"]["source_parameter_interval"])
        self.assertTrue(any(m["kind"] == "SurfaceCurveMasterPcurve" for m in report["parameter_mappings"]))
        auxiliary = [m for m in report["parameter_mappings"] if m["kind"].startswith("SurfaceCurveAuxiliary3d")]
        self.assertTrue(auxiliary)
        self.assertTrue(all(m["target_edge_index"] is None for m in auxiliary))

    def test_plane_parameters_use_basis_length_units(self):
        data = spline_pcurve_source().replace(b"SI_UNIT(.MILLI.,.METRE.)", b"SI_UNIT(.CENTI.,.METRE.)")
        run, report = self.invoke(data)
        self.assertEqual(0, run.returncode, report["diagnostics"])
        self.assertAlmostEqual(6000000, report["model"]["material_volume"]["value_mm3"], delta=.001)
        mapping = next(m for m in report["parameter_mappings"] if m["kind"] == "SourcePcurveComposition")
        self.assertEqual([10, 10], mapping["parameters"]["source_uv_to_native_step_scale"])

    def test_frozen_native_fixture_matches_the_public_analytic_construction(self):
        path = Path(__file__).resolve().parents[3] / "source/STEP_IMPORT/tests/fixtures/source_pcurve_box.step"
        self.assertEqual(spline_pcurve_source().replace(b"\r\n", b"\n"), path.read_bytes().replace(b"\r\n", b"\n"))

    def test_angular_and_linear_parameters_follow_cylinder_basis_units(self):
        for milliradian in (False, True):
            with self.subTest(milliradian=milliradian):
                run, report = self.invoke(axial_pcurve_source(milliradian=milliradian))
                self.assertEqual(0, run.returncode, report["diagnostics"])
                self.assertAlmostEqual(63 * math.pi, report["model"]["material_volume"]["value_mm3"], delta=.001)
                self.assertAlmostEqual(60 * math.pi, report["model"]["area_mm2"], delta=.001)

    def test_revolution_v_is_generator_parameter_and_not_physical_height(self):
        run, report = self.invoke(axial_pcurve_source(revolution=True))
        self.assertEqual(0, run.returncode, report["diagnostics"])
        self.assertAlmostEqual(63 * math.pi, report["model"]["material_volume"]["value_mm3"], delta=.001)
        self.assertAlmostEqual(60 * math.pi, report["model"]["area_mm2"], delta=.001)
        mapping = next(m for m in report["parameter_mappings"] if m["kind"] == "SourcePcurveComposition")
        self.assertEqual([-3, 9], mapping["parameters"]["source_parameter_interval"])
        self.assertEqual(1, mapping["parameters"]["source_uv_to_native_step_scale"][1])

    def test_revolution_patch_uses_bounded_projection_and_retains_source_role(self):
        path = (Path(__file__).resolve().parents[3] /
                "source/STEP_IMPORT/tests/fixtures/source_pcurve_revolution_patch.step")
        data = path.read_bytes()
        run, report = self.invoke(data)
        self.assertEqual(0, run.returncode, report["diagnostics"])
        self.assertAlmostEqual(3 * 7 * (.9), report["model"]["area_mm2"], delta=1e-4)
        self.assertEqual("NotApplicable", report["model"]["material_volume"])
        self.assertEqual(0, report["native_role"]["material_regions"])
        self.assertEqual("None", report["repair"])
        self.assertEqual(data.decode(), report["source_inspection"]["source"]["input"]["raw"])
        self.assertTrue(any(row["kind"] == "ControlledSourcePcurveUvProjection"
                            for row in report["representation_normalizations"]))
        mapping = next(row for row in report["parameter_mappings"] if row["kind"] == "SourcePcurveComposition")
        self.assertEqual([-3, 9], mapping["parameters"]["source_parameter_interval"])
        self.assertEqual(1, mapping["parameters"]["source_uv_to_native_step_scale"][1])
        self.assertEqual({"pointer_reports": 0, "level2_reports": 0}, report["validations"])

    def test_revolution_patch_source_gap_is_not_hidden_by_tighter_projection(self):
        path = (Path(__file__).resolve().parents[3] /
                "source/STEP_IMPORT/tests/fixtures/source_pcurve_revolution_patch.step")
        data = path.read_bytes().replace(b"(0.,3.,7.)", b"(0.,3.01,7.)")
        run, report = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report["model"])
        self.assertEqual("None", report["repair"])

    def test_unmarked_revolution_patch_uses_periodic_region_checks(self):
        path = (Path(__file__).resolve().parents[3] /
                "source/STEP_IMPORT/tests/fixtures/source_pcurve_revolution_patch.step")
        data = path.read_bytes().replace(b"#170=FACE_OUTER_BOUND", b"#170=FACE_BOUND")
        run, report = self.invoke(data)
        self.assertEqual(0, run.returncode, report["diagnostics"])
        self.assertAlmostEqual(18.9, report["model"]["area_mm2"], delta=1e-4)
        self.assertEqual("NotApplicable", report["model"]["material_volume"])
        self.assertTrue(any(row["kind"] == "NativeRevolutionCurveIntersectionContainment"
                            for row in report["representation_normalizations"]))
        self.assertEqual(data.decode(), report["source_inspection"]["source"]["input"]["raw"])

    def test_unmarked_revolution_solid_keeps_its_original_volume(self):
        data = axial_pcurve_source(revolution=True).replace(b"#172=FACE_OUTER_BOUND", b"#172=FACE_BOUND")
        run, report = self.invoke(data)
        self.assertEqual(0, run.returncode, report["diagnostics"])
        self.assertAlmostEqual(63 * math.pi, report["model"]["material_volume"]["value_mm3"], delta=.001)
        self.assertAlmostEqual(60 * math.pi, report["model"]["area_mm2"], delta=.001)
        self.assertTrue(any(row["kind"] == "NativeRevolutionCurveIntersectionContainment"
                            for row in report["representation_normalizations"]))
        wrong = data.replace(b"#182=ADVANCED_FACE('',(#172),#142,.T.)", b"#182=ADVANCED_FACE('',(#172),#142,.F.)")
        failed, rejected = self.invoke(wrong)
        self.assertNotEqual(0, failed.returncode)
        self.assertIsNone(rejected["model"])

    def test_bspline_basis_uses_original_uv_knots_not_physical_length_units(self):
        data = spline_pcurve_source().replace(
            b"#504=PLANE('',#503);",
            b"#504=B_SPLINE_SURFACE_WITH_KNOTS('',1,1,((#100,#101),(#103,#102)),"
            b".UNSPECIFIED.,.F.,.F.,.F.,(2,2),(2,2),(2.,4.),(-1.,1.),.UNSPECIFIED.);")
        data = data.replace(b"'body',(#21),#12", b"'body',(#21,#503),#12")
        data = data.replace(b"(0.,0.));\n#930", b"(2.,-1.));\n#930")
        data = data.replace(b"(0.,2.)", b"(2.,-0.6)").replace(b"(0.,10.)", b"(2.,1.)")
        data = data.replace(b"SI_UNIT(.MILLI.,.METRE.)", b"SI_UNIT(.CENTI.,.METRE.)")
        run, report = self.invoke(data)
        self.assertEqual(0, run.returncode, report["diagnostics"])
        self.assertAlmostEqual(6000000, report["model"]["material_volume"]["value_mm3"], delta=.001)
        mapping = next(m for m in report["parameter_mappings"] if m["kind"] == "SourcePcurveComposition")
        self.assertEqual([1, 1], mapping["parameters"]["source_uv_to_native_step_scale"])

    def test_second_occurrence_selection_does_not_deduplicate_associations(self):
        data = spline_pcurve_source().replace(b"(#921),.PCURVE_S1.", b"(#921,#921),.PCURVE_S2.")
        run, report = self.invoke(data)
        self.assertEqual(0, run.returncode, report["diagnostics"])
        master = next(m for m in report["parameter_mappings"] if m["kind"] == "SurfaceCurveMasterPcurve")
        self.assertEqual([2], master["parameters"]["selected_source_pcurve_occurrence"])

    def test_curve_3d_master_is_not_silently_replaced_by_a_pcurve(self):
        run, report = self.invoke(spline_pcurve_source().replace(b".PCURVE_S1.", b".CURVE_3D."))
        self.assertEqual(0, run.returncode, report["diagnostics"])
        self.assertEqual("Accepted", report["status"])
        self.assertAlmostEqual(6000, report["model"]["material_volume"]["value_mm3"], delta=.001)
        mappings = report["parameter_mappings"]
        master = next(m for m in mappings if m["kind"] == "SurfaceCurveMaster3d")
        self.assertIsNotNone(master["target_edge_index"])
        self.assertFalse(any(m["kind"] == "SurfaceCurveMasterPcurve" for m in mappings))
        comparison = [m for m in mappings if m["kind"].startswith("SurfaceCurveAuxiliaryPcurve")]
        self.assertTrue(comparison)
        self.assertTrue(all(m["target_edge_index"] is None for m in comparison))

    def test_curve_3d_master_revolution_keeps_original_3d_parameter(self):
        data = axial_pcurve_source(revolution=True).replace(b".PCURVE_S1.", b".CURVE_3D.")
        run, report = self.invoke(data)
        self.assertEqual(0, run.returncode, report["diagnostics"])
        master = next(m for m in report["parameter_mappings"] if m["kind"] == "SurfaceCurveMaster3d")
        self.assertEqual([0, 1], master["parameters"]["native_edge_interval"])
        self.assertAlmostEqual(63 * math.pi, report["model"]["material_volume"]["value_mm3"], delta=.001)

    def test_curve_3d_master_does_not_ignore_pcurve_image_conflicts(self):
        data = spline_pcurve_source().replace(b".PCURVE_S1.", b".CURVE_3D.").replace(b"(0.,2.)", b"(0.1,2.)")
        run, report = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report["model"])
        self.assertIn("GeneralSurfaceCurveRepresentationsConflict", {d["key"] for d in report["diagnostics"]})

    def test_curve_3d_master_does_not_sort_opposite_pcurve_sense(self):
        data = spline_pcurve_source().replace(b".PCURVE_S1.", b".CURVE_3D.")
        # Reverse the pcurve's source traversal while keeping the same point set.
        data = data.replace(b"2,(#924,#927,#928)", b"2,(#928,#927,#924)")
        run, report = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report["model"])
        self.assertIn("GeneralSurfaceCurveAssociationSenseConflict", {d["key"] for d in report["diagnostics"]})

    def test_curve_3d_master_preserves_duplicate_association_occurrences(self):
        data = spline_pcurve_source().replace(b"(#921),.PCURVE_S1.", b"(#921,#921),.CURVE_3D.")
        run, report = self.invoke(data)
        self.assertEqual(0, run.returncode, report["diagnostics"])
        mappings = [m for m in report["parameter_mappings"] if m["kind"] == "SurfaceCurveAuxiliaryPcurveSourcePcurveComposition"]
        self.assertEqual([[1], [2]], [m["parameters"]["source_association_occurrence"] for m in mappings])
        self.assertTrue(all(m["target_edge_index"] is None for m in mappings))

    def test_curve_3d_master_still_checks_the_second_associated_pcurve(self):
        data = spline_pcurve_source().replace(b"(#921),.PCURVE_S1.", b"(#921,#981),.CURVE_3D.")
        data = append_records(data, b"""#981=PCURVE('',#504,#982);
#982=DEFINITIONAL_REPRESENTATION('',(#983),#930);
#983=B_SPLINE_CURVE_WITH_KNOTS('',2,(#924,#984,#928),.UNSPECIFIED.,.F.,.F.,(3,3),(-2.,8.),.UNSPECIFIED.);
#984=CARTESIAN_POINT('',(0.1,2.));
""")
        run, report = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report["model"])
        self.assertIn("GeneralSurfaceCurveRepresentationsConflict", {d["key"] for d in report["diagnostics"]})

    def test_trimmed_surface_curve_composes_wrapper_edge_and_loop_senses(self):
        for trim in (False, True):
            for edge in (False, True):
                for master_3d in (False, True):
                    with self.subTest(trim=trim, edge=edge, master_3d=master_3d):
                        run, report = self.invoke(surface_curve_trimmed_arc(trim, edge, master_3d))
                        self.assertEqual(0, run.returncode, report["diagnostics"])
                        self.assertEqual("OpenSurfaceModel", report["model"]["shape_role"])
                        self.assertAlmostEqual(18.9, report["model"]["area_mm2"], delta=.001)

    def test_reverse_trim_cannot_hide_an_unrelated_pcurve_sense_conflict(self):
        run, report = self.invoke(surface_curve_trimmed_arc(False, True, True, True))
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report["model"])
        self.assertIn("GeneralSurfaceCurveAssociationSenseConflict", {d["key"] for d in report["diagnostics"]})

    def test_opposite_auxiliary_sense_is_not_sorted_or_flipped_away(self):
        data = spline_pcurve_source().replace(b"('',1,(#100,#101),", b"('',1,(#101,#100),")
        run, report = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertEqual("PostcheckFailure", report["status"])
        self.assertIn("GeneralSurfaceCurveAuxiliarySenseConflict", {d["key"] for d in report["diagnostics"]})

    def test_parameter_domain_evidence_failure_is_not_a_type_rejection(self):
        data = axial_pcurve_source(revolution=True).replace(b"#965=CARTESIAN_POINT('',(0.,0.))", b"#965=CARTESIAN_POINT('',(0.,9.))")
        run, report = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertEqual("Indeterminate", report["status"])
        self.assertIn("GeneralPcurveBasisDomainUnestablished", {d["key"] for d in report["diagnostics"]})

    def test_conflicting_image_is_rejected_even_when_endpoints_agree(self):
        data = spline_pcurve_source().replace(b"(0.,2.)", b"(0.1,2.)")
        run, report = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertEqual("PostcheckFailure", report["status"])
        self.assertIn("GeneralSurfaceCurveRepresentationsConflict", {d["key"] for d in report["diagnostics"]})

    def test_unselected_pcurve_conflict_is_still_checked(self):
        data = spline_pcurve_source().replace(b"(#921),.PCURVE_S1.", b"(#921,#981),.PCURVE_S1.")
        data = append_records(data, b"""#981=PCURVE('',#504,#982);
#982=DEFINITIONAL_REPRESENTATION('',(#983),#930);
#983=B_SPLINE_CURVE_WITH_KNOTS('',2,(#924,#984,#928),.UNSPECIFIED.,.F.,.F.,(3,3),(-2.,8.),.UNSPECIFIED.);
#984=CARTESIAN_POINT('',(0.1,2.));
""")
        run, report = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertEqual("PostcheckFailure", report["status"])
        self.assertIn("GeneralSurfaceCurveRepresentationsConflict", {d["key"] for d in report["diagnostics"]})

    def test_rational_parameter_coefficients_need_their_own_lift_contract(self):
        data = spline_pcurve_source().replace(
            b"B_SPLINE_CURVE_WITH_KNOTS('parameter quadratic',2,(#924,#927,#928),.UNSPECIFIED.,.F.,.F.,(3,3),(-2.,8.),.UNSPECIFIED.)",
            b"(BOUNDED_CURVE() B_SPLINE_CURVE(2,(#924,#927,#928),.UNSPECIFIED.,.F.,.F.) "
            b"B_SPLINE_CURVE_WITH_KNOTS((3,3),(-2.,8.),.UNSPECIFIED.) CURVE() GEOMETRIC_REPRESENTATION_ITEM() "
            b"RATIONAL_B_SPLINE_CURVE((1.,2.,1.)) REPRESENTATION_ITEM('parameter quadratic'))")
        run, report = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertEqual("Unsupported", report["status"])
        self.assertIn("GeneralPcurveParameterCurveUnsupported", {d["key"] for d in report["diagnostics"]})
