# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Measure-representation source fields and necessary unit rules through the CLI."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214_units import conversion_mass_source
from tools.step_semantic_validation.tests.test_ap214_units import unit_source
from tools.step_semantic_validation.tests.test_ap214_entities import append_records
from tools.step_semantic_validation.tests.test_ap214 import CORPUS


def measure_source():
    # An MRI is also an MWU and can be a conversion factor. This fixture isolates
    # the source-field contract; it does not claim full RI.WR1/global validation.
    return conversion_mass_source().replace(
        b"MASS_MEASURE_WITH_UNIT(MASS_MEASURE(2.2046),#900)",
        b"MEASURE_REPRESENTATION_ITEM('source measure',MASS_MEASURE(+2.204600E+0),#900)")


class Ap214MeasureTests(unittest.TestCase):
    def invoke(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="step-ap214-measure-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path),
                                  *map(str, options)], capture_output=True, timeout=20)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_measure_item_preserves_both_parent_fields_and_exact_source_values(self):
        data = measure_source()
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        self.assertEqual("Passed", result["checks"]["unit_dependencies"])
        record = next(r for r in result["snapshot"]["records"] if r["id"] == "904")
        self.assertEqual([("name", "REPRESENTATION_ITEM"), ("value_component", "MEASURE_WITH_UNIT"),
                          ("unit_component", "MEASURE_WITH_UNIT")],
                         [(a["name"], a["declared_in"]) for a in record["attributes"]])

    def test_positive_ratio_predicate_and_inherited_ratio_dimensions(self):
        data = append_records(unit_source(),
                              b"#904=MEASURE_REPRESENTATION_ITEM('ratio',POSITIVE_RATIO_MEASURE(1.),#11);\n")
        run, result = self.invoke(data)
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnownedOrOrphanProfileContent", result["diagnostics"][0]["key"])
        self.assertEqual("Passed", result["checks"]["unit_dependencies"])
        self.assertEqual("Partial", result["checks"]["named_measure_predicates"])
        # Positivity does not erase inherited RATIO_MEASURE dimensional rules.
        for original, replacement, key in (
            (b"MEASURE(1.)", b"MEASURE(-0.)", "SourceRuleViolation:POSITIVE_RATIO_MEASURE.WR1"),
            (b"1.),#11", b"1.),#903", "SourceRuleViolation:MEASURE_WITH_UNIT.WR1"),
        ):
            run, result = self.invoke(data.replace(original, replacement))
            self.assertEqual(1, run.returncode)
            self.assertEqual(key, result["diagnostics"][0]["key"])
            self.assertIsNone(result["snapshot"])

    def test_complete_external_mapping_keeps_local_slots_and_both_parent_roles(self):
        original = b"MEASURE_REPRESENTATION_ITEM('source measure',MASS_MEASURE(+2.204600E+0),#900)"
        external = (b"(MEASURE_REPRESENTATION_ITEM() MEASURE_WITH_UNIT(MASS_MEASURE(+2.204600E+0),#900) "
                    b"REPRESENTATION_ITEM('source measure'))")
        data = measure_source().replace(original, external)
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        record = next(r for r in result["snapshot"]["records"] if r["id"] == "904")
        self.assertEqual(["value_component", "unit_component", "name"],
                         [a["name"] for a in record["attributes"]])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])

    def test_density_named_ratio_conflict_keeps_original_branch_number_and_unit(self):
        data = append_records(unit_source(), b"#904=MEASURE_REPRESENTATION_ITEM('density measure',"
                              b"POSITIVE_RATIO_MEASURE(7850.),#903);\n")
        run, result = self.invoke(data)
        self.assertEqual(1, run.returncode)
        error = result["diagnostics"][0]
        self.assertEqual(("SourceRuleViolation:MEASURE_WITH_UNIT.WR1", "SourceInvalid", "904", "unit_component"),
                         (error["key"], error["classification"], error["record_id"], error["field"]))
        self.assertEqual([3], error["parameter_path"])
        self.assertEqual(b"#903", data[slice(*error["span"])])
        self.assertEqual("Rejected", result["checks"]["unit_dependencies"])
        self.assertEqual("NotRun", result["checks"]["geometry_dependencies"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        self.assertIsNone(result["snapshot"])

    def test_ratio_signs_and_extreme_exponents_use_exact_source_comparison(self):
        for branch, text, rejected in (
            ("RATIO_MEASURE", "-0.", False), ("RATIO_MEASURE", "-1.E999999999999999999999", False),
            ("POSITIVE_RATIO_MEASURE", "+1.E-999999999999999999999", False),
            ("POSITIVE_RATIO_MEASURE", "+1.E999999999999999999999", False),
            ("POSITIVE_RATIO_MEASURE", "0.", True), ("POSITIVE_RATIO_MEASURE", "-0.E99999999999999999", True),
            ("POSITIVE_RATIO_MEASURE", "-1.E-99999999999999999999", True),
        ):
            with self.subTest(branch=branch, text=text):
                record = f"#904=MEASURE_REPRESENTATION_ITEM('ratio',{branch}({text}),#11);\n".encode()
                data = append_records(unit_source(), record)
                run, result = self.invoke(data)
                self.assertEqual(1 if rejected else 2, run.returncode)
                self.assertEqual("SourceRuleViolation:POSITIVE_RATIO_MEASURE.WR1" if rejected else
                                 "UnownedOrOrphanProfileContent", result["diagnostics"][0]["key"])
                self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
                self.assertIsNone(result["snapshot"])
                self.assertEqual(int(branch == "POSITIVE_RATIO_MEASURE"),
                                 result["statistics"]["unit_type_predicate_checks"])

    def test_transport_references_and_unregistered_branches_remain_strict(self):
        data = measure_source()
        for old, new, key, code in (
            (b"'source measure'", b"$", "RequiredAttributeMissing", 1),
            (b"MASS_MEASURE(+2.204600E+0)", b"MASS_MEASURE(2)", "AttributeTypeMismatch", 1),
            (b"MASS_MEASURE(+2.204600E+0)", b"NUMERIC_MEASURE(2.)", "UnsupportedMeasureBranch", 2),
            (b"E+0),#900", b"E+0),#901", "ReferenceTypeMismatch", 1),
            (b"E+0),#900", b"E+0),#900,'extra'", "AttributeCountMismatch", 1),
        ):
            with self.subTest(key=key):
                run, result = self.invoke(data.replace(old, new))
                self.assertEqual(code, run.returncode)
                self.assertEqual(key, result["diagnostics"][0]["key"])
                self.assertIsNone(result["snapshot"])
        original = (CORPUS / "S203-P01/input.step").read_bytes()
        data = append_records(original, b"#904=MEASURE_REPRESENTATION_ITEM('length',LENGTH_MEASURE(1.),#10);\n")
        run, result = self.invoke(data)
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedEntityType", result["diagnostics"][0]["key"])
        self.assertEqual("MEASURE_REPRESENTATION_ITEM", result["diagnostics"][0]["actual"])

    def test_missing_external_parent_is_rejected(self):
        data = measure_source().replace(
            b"MEASURE_REPRESENTATION_ITEM('source measure',MASS_MEASURE(+2.204600E+0),#900)",
            b"(MEASURE_REPRESENTATION_ITEM() MEASURE_WITH_UNIT(MASS_MEASURE(2.),#900))")
        run, result = self.invoke(data)
        self.assertEqual(1, run.returncode)
        self.assertEqual("MissingComplexConstituent", result["diagnostics"][0]["key"])
        self.assertEqual("REPRESENTATION_ITEM", result["diagnostics"][0]["expected"])

    def test_measure_item_does_not_bypass_shape_or_ownership_guards(self):
        data = append_records(unit_source(),
                              b"#904=MEASURE_REPRESENTATION_ITEM('ratio',RATIO_MEASURE(1.),#11);\n")
        run, result = self.invoke(data.replace(b"'body',(#21)", b"'body',(#21,#904)"))
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedRepresentationItem", result["diagnostics"][0]["key"])
        self.assertEqual("Passed", result["checks"]["unit_dependencies"])
        self.assertIsNone(result["snapshot"])

    def test_visits_and_report_bytes_have_atomic_boundaries(self):
        data = measure_source()
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode)
        visits = result["statistics"]["dependency_visits"]
        self.assertEqual(0, self.invoke(data, "--max-semantic-visits", visits)[0].returncode)
        stopped, limited = self.invoke(data, "--max-semantic-visits", visits - 1)
        self.assertEqual(3, stopped.returncode)
        self.assertIsNone(limited["snapshot"])
        size = len(run.stdout)
        for _ in range(4):
            run, _ = self.invoke(data, "--max-report-bytes", size)
            self.assertEqual(0, run.returncode)
            size = len(run.stdout)
        stopped, limited = self.invoke(data, "--max-report-bytes", size - 1)
        self.assertEqual(5, stopped.returncode)
        self.assertEqual(b"", stopped.stdout)
        self.assertIsNone(limited)


if __name__ == "__main__":
    unittest.main()
