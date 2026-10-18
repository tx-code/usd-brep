# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Date ownership and rendering source errors through the real semantic CLI."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import ap214_source, CORPUS
from tools.step_semantic_validation.tests.test_ap214_entities import append_records
from tools.step_semantic_validation.tests.test_ap214_units import unit_source, conversion_mass_source


def date_source():
    data = ap214_source((CORPUS / "S203-P04/input.step").read_bytes())
    return append_records(data, b"""#940=DATE_TIME_ROLE('creation_date');
#941=APPLIED_DATE_AND_TIME_ASSIGNMENT(#942,#940,(#7));
#942=DATE_AND_TIME(#943,#944);
#943=CALENDAR_DATE(+2024,29,2);
#944=LOCAL_TIME(12,30,+17.000E+0,#945);
#945=COORDINATED_UNIVERSAL_TIME_OFFSET(0,$,.BEHIND.);
""")


def rendering_source(method=b".NORMAL_SHADING."):
    return append_records(date_source(), b"""#950=MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('display',(#951),#12);
#951=STYLED_ITEM('source style',(#952),#21);
#952=PRESENTATION_STYLE_ASSIGNMENT((#953));
#953=SURFACE_STYLE_USAGE(.BOTH.,#954);
#954=SURFACE_SIDE_STYLE('surface',(#955));
#955=SURFACE_STYLE_RENDERING_WITH_PROPERTIES(""" + method + b""",#957,(#956));
#956=SURFACE_STYLE_TRANSPARENT(+0.000E+0);
#957=COLOUR_RGB('RGB',0.2,0.4,0.6);
""")


def property_mass_source(conversion=False, physical=False):
    data = conversion_mass_source() if conversion else unit_source()
    if not physical:
        data = data.replace(b"GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11,#903))", b"GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11))")
    if conversion:
        data = data.replace(b"'source mass',#904) MASS_UNIT() NAMED_UNIT(*)", b"'source mass',#904) MASS_UNIT() NAMED_UNIT(#960)")
        data = append_records(data, b"#960=DIMENSIONAL_EXPONENTS(0.,1.,0.,0.,0.,0.,0.);\n")
    else:
        data = data.replace(b"SI_UNIT(.KILO.,.GRAM.)", b"SI_UNIT($,.GRAM.)")
    return append_records(data, b"""#950=PROPERTY_DEFINITION('density property',$,#7);
#951=PROPERTY_DEFINITION_REPRESENTATION(#950,#952);
#952=REPRESENTATION('density',(#953),#12);
#953=MEASURE_REPRESENTATION_ITEM('density',POSITIVE_RATIO_MEASURE(1.),#903);
""")


class Ap214MetadataTests(unittest.TestCase):
    def invoke(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="step-ap214-metadata-") as directory:
            path = Path(directory) / "input.step"
            path.write_bytes(data)
            result = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path), *map(str, options)],
                                    capture_output=True, timeout=30)
        return result, json.loads(result.stdout) if result.stdout else None

    def test_date_assignment_preserves_source_without_normalizing_clock_or_zone(self):
        for target in (4, 5, 7, 8, 20):
            data = date_source().replace(b"(#942,#940,(#7))", f"(#942,#940,(#{target}))".encode())
            with self.subTest(target=target):
                run, result = self.invoke(data)
                self.assertEqual(0, run.returncode, result["diagnostics"])
                self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
                self.assertIn("941", result["snapshot"]["part"]["roles"].values())
                self.assertEqual("12", result["snapshot"]["part"]["roles"]["unit_context"])

    def test_date_assignment_cannot_target_an_unadmitted_geometry_branch(self):
        data = date_source().replace(b"(#942,#940,(#7))", b"(#942,#940,(#21))")
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(1, run.returncode)
        self.assertEqual("ReferenceTypeMismatch", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])

    def test_orphan_date_chain_is_not_implicitly_attached(self):
        data = date_source().replace(b"#941=APPLIED_DATE_AND_TIME_ASSIGNMENT(#942,#940,(#7));\n", b"")
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnownedOrOrphanProfileContent", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])

    def test_date_assignment_does_not_grant_ownership_to_an_unattached_property(self):
        data = append_records(date_source(), b"#960=PROPERTY_DEFINITION('unattached',$,#7);\n").replace(
            b"(#942,#940,(#7))", b"(#942,#940,(#960))")
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedAdministrativeAttachment", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])

    def test_date_transport_checks_required_types_and_has_no_derived_slots(self):
        for old, new, key in [
            (b"LOCAL_TIME(12,", b"LOCAL_TIME('12',", "AttributeTypeMismatch"),
            (b"DATE_TIME_ROLE('creation_date')", b"DATE_TIME_ROLE('creation_date','description')", "AttributeCountMismatch"),
            (b"(#942,#940,(#7))", b"(#942,#940,())", "AggregateCardinalityViolation"),
        ]:
            with self.subTest(key=key):
                run, result = self.invoke(date_source().replace(old, new), "--interop-nongeometric")
                self.assertEqual(1, run.returncode)
                self.assertEqual(key, result["diagnostics"][0]["key"])
                self.assertIsNone(result["snapshot"])

    def test_rendering_preserves_exact_values_and_selected_presentation(self):
        data = rendering_source()
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, result["diagnostics"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        self.assertEqual("950", result["snapshot"]["part"]["roles"]["presentation_0"])

    def test_missing_rendering_method_requires_explicit_policy_and_keeps_original_error(self):
        data = rendering_source(b"$")
        strict, rejected = self.invoke(data)
        self.assertEqual(1, strict.returncode)
        self.assertEqual("RequiredAttributeMissing", rejected["diagnostics"][0]["key"])
        self.assertEqual("rendering_method", rejected["diagnostics"][0]["field"])
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(0, run.returncode, result["diagnostics"])
        self.assertEqual(rejected["diagnostics"], result["preserved_diagnostics"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])

    def test_unattached_missing_rendering_method_cannot_use_presentation_exception(self):
        data = rendering_source(b"$").replace(
            b"#950=MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('display',(#951),#12);\n", b"")
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(1, run.returncode)
        self.assertEqual("RequiredAttributeMissing", result["diagnostics"][0]["key"])
        self.assertEqual("rendering_method", result["diagnostics"][0]["field"])
        self.assertIsNone(result["snapshot"])
        self.assertEqual([], result["preserved_diagnostics"])

    def test_rendering_cannot_attach_to_foreign_geometry(self):
        data = append_records(rendering_source(b"$"), b"#960=CARTESIAN_POINT('unrelated',(1.,2.,3.));\n").replace(
            b"STYLED_ITEM('source style',(#952),#21)", b"STYLED_ITEM('source style',(#952),#960)")
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedPresentationAttachment", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])

    def test_rendering_source_predicates_are_not_silently_relaxed(self):
        for value in (b"-1.0E-9999", b"1.00000000000000000001"):
            data = rendering_source().replace(b"TRANSPARENT(+0.000E+0)", b"TRANSPARENT(" + value + b")")
            with self.subTest(value=value):
                run, result = self.invoke(data, "--interop-nongeometric")
                self.assertEqual(1, run.returncode)
                self.assertEqual("SourceRuleViolation:SURFACE_STYLE_TRANSPARENT.WR1", result["diagnostics"][0]["key"])
                self.assertIsNone(result["snapshot"])
        data = append_records(rendering_source(), b"#960=SURFACE_STYLE_TRANSPARENT(1.);\n").replace(b",#957,(#956))", b",#957,(#956,#960))")
        run, result = self.invoke(data)
        self.assertEqual(1, run.returncode)
        self.assertEqual("SourceRuleViolation:SURFACE_STYLE_RENDERING_WITH_PROPERTIES.WR1", result["diagnostics"][0]["key"])

    def test_unknown_enum_and_missing_colour_remain_errors_in_interop(self):
        for data, key in [(rendering_source(b".OTHER."), "EnumerationValueInvalid"),
                          (rendering_source().replace(b",#957,(#956))", b",$,(#956))"), "RequiredAttributeMissing")]:
            with self.subTest(key=key):
                run, result = self.invoke(data, "--interop-nongeometric")
                self.assertEqual(1, run.returncode)
                self.assertEqual(key, result["diagnostics"][0]["key"])
                self.assertIsNone(result["snapshot"])

    def test_diagnostic_and_visit_limits_remain_atomic(self):
        data = rendering_source(b"$")
        run, result = self.invoke(data, "--interop-nongeometric", "--max-preserved-diagnostics", 0)
        self.assertEqual(3, run.returncode)
        self.assertEqual("PreservedDiagnosticLimit", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])
        run, result = self.invoke(data, "--interop-nongeometric")
        visits = result["statistics"]["dependency_visits"]
        exact, _ = self.invoke(data, "--interop-nongeometric", "--max-semantic-visits", visits)
        short, failed = self.invoke(data, "--interop-nongeometric", "--max-semantic-visits", visits - 1)
        self.assertEqual(0, exact.returncode)
        self.assertEqual(3, short.returncode)
        self.assertIsNone(failed["snapshot"])

    def test_property_only_mass_unit_source_errors_keep_exact_units_and_diagnostics(self):
        for conversion, key in [(False, "SourceRuleViolation:SI_UNIT.WR1"), (True, "DerivedSlotMarkerRequired")]:
            data = property_mass_source(conversion)
            with self.subTest(conversion=conversion):
                strict, rejected = self.invoke(data)
                self.assertEqual(1, strict.returncode)
                self.assertEqual(key, rejected["diagnostics"][0]["key"])
                run, result = self.invoke(data, "--interop-nongeometric")
                self.assertEqual(0, run.returncode, result["diagnostics"])
                self.assertIn(rejected["diagnostics"][0], result["preserved_diagnostics"])
                self.assertIn("SourceRuleViolation:MEASURE_WITH_UNIT.WR1", [d["key"] for d in result["preserved_diagnostics"]])
                self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
                self.assertEqual("CheckedWithDiagnostics", result["checks"]["unit_dependencies"])

    def test_mass_unit_error_shared_with_physical_context_cannot_be_quarantined(self):
        for conversion, key in [(False, "SourceRuleViolation:SI_UNIT.WR1"), (True, "DerivedSlotMarkerRequired")]:
            with self.subTest(conversion=conversion):
                run, result = self.invoke(property_mass_source(conversion, physical=True), "--interop-nongeometric")
                self.assertEqual(1, run.returncode, result["diagnostics"])
                self.assertEqual(key, result["diagnostics"][0]["key"])
                self.assertEqual("Rejected", result["checks"]["interoperability_approval"])
                self.assertIsNone(result["snapshot"])
                self.assertEqual([], result["preserved_diagnostics"])

    def test_unowned_bad_mass_unit_does_not_become_a_property_root(self):
        data = property_mass_source().replace(b"#951=PROPERTY_DEFINITION_REPRESENTATION(#950,#952);\n", b"")
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(2, run.returncode)
        self.assertIsNone(result["snapshot"])
        self.assertEqual("UnownedOrOrphanProfileContent", result["diagnostics"][0]["key"])

    def test_nonmass_derived_marker_error_is_not_a_metadata_exception(self):
        data = property_mass_source(conversion=True).replace(b"'source mass',#904) MASS_UNIT()", b"'source mass',#904) LENGTH_UNIT()")
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(1, run.returncode)
        self.assertEqual("DerivedSlotMarkerRequired", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])

    def test_invalid_mass_dimension_and_reference_types_are_not_quarantined(self):
        data = property_mass_source().replace(b"SI_UNIT($,.GRAM.)", b"SI_UNIT(.KILO.,.METRE.)")
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(1, run.returncode)
        self.assertEqual("SourceRuleViolation:MASS_UNIT.WR1", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])
        data = property_mass_source(conversion=True).replace(b"NAMED_UNIT(#960)", b"NAMED_UNIT(#903)")
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(1, run.returncode)
        self.assertEqual("ReferenceTypeMismatch", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])
