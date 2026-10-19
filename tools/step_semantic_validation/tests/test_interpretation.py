# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Application assumptions never become authenticated HEADER bindings."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

UNREGISTERED = b"AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1 1}"
CORPUS = Path(__file__).resolve().parents[2] / "step_import_validation/corpus/ap203e2-planar/v1/cases"


def box_source():
    # A new synthetic original, not an edit or normalization of an imported file.
    data = (CORPUS / "S203-P04/input.step").read_bytes()
    return data.replace(
        b"AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF {1 0 10303 403 2 1 2}",
        UNREGISTERED).replace(
        b"'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011",
        b"'automotive_design',2010")


class InterpretationTests(unittest.TestCase):
    def invoke(self, data, interpret=False, *extra, general=True, interop=True):
        with tempfile.TemporaryDirectory(prefix="step-interpreted-source-") as directory:
            source = Path(directory) / "source.step"
            source.write_bytes(data)
            command = [os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(source)]
            if general:
                command.append("--general-brep-source")
            if interop:
                command.append("--interop-nongeometric")
            if interpret:
                command.append("--interpret-unregistered-ap214-2010")
            run = subprocess.run(command + list(extra), capture_output=True, timeout=30)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_exact_unregistered_identifier_needs_explicit_assumption_and_preserves_failure(self):
        source = box_source()
        strict, before = self.invoke(source)
        self.assertEqual(2, strict.returncode)
        run, result = self.invoke(source, True)
        self.assertEqual(0, run.returncode, run.stderr)
        self.assertEqual("CheckedWithDiagnostics", result["status"])
        self.assertEqual("InterpretedUnverified", result["checks"]["schema_binding"])
        identity = result["schema_selection"]["identifiers"][0]
        self.assertEqual(UNREGISTERED.decode(), identity["literal"])
        self.assertEqual("UnregisteredVersion", identity["match"])
        self.assertIsNone(identity["registry_id"])
        self.assertEqual(["1", "0", "10303", "214", "3", "1", "1", "1"], identity["oid"])
        implementation = result["schema_selection"]["qualified_profile"]
        self.assertEqual("214-MBx2010-Source-0.1", implementation["profile"])
        self.assertFalse(implementation["header_identity_verified"])
        self.assertEqual("ExplicitApplicationInterpretation", implementation["binding_basis"])
        assumption = result["schema_interpretation"]
        self.assertTrue(assumption["applied"])
        self.assertFalse(assumption["source_binding_verified"])
        self.assertEqual(before["diagnostics"], assumption["original_binding_diagnostics"])
        self.assertEqual(source.decode(), result["source"]["input"]["raw"])

    def test_not_an_arbitrary_identifier_or_multischema_override(self):
        for data in (box_source().replace(UNREGISTERED, b"AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1 2}"),
                     box_source().replace(UNREGISTERED, b"UNRELATED_SCHEMA {1 0 10303 214 3 1 1 1}"),
                     box_source().replace(UNREGISTERED + b"'", UNREGISTERED + b"','OTHER_SCHEMA'")):
            with self.subTest(input_size=len(data)):
                run, result = self.invoke(data, True)
                self.assertEqual(2, run.returncode, run.stderr)
                self.assertFalse(result["schema_interpretation"]["applied"])
                self.assertEqual([], result["schema_interpretation"]["original_binding_diagnostics"])
                self.assertEqual("NotRun", result["checks"]["type_fields"])

    def test_existing_registered_binding_is_not_replaced_by_the_optional_policy(self):
        source = box_source().replace(UNREGISTERED, b"AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}")
        before, normal = self.invoke(source)
        run, result = self.invoke(source, True)
        self.assertEqual((0, 0), (before.returncode, run.returncode))
        self.assertEqual(normal["status"], result["status"])
        self.assertEqual(normal["schema_selection"], result["schema_selection"])
        self.assertFalse(result["schema_interpretation"]["applied"])

    def test_assumption_diagnostics_share_quota_but_are_not_nonphysical_proofs(self):
        source = box_source().replace(b"ENDSEC;\nEND-ISO",
                                    b"#9999=PRODUCT_RELATED_PRODUCT_CATEGORY('synthetic category',$,(#4));\n"
                                    b"ENDSEC;\nEND-ISO")
        self.assertNotEqual(box_source(), source)
        run, result = self.invoke(source, True, "--max-preserved-diagnostics", "1")
        self.assertEqual(3, run.returncode)
        self.assertEqual("PreservedDiagnosticLimit", result["diagnostics"][-1]["key"])
        run, result = self.invoke(source, True, "--max-preserved-diagnostics", "2")
        self.assertEqual(0, run.returncode, run.stderr)
        self.assertEqual(1, len(result["schema_interpretation"]["original_binding_diagnostics"]))
        self.assertEqual(1, len(result["preserved_diagnostics"]))
        self.assertEqual("UnsupportedProductCategory", result["preserved_diagnostics"][0]["key"])
        run, result = self.invoke(box_source(), True, "--max-preserved-diagnostics", "0")
        self.assertEqual(3, run.returncode)
        self.assertEqual("PreservedDiagnosticLimit", result["diagnostics"][-1]["key"])
        self.assertFalse(result["schema_interpretation"]["applied"])
        self.assertIsNone(result["schema_selection"]["qualified_profile"])

    def test_required_content_and_unit_checks_still_refuse_the_source(self):
        for data in (box_source().replace(b"ENDSEC;\nEND-ISO", b"#9999=UNSUPPORTED_REQUIRED_TYPE();\nENDSEC;\nEND-ISO"),
                     box_source().replace(b".METRE.", b".SECOND.")):
            run, result = self.invoke(data, True)
            self.assertNotEqual(0, run.returncode)
            self.assertNotIn(result["status"], ("Checked", "CheckedWithDiagnostics"))
            self.assertTrue(result["schema_interpretation"]["applied"])
            original = result["schema_interpretation"]["original_binding_diagnostics"]
            self.assertEqual("UnsupportedSchemaBinding", original[0]["key"])

    def test_general_interoperability_is_required_separately(self):
        for configuration in ({"general": False}, {"interop": False}):
            run, result = self.invoke(box_source(), True, **configuration)
            self.assertEqual(64, run.returncode)
            self.assertIsNone(result)
