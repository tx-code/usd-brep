# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit general N2635 source admission; default P0 remains frozen."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

CORPUS = Path(__file__).resolve().parents[2] / "step_import_validation/corpus/ap203e2-planar/v1/cases"
BOX = (CORPUS / "S203-P04/input.step").read_bytes()


def add_records(records):
    return BOX.replace(b"ENDSEC;\nEND-ISO-10303-21;", records.encode("ascii") + b"ENDSEC;\nEND-ISO-10303-21;")


def approval(target=4):
    return add_records(f"""#900=APPROVAL_STATUS('approved');
#901=APPROVAL(#900,'design');
#902=CC_DESIGN_APPROVAL(#901,(#{target}));
""")


class Ap203GeneralTests(unittest.TestCase):
    def invoke(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="ap203-general-") as directory:
            path = Path(directory) / "input.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path), *map(str, options)],
                                 capture_output=True, timeout=30)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_explicit_scope_selects_separate_n2635_catalog(self):
        old_run, old = self.invoke(BOX)
        run, result = self.invoke(BOX, "--general-brep-source")
        self.assertEqual((0, 0), (old_run.returncode, run.returncode))
        self.assertEqual("203e2-P0-Planar-0.1", old["schema_selection"]["qualified_profile"]["profile"])
        self.assertEqual("203e2-G0-Source-0.1", result["schema_selection"]["qualified_profile"]["profile"])
        self.assertEqual(old["source"]["input"]["raw"], result["source"]["input"]["raw"])
        self.assertIn("CIRCLE", result["snapshot"]["type_populations"])
        self.assertNotIn("CIRCLE", old["snapshot"]["type_populations"])

    def test_valid_approval_attachment_is_retained_without_changing_p0(self):
        run, result = self.invoke(approval(), "--general-brep-source")
        self.assertEqual(0, run.returncode, result["diagnostics"])
        self.assertIn("CC_DESIGN_APPROVAL", result["snapshot"]["type_populations"])
        old_run, old = self.invoke(approval())
        self.assertEqual(2, old_run.returncode)
        self.assertEqual("UnsupportedEntityType", old["diagnostics"][0]["key"])

    def test_legacy_approval_select_error_requires_explicit_policy_and_selected_owner(self):
        data = approval(7)
        strict_run, strict = self.invoke(data, "--general-brep-source")
        self.assertEqual(1, strict_run.returncode)
        self.assertEqual("ReferenceTypeMismatch", strict["diagnostics"][0]["key"])
        run, result = self.invoke(data, "--general-brep-source", "--interop-nongeometric")
        self.assertEqual(0, run.returncode, result["diagnostics"])
        issue, = result["preserved_diagnostics"]
        self.assertEqual(("ReferenceTypeMismatch", "902", "7"), tuple(issue[k] for k in ("key", "record_id", "target_id")))
        self.assertEqual(strict["source"]["records"], result["source"]["records"])
        self.assertEqual("N2635 explicit reference type", issue["responsibility"])

    def test_geometry_target_cannot_use_approval_exception(self):
        run, result = self.invoke(approval(21), "--general-brep-source", "--interop-nongeometric")
        self.assertEqual(1, run.returncode)
        self.assertIsNone(result["snapshot"])
        self.assertEqual("ReferenceTypeMismatch", result["diagnostics"][0]["key"])

    def test_unattached_administrative_cycle_cannot_create_part_ownership(self):
        data = add_records("""#900=APPROVAL_STATUS('approved');
#901=APPROVAL(#900,'design');
#902=CC_DESIGN_APPROVAL(#901,(#904));
#903=SECURITY_CLASSIFICATION_LEVEL('public');
#904=SECURITY_CLASSIFICATION('unused','none',#903);
""")
        run, result = self.invoke(data, "--general-brep-source", "--interop-nongeometric")
        self.assertEqual(2, run.returncode)
        self.assertIsNone(result["snapshot"])
        self.assertEqual("UnsupportedAdministrativeAttachment", result["diagnostics"][0]["key"])

    def test_value_item_preserves_number_branch_and_implicit_context(self):
        for value in ("0", "+0.000E+0"):
            data = add_records(f"""#900=PROPERTY_DEFINITION('annotation count',$,#7);
#901=VALUE_REPRESENTATION_ITEM('count',COUNT_MEASURE({value}));
#902=REPRESENTATION('counts',(#901),#12);
#903=PROPERTY_DEFINITION_REPRESENTATION(#900,#902);
""")
            with self.subTest(value=value):
                run, result = self.invoke(data, "--general-brep-source")
                self.assertEqual(0, run.returncode, result["diagnostics"])
                self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
                self.assertEqual(1, result["statistics"]["property_attachment_checks"])

    def test_value_item_without_global_unit_context_is_a_source_failure(self):
        data = add_records("""#900=PROPERTY_DEFINITION('count',$,#7);
#901=VALUE_REPRESENTATION_ITEM('count',COUNT_MEASURE(0.));
#902=REPRESENTATION('counts',(#901),#904);
#903=PROPERTY_DEFINITION_REPRESENTATION(#900,#902);
#904=REPRESENTATION_CONTEXT('metadata','units absent');
""")
        run, result = self.invoke(data, "--general-brep-source", "--interop-nongeometric")
        self.assertEqual(1, run.returncode)
        self.assertEqual("SourceRuleViolation:VALUE_REPRESENTATION_ITEM.WR1", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])

    def test_approval_diagnostic_capacity_is_not_silently_truncated(self):
        run, result = self.invoke(approval(7), "--general-brep-source", "--interop-nongeometric", "--max-preserved-diagnostics", 0)
        self.assertEqual(3, run.returncode)
        self.assertIsNone(result["snapshot"])
        self.assertEqual("PreservedDiagnosticLimit", result["diagnostics"][0]["key"])
