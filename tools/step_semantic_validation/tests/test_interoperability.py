# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit metadata interoperability leaves strict import and source bytes intact."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214_entities import append_records, presentation_source
from tools.step_semantic_validation.tests.test_ap214_units import unit_source
from tools.step_semantic_validation.tests.test_ap214_property import property_source
from tools.step_semantic_validation.tests.test_ap214 import CORPUS, converted_ap214_source
from tools.step_semantic_validation.tests.test_ap214_units import conversion_mass_source


def density_property():
    return append_records(unit_source(), b"""#950=PROPERTY_DEFINITION('density property',$,#7);
#951=PROPERTY_DEFINITION_REPRESENTATION(#950,#952);
#952=REPRESENTATION('density',(#953),#12);
#953=MEASURE_REPRESENTATION_ITEM('density',POSITIVE_RATIO_MEASURE(7850.),#903);
""")


class InteroperabilityTests(unittest.TestCase):
    def invoke(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="step-interop-") as directory:
            path = Path(directory) / "input.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path),
                                  *map(str, options)], capture_output=True, timeout=25)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_explicit_policy_preserves_attached_density_error_and_original_values(self):
        data = density_property()
        strict, rejected = self.invoke(data)
        self.assertEqual(1, strict.returncode)
        self.assertEqual("SourceRuleViolation:MEASURE_WITH_UNIT.WR1", rejected["diagnostics"][0]["key"])
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual("CheckedWithDiagnostics", result["status"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        self.assertEqual(rejected["diagnostics"], result["preserved_diagnostics"])
        self.assertEqual("953", next(r["id"] for r in result["snapshot"]["records"] if r["id"] == "953"))
        self.assertEqual("7", result["snapshot"]["part"]["property_attachments"][0]["product_definition"])
        self.assertEqual([], result["diagnostics"])

    def test_unattached_style_remains_unassigned_without_creating_a_part_root(self):
        data = append_records(property_source(), b"#970=COLOUR_RGB('unused',0.2,0.4,0.6);\n")
        strict, rejected = self.invoke(data)
        self.assertEqual(2, strict.returncode)
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual(rejected["diagnostics"], result["preserved_diagnostics"])
        self.assertEqual(["970"], result["snapshot"]["unattached_non_geometric_declarations"])
        self.assertNotIn("970", result["snapshot"]["part"]["roles"].values())

    def test_style_without_presentation_owner_requires_a_proven_body_target(self):
        data = presentation_source().replace(
            b"#900=MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('display',(#901),#12);\n", b"")
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual([str(v) for v in range(901, 909)],
                         result["snapshot"]["unattached_non_geometric_declarations"])
        self.assertNotIn("901", result["snapshot"]["part"]["roles"].values())
        bad = append_records(data, b"#980=CARTESIAN_POINT('unrelated',(1.,2.,3.));\n")
        bad = bad.replace(b"STYLED_ITEM('source style',(#902),#21)", b"STYLED_ITEM('source style',(#902),#980)")
        run, rejected = self.invoke(bad, "--interop-nongeometric")
        self.assertEqual(2, run.returncode)
        self.assertIsNone(rejected["snapshot"])
        self.assertEqual([], rejected["preserved_diagnostics"])

    def test_general_source_defers_unmarked_region_instead_of_guessing_an_outer_bound(self):
        data = property_source().replace(b"FACE_OUTER_BOUND", b"FACE_BOUND")
        strict, rejected = self.invoke(data)
        self.assertEqual(2, strict.returncode)
        run, result = self.invoke(data, "--general-brep-source")
        self.assertEqual(7, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual("Deferred", result["status"])
        self.assertEqual(["302"], result["snapshot"]["deferred_region_faces"])
        self.assertEqual("GeneralBrepRegionClassificationNotRun", result["deferred_reason"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        bad = append_records(property_source(), b"#970=FACE_OUTER_BOUND('second',#300,.T.);\n")
        bad = bad.replace(b"('',(#301),#134", b"('',(#301,#970),#134")
        run, rejected = self.invoke(bad, "--general-brep-source")
        self.assertEqual(2, run.returncode)
        self.assertIsNone(rejected["snapshot"])

    def test_region_deferral_does_not_overwrite_failed_ownership_phase(self):
        data = property_source().replace(b"FACE_OUTER_BOUND", b"FACE_BOUND")
        data = append_records(data, b"#970=CARTESIAN_POINT('detached',(1.,2.,3.));\n")
        run, result = self.invoke(data, "--interop-nongeometric", "--general-brep-source")
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnownedOrOrphanProfileContent", result["diagnostics"][0]["key"])
        self.assertEqual("Unsupported", result["checks"]["single_part_profile"])
        self.assertIsNone(result["snapshot"])

    def test_missing_labels_are_retained_but_missing_geometry_is_fatal(self):
        data = property_source().replace(b"'body'", b"$")
        self.assertEqual(1, self.invoke(data)[0].returncode)
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual("CheckedWithDiagnostics", result["checks"]["type_fields"])
        self.assertEqual("RequiredAttributeMissing", result["preserved_diagnostics"][0]["key"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        for old, new in ((b"(3.,-2.,5.)", b"($,-2.,5.)"), (b"#134=PLANE('',#133)", b"#134=PLANE('',$)")):
            with self.subTest(old=old):
                changed = property_source().replace(old, new)
                self.assertTrue(changed != property_source(), "Fixture edit must change the intended geometry field")
                run, result = self.invoke(changed, "--interop-nongeometric")
                self.assertEqual(1, run.returncode)
                self.assertIsNone(result["snapshot"])
                self.assertEqual([], result["preserved_diagnostics"])
        ap203 = (CORPUS / "S203-P01/input.step").read_bytes().replace(b"'body'", b"$")
        self.assertEqual(1, self.invoke(ap203, "--interop-nongeometric")[0].returncode)

    def test_shared_physical_conversion_factor_cannot_be_quarantined_as_metadata(self):
        data = converted_ap214_source().replace(
            b"LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#10)",
            b"MEASURE_REPRESENTATION_ITEM('also physical',POSITIVE_RATIO_MEASURE(25.4),#10)")
        data = append_records(data, b"""#950=PROPERTY_DEFINITION('shared factor',$,#7);
#951=PROPERTY_DEFINITION_REPRESENTATION(#950,#952);
#952=REPRESENTATION('metadata',(#18),#953);
#953=REPRESENTATION_CONTEXT('metadata','source');
""")
        strict, rejected = self.invoke(data)
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(1, strict.returncode)
        self.assertEqual(1, run.returncode)
        self.assertEqual(rejected["diagnostics"], result["diagnostics"])
        self.assertEqual("Rejected", result["checks"]["unit_dependencies"])
        self.assertEqual("Rejected", result["checks"]["interoperability_approval"])
        self.assertIsNone(result["snapshot"])
        self.assertEqual([], result["preserved_diagnostics"])

    def test_detached_geometry_and_unit_cycles_are_not_metadata_exemptions(self):
        data = append_records(property_source(), b"#970=CARTESIAN_POINT('detached',(1.,2.,3.));\n")
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnownedOrOrphanProfileContent", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])
        data = conversion_mass_source().replace(b"MASS_MEASURE(2.2046),#900", b"MASS_MEASURE(2.2046),#903")
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(1, run.returncode)
        self.assertEqual("SourceUnitDependencyCycle", result["diagnostics"][0]["key"])
        self.assertEqual([], result["preserved_diagnostics"])

    def test_unattached_bad_measure_cannot_acquire_property_ownership(self):
        data = density_property().replace(b"#951=PROPERTY_DEFINITION_REPRESENTATION(#950,#952);\n", b"")
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnownedOrOrphanProfileContent", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])
        self.assertEqual([], result["preserved_diagnostics"])

    def test_category_label_does_not_change_selected_product_membership(self):
        data = append_records(property_source(), b"#970=PRODUCT_RELATED_PRODUCT_CATEGORY('vendor name',$,(#4));\n")
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual("UnsupportedProductCategory", result["preserved_diagnostics"][0]["key"])
        self.assertEqual("4", result["snapshot"]["part"]["roles"]["product"])
        run, rejected = self.invoke(data.replace(b"'vendor name',$,(#4)", b"'vendor name',$,(#4,#4)"),
                                    "--interop-nongeometric")
        self.assertEqual(1, run.returncode)
        self.assertIsNone(rejected["snapshot"])

    def test_definition_context_label_preserves_the_selected_chain_identity(self):
        data = property_source().replace(b"'part definition'", b"'3D Mechanical Parts'")
        strict, rejected = self.invoke(data)
        self.assertEqual(2, strict.returncode)
        run, result = self.invoke(data, "--interop-nongeometric", "--general-brep-source")
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual(rejected["diagnostics"], result["preserved_diagnostics"])
        self.assertEqual("6", result["snapshot"]["part"]["roles"]["definition_context"])
        self.assertEqual("CheckedWithDiagnostics", result["checks"]["single_part_profile"])

    def test_preservation_and_traversal_capacities_do_not_publish_partial_results(self):
        data = density_property()
        run, result = self.invoke(data, "--interop-nongeometric", "--max-preserved-diagnostics", 0)
        self.assertEqual(3, run.returncode)
        self.assertEqual("PreservedDiagnosticLimit", result["diagnostics"][0]["key"])
        self.assertEqual([], result["preserved_diagnostics"])
        self.assertIsNone(result["snapshot"])
        run, accepted = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(0, run.returncode)
        visits = accepted["statistics"]["dependency_visits"]
        run, limited = self.invoke(data, "--interop-nongeometric", "--max-semantic-visits", visits - 1)
        self.assertEqual(3, run.returncode)
        self.assertIsNone(limited["snapshot"])
        self.assertEqual([], limited["preserved_diagnostics"])
        self.assertEqual(0, self.invoke(data, "--interop-nongeometric", "--max-semantic-visits", visits)[0].returncode)
        run, limited = self.invoke(data, "--interop-nongeometric", "--max-report-bytes", 1)
        self.assertEqual(5, run.returncode)
        self.assertEqual(b"", run.stdout)
        self.assertIsNone(limited)


if __name__ == "__main__":
    unittest.main()
