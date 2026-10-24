"""Source pcurve/master checks are separate from UV lifting and native qualification."""
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_interpretation import box_source, UNREGISTERED
from tools.step_semantic_validation.tests.test_ap214_entities import append_records

REGISTERED = b"AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}"


def surface_curve_source():
    data = box_source().replace(UNREGISTERED, REGISTERED)
    data = data.replace(b"#203=EDGE_CURVE('',#110,#111,#202,.T.);", b"#203=EDGE_CURVE('',#110,#111,#920,.T.);")
    # Public bottom plane: S(u,v)=(2+v,-3+u,5). Parameter-space C(t)=(0,10t)
    # lifts to the original x-directed edge for 0<=t<=1. Its parameter-space
    # curve has dim=2, while the PCURVE item itself belongs to a 3D representation.
    return append_records(data, b"""#920=SURFACE_CURVE('public master',#202,(#921),.PCURVE_S1.);
#921=PCURVE('source pcurve',#504,#922);
#922=DEFINITIONAL_REPRESENTATION('parameter definition',(#923),#930);
#923=LINE('parameter line',#924,#925);
#924=CARTESIAN_POINT('parameter origin',(0.,0.));
#925=VECTOR('parameter tangent',#926,10.);
#926=DIRECTION('parameter direction',(0.,1.));
#930=(GEOMETRIC_REPRESENTATION_CONTEXT(2) PARAMETRIC_REPRESENTATION_CONTEXT() REPRESENTATION_CONTEXT('parameter','2D'));
""")


def revolution_source(axis=b"$"):
    from tools.step_import_native_validation.tests.test_general import cylinder_source

    # The existing independent r=3, h=7 cylinder keeps all source boundaries.
    # Rotating its z-directed seam line about the z axis gives the same support,
    # with source angular zero at +Y. No fitted surface or importer output.
    data = cylinder_source().replace(b"CYLINDRICAL_SURFACE('',#106,3.)", b"SURFACE_OF_REVOLUTION('original revolution',#112,#941)")
    return append_records(data, b"#941=AXIS1_PLACEMENT('original axis',#100," + axis + b");\n")


class SurfaceCurveSourceTests(unittest.TestCase):
    def invoke(self, data, *flags):
        with tempfile.TemporaryDirectory(prefix="public-surface-curve-") as folder:
            path = Path(folder) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path), "--general-brep-source", *flags],
                                 capture_output=True, timeout=30)
            self.assertEqual(data, path.read_bytes())
        self.assertTrue(run.stdout, run.stderr)
        return run, json.loads(run.stdout)

    def rejected(self, data, key, exit_code=1):
        run, result = self.invoke(data)
        self.assertEqual(exit_code, run.returncode, run.stderr or result["diagnostics"])
        self.assertIsNone(result["snapshot"])
        self.assertEqual(key, result["diagnostics"][0]["key"])
        return result

    def test_valid_parameter_context_and_master_are_preserved(self):
        for associations, master in ((b"#921", b"PCURVE_S1"), (b"#921,#921", b"PCURVE_S2"), (b"#504", b"CURVE_3D")):
            data = surface_curve_source().replace(b"(#921),.PCURVE_S1.", b"(" + associations + b"),." + master + b".")
            if associations == b"#504":
                # A surface-only source has no parameter representation; do not
                # leave a synthetic orphan and call it a positive whole-source case.
                data = re.sub(rb"^#(?:921|922|923|924|925|926|930)=.*\n", b"", data, flags=re.M)
            run, result = self.invoke(data)
            self.assertEqual(0, run.returncode, run.stderr or result["diagnostics"])
            self.assertEqual("Checked", result["status"])
            raw = {r["id"]: r for r in result["source"]["records"]}
            fields = raw["920"]["components"][0]["parameters"]
            self.assertEqual(associations.decode().replace("#", "").split(","), [v["text"] for v in fields[2]["children"]])
            self.assertEqual(master.decode(), fields[3]["text"])
            self.assertEqual("Passed", result["checks"]["geometry_dependencies"])
            self.assertEqual("NotRun", result["checks"]["native_build"])

    def test_requested_master_requires_the_corresponding_pcurve_occurrence(self):
        self.rejected(surface_curve_source().replace(b"(#921),.PCURVE_S1.", b"(#504),.PCURVE_S1."),
                      "SourceRuleViolation:SURFACE_CURVE.WR2")
        self.rejected(surface_curve_source().replace(b".PCURVE_S1.", b".PCURVE_S2."),
                      "SourceRuleViolation:SURFACE_CURVE.WR3")

    def test_pcurve_definition_requires_one_curve_in_parametric_context(self):
        self.rejected(surface_curve_source().replace(b"(#923),#930", b"(#923,#924),#930"), "SourceRuleViolation:PCURVE.WR1")
        self.rejected(surface_curve_source().replace(b"(#923),#930", b"(#924),#930"), "SourceRuleViolation:PCURVE.WR2")
        self.rejected(surface_curve_source().replace(b"(#923),#930", b"(#923),#12"),
                      "SourceRuleViolation:DEFINITIONAL_REPRESENTATION.WR1")

    def test_parameter_curve_dimension_and_coordinate_compatibility_are_distinct(self):
        result = self.rejected(surface_curve_source().replace(b"(0.,0.)", b"(0.,0.,0.)"),
                               "SourceRuleViolation:COMPATIBLE_DIMENSION.WR1")
        issue = result["diagnostics"][0]
        self.assertEqual(("924", "coordinates", [2]), (issue["record_id"], issue["field"], issue["parameter_path"]))
        self.assertTrue(issue["related"])
        self.rejected(surface_curve_source().replace(b"(0.,1.)", b"(0.,1.,0.)"),
                      "SourceRuleViolation:COMPATIBLE_DIMENSION.WR2")

    def test_referenced_parameter_curve_must_have_dimension_two(self):
        data = surface_curve_source().replace(b"(0.,0.)", b"(0.,0.,0.)").replace(b"(0.,1.)", b"(0.,1.,0.)")
        data = data.replace(b"GEOMETRIC_REPRESENTATION_CONTEXT(2)", b"GEOMETRIC_REPRESENTATION_CONTEXT(3)")
        result = self.rejected(data, "SourceRuleViolation:PCURVE.WR3")
        issue = result["diagnostics"][0]
        self.assertEqual(("921", "reference_to_curve", [3]), (issue["record_id"], issue["field"], issue["parameter_path"]))

    def test_missing_geometric_parameter_context_does_not_borrow_the_body_context(self):
        data = surface_curve_source().replace(
            b"(GEOMETRIC_REPRESENTATION_CONTEXT(2) PARAMETRIC_REPRESENTATION_CONTEXT() REPRESENTATION_CONTEXT('parameter','2D'))",
            b"PARAMETRIC_REPRESENTATION_CONTEXT('parameter','2D')")
        self.rejected(data, "SourceRuleViolation:GEOMETRIC_REPRESENTATION_ITEM.WR1")

    def test_shared_definition_dag_is_bounded_by_source_work_not_expanded_paths(self):
        data = surface_curve_source().replace(b"'public master',#202", b"'public master',#1000")
        extra = []
        for level in range(24):
            curve = 1000 + 5 * level
            child = curve + 5 if level != 23 else 202
            extra.extend((f"#{curve}=COMPOSITE_CURVE('',(#{curve+1},#{curve+2}),.U.);\n",
                          f"#{curve+1}=COMPOSITE_CURVE_SEGMENT(.CONTINUOUS.,.T.,#{curve+3});\n",
                          f"#{curve+2}=COMPOSITE_CURVE_SEGMENT(.DISCONTINUOUS.,.T.,#{curve+3});\n",
                          f"#{curve+3}=TRIMMED_CURVE('',#{curve+4},(PARAMETER_VALUE(0.)),(PARAMETER_VALUE(1.)),.T.,.PARAMETER.);\n",
                          f"#{curve+4}=SURFACE_CURVE('',#{child},(#921,#921),.PCURVE_S1.);\n"))
        data = append_records(data, "".join(extra).encode())
        run, result = self.invoke(data, "--max-semantic-visits", "200000")
        self.assertEqual(0, run.returncode, run.stderr or result["diagnostics"])

    def test_forward_definition_cycles_are_not_representation_inverse_cycles(self):
        self.rejected(surface_curve_source().replace(b"'public master',#202", b"'public master',#920"), "SourceGeometryDependencyUnestablished", 2)
        self.rejected(surface_curve_source().replace(b"(#923),#930", b"(#921),#930"), "SourceGeometryDependencyUnestablished", 2)

    def test_source_checks_do_not_establish_geometric_agreement(self):
        # Same source types/dimensions, conflicting lifted boundary. Type/source
        # prerequisite success must not be advertised as continuous agreement.
        data = surface_curve_source().replace(b"(0.,0.)", b"(0.,1.)")
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result["diagnostics"])
        self.assertEqual("NotRun", result["checks"]["geometry_consistency"])
        self.assertEqual("NotRun", result["checks"]["native_build"])

    def test_a_definitional_representation_does_not_become_an_ownership_root(self):
        data = append_records(surface_curve_source(), b"#950=DEFINITIONAL_REPRESENTATION('orphan',(#923),#930);\n")
        run, result = self.invoke(data)
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnownedOrOrphanProfileContent", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])

    def test_revolution_retains_generator_axis_and_derived_default_without_source_records(self):
        for axis in (b"$", b"#104"):
            run, result = self.invoke(revolution_source(axis))
            self.assertEqual(0, run.returncode, run.stderr or result["diagnostics"])
            typed = {r["id"]: r for r in result["snapshot"]["records"]}
            self.assertIn("SWEPT_SURFACE", typed["142"]["types"])
            self.assertEqual(["name", "swept_curve", "axis_position"], [a["name"] for a in typed["142"]["attributes"]])
            raw = {r["id"]: r for r in result["source"]["records"]}
            fields = raw["941"]["components"][0]["parameters"]
            self.assertEqual("Unset" if axis == b"$" else "Reference", fields[2]["kind"])
            self.assertEqual("MaterialSolid", result["snapshot"]["part"]["source_shape_role"])

    def test_revolution_axis_dimensions_and_zero_direction_are_real_source_failures(self):
        data = append_records(revolution_source(), b"""#950=REPRESENTATION('axis-check',(#951),#954);
#951=AXIS1_PLACEMENT('invalid 2D axis',#952,$);
#952=CARTESIAN_POINT('',(0.,0.));
#954=(GEOMETRIC_REPRESENTATION_CONTEXT(2) PARAMETRIC_REPRESENTATION_CONTEXT() REPRESENTATION_CONTEXT('parameter','2D'));
""")
        self.rejected(data, "SourceRuleViolation:AXIS1_PLACEMENT.WR1")
        self.rejected(revolution_source(b"#104").replace(b"#104=DIRECTION('',(0.,0.,1.))", b"#104=DIRECTION('',(0.,0.,0.))"),
                      "SourceRuleViolation:DIRECTION.WR1")

    def test_surface_curve_three_dimensional_alternative_is_not_itself_a_pcurve(self):
        self.rejected(surface_curve_source().replace(b"'public master',#202", b"'public master',#921"),
                      "SourceRuleViolation:SURFACE_CURVE.WR4")

    def test_nongeometric_interoperability_cannot_waive_source_pcurve_failures(self):
        data = surface_curve_source().replace(b"(#921),.PCURVE_S1.", b"(#504),.PCURVE_S1.")
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(1, run.returncode)
        self.assertEqual("SourceRuleViolation:SURFACE_CURVE.WR2", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])

    def test_partial_source_budget_never_publishes_a_snapshot(self):
        run, result = self.invoke(surface_curve_source(), "--max-semantic-visits", "0")
        self.assertEqual(3, run.returncode)
        self.assertIsNone(result["snapshot"])

    def test_parameter_curve_lexemes_and_context_units_are_not_reinterpreted(self):
        data = surface_curve_source().replace(b".MILLI.", b".CENTI.")
        data = data.replace(b"LINE('parameter line',#924,#925)",
                            b"B_SPLINE_CURVE_WITH_KNOTS('parameter line',1,(#924,#927),.UNSPECIFIED.,.U.,.F.,(2,2),(-2.000E+0,+8.000E0),.UNSPECIFIED.)")
        data = data.replace(b"(0.,0.)", b"(-0.0000,+0.000E0)")
        data = re.sub(rb"^#(?:925|926)=.*\n", b"", data, flags=re.M)
        data = append_records(data, b"#927=CARTESIAN_POINT('parameter endpoint',(0.,+1.0000E+1));\n")
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result["diagnostics"])
        self.assertEqual(data.decode(), result["source"]["input"]["raw"])
        raw = {r["id"]: r for r in result["source"]["records"]}
        knots = raw["923"]["components"][0]["parameters"][7]["children"]
        self.assertEqual(["-2.000E+0", "+8.000E0"], [v["text"] for v in knots])
        self.assertEqual(["-0.0000", "+0.000E0"], [v["text"] for v in raw["924"]["components"][0]["parameters"][1]["children"]])
        context = next(r for r in result["snapshot"]["records"] if r["id"] == "930")
        self.assertIn("PARAMETRIC_REPRESENTATION_CONTEXT", context["types"])
        self.assertNotIn("GLOBAL_UNIT_ASSIGNED_CONTEXT", context["types"])

    @unittest.skipUnless(os.environ.get("STEP_NATIVE_BINARY"), "Requires matching native CLI")
    def test_source_success_does_not_skip_the_unimplemented_native_pcurve_path(self):
        data = surface_curve_source()
        with tempfile.TemporaryDirectory(prefix="public-native-pcurve-guard-") as folder:
            path = Path(folder) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", str(path), "--quality", "Q-AP214-development-mm-01"],
                                 capture_output=True, timeout=30)
        self.assertEqual(2, run.returncode, run.stderr)
        result = json.loads(run.stdout)
        self.assertIsNone(result["model"])
        self.assertEqual("GeneralUnsupportedCurve", result["diagnostics"][-1]["key"])
