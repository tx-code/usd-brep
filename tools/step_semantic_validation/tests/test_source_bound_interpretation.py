"""Explicit fixed-schema interpretation binds immutable input, never a guessed OID."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_interpretation import box_source, UNREGISTERED

POLICY = "SourceBoundAp214As2010"
FLAG = "--interpret-ap214-for-source-sha256"


def malformed_box():
    # Deliberately synthetic invalid OID. No private HEADER or model is copied.
    return box_source().replace(UNREGISTERED, b"AUTOMOTIVE_DESIGN {1 0 10303 214 -1 1 1 1}")


class SourceBoundInterpretationTests(unittest.TestCase):
    def invoke(self, source, digest=None, *, native=False, extra=()):
        key = "STEP_NATIVE_BINARY" if native else "STEP_SEMANTIC_BINARY"
        with tempfile.TemporaryDirectory(prefix="public-source-bound-schema-") as folder:
            path = Path(folder) / "source.step"
            path.write_bytes(source)
            flags = (["--quality", "Q-AP214-development-mm-01"] if native else ["--general-brep-source"])
            command = [os.environ[key], "--fixture-limits", str(path), *flags, "--interop-nongeometric", *extra]
            if digest is not None:
                command += [FLAG, digest]
            run = subprocess.run(command, capture_output=True, timeout=30)
            self.assertEqual(source, path.read_bytes())
        self.assertTrue(run.stdout, run.stderr)
        return run, json.loads(run.stdout)

    def test_source_bound_interpretation_retains_the_original_malformed_identity(self):
        data = malformed_box()
        digest = hashlib.sha256(data).hexdigest()
        _, strict = self.invoke(data)
        run, result = self.invoke(data, digest)
        self.assertEqual(0, run.returncode, run.stderr or result["diagnostics"])
        self.assertEqual("CheckedWithDiagnostics", result["status"])
        identity = result["schema_selection"]["identifiers"][0]
        self.assertEqual("Malformed", identity["match"])
        self.assertEqual(strict["schema_selection"]["identifiers"], result["schema_selection"]["identifiers"])
        self.assertEqual(strict["diagnostics"], result["schema_interpretation"]["original_binding_diagnostics"])
        assumption = result["schema_interpretation"]
        self.assertEqual((POLICY, digest, True), (assumption["requested"], assumption["source_sha256"], assumption["applied"]))
        self.assertFalse(assumption["source_binding_verified"])
        self.assertEqual(data.decode(), result["source"]["input"]["raw"])
        self.assertIsNotNone(result["snapshot"])

    def test_an_input_mismatch_never_falls_back_to_another_interpretation(self):
        data = malformed_box()
        run, result = self.invoke(data, "0" * 64)
        self.assertEqual(2, run.returncode)
        self.assertEqual("SourceInterpretationInputMismatch", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])
        self.assertFalse(result["schema_interpretation"]["applied"])

    def test_malformed_digest_is_a_reported_request_failure(self):
        run, result = self.invoke(malformed_box(), "not-a-sha256")
        self.assertEqual(2, run.returncode)
        self.assertEqual("InvalidSchemaInterpretationRequest", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])

    def test_other_families_and_multiple_identifiers_are_not_eligible(self):
        for data in (malformed_box().replace(b"AUTOMOTIVE_DESIGN", b"CONFIG_CONTROL_DESIGN"),
                     malformed_box().replace(b"FILE_SCHEMA((", b"FILE_SCHEMA(('CONFIG_CONTROL_DESIGN',")):
            with self.subTest(multiple=b"('CONFIG_CONTROL_DESIGN'," in data):
                run, result = self.invoke(data, hashlib.sha256(data).hexdigest())
                self.assertEqual(2, run.returncode)
                self.assertIsNone(result["snapshot"])
                self.assertFalse(result["schema_interpretation"]["applied"])

    def test_existing_policies_keep_their_exact_eligibility(self):
        for flag in ("--interpret-unregistered-ap214-2010", "--interpret-ap214-name-only-as-2010"):
            run, result = self.invoke(malformed_box(), extra=(flag,))
            self.assertEqual(2, run.returncode)
            self.assertEqual("UnsupportedSchemaBinding", result["diagnostics"][0]["key"])
            self.assertIsNone(result["snapshot"])

    def test_an_already_bound_profile_is_not_silently_overridden(self):
        data = malformed_box().replace(b"{1 0 10303 214 -1 1 1 1}", b"{1 0 10303 214 3 1 1}")
        run, result = self.invoke(data, hashlib.sha256(data).hexdigest())
        self.assertEqual(2, run.returncode)
        self.assertEqual("SourceInterpretationNotApplicable", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])
        self.assertFalse(result["schema_interpretation"]["applied"])

    @unittest.skipUnless(os.environ.get("STEP_NATIVE_BINARY"), "Requires matching native CLI")
    def test_native_import_qualifies_only_under_the_explicit_bound_interpretation(self):
        data = malformed_box()
        run, result = self.invoke(data, hashlib.sha256(data).hexdigest(), native=True)
        self.assertEqual(11, run.returncode, run.stderr or result["diagnostics"])
        self.assertEqual("AcceptedUnderInterpretation", result["status"])
        self.assertIsNotNone(result["model"])
        self.assertEqual("None", result["repair"])
        self.assertAlmostEqual(6000, result["model"]["material_volume"]["value_mm3"], delta=1e-5)
        run, mismatch = self.invoke(data, "0" * 64, native=True)
        self.assertEqual(2, run.returncode)
        self.assertIsNone(mismatch["model"])
        self.assertEqual(0, mismatch["native_calls"])
