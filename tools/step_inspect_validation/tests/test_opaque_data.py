# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Unknown DATA text is retained as bytes, never silently decoded."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SOURCE = (Path(__file__).resolve().parents[2] / "step_import_validation/corpus/ap203e2-planar/v1/cases"
          / "S203-P04/input.step").read_bytes()


class OpaqueDataTests(unittest.TestCase):
    def invoke(self, data, *args):
        with tempfile.TemporaryDirectory(prefix="step-opaque-data-") as folder:
            path = Path(folder) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_INSPECT_BINARY"], "--fixture-limits", str(path), *args],
                                 capture_output=True, timeout=30)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_explicit_data_policy_preserves_original_bytes_and_unavailable_text(self):
        data = SOURCE.replace(b"'P0-PART'", b"'\x80\xfe\\unknown''quote'")
        run, result = self.invoke(data, "--preserve-opaque-data-strings")
        self.assertEqual(0, run.returncode, run.stderr or result)
        self.assertEqual("PreserveOpaqueBytes", result["data_string_policy"])
        self.assertEqual(data, result["input"]["raw"].encode("latin1"))
        record = next(r for r in result["records"] if r["id"] == "4")
        value = record["components"][0]["parameters"][0]
        self.assertEqual("OpaqueString", value["kind"])
        self.assertIn("DATA", value["notice"])
        self.assertNotIn("HEADER", value["notice"])
        self.assertIsNone(value["text"])
        self.assertEqual(b"'\x80\xfe\\unknown''quote'", value["raw"].encode("latin1"))
        self.assertIn("OpaqueDataStringsRetained", [d["key"] for d in result["diagnostics"]])

    def test_header_and_data_policies_are_independent_and_default_is_strict(self):
        data = SOURCE.replace(b"'P0-PART'", b"'\x80\xfe'")
        for flags in [(), ("--preserve-opaque-header-name",)]:
            run, result = self.invoke(data, *flags)
            self.assertEqual(2, run.returncode, run.stderr or result)
            self.assertEqual("UnsupportedEncoding", result["diagnostics"][0]["key"])
            self.assertIsNone(result["records"])
        header = SOURCE.replace(b"'p0-analytic-design.step'", b"'\x80\xfe'")
        run, result = self.invoke(header, "--preserve-opaque-data-strings")
        self.assertEqual(2, run.returncode, run.stderr or result)
        combined = header.replace(b"'P0-PART'", b"'\x80\xfe'")
        run, result = self.invoke(combined, "--preserve-opaque-data-strings", "--preserve-opaque-header-name")
        self.assertEqual(0, run.returncode, run.stderr or result)
        self.assertEqual(2, sum(d["key"].startswith("Opaque") for d in result["diagnostics"]))

    def test_comments_identifiers_and_controls_are_not_opaque_string_exceptions(self):
        for data in [SOURCE.replace(b"#22=", b"/*\x80*/#22="),
                     SOURCE.replace(b"CLOSED_SHELL", b"CLOSED_\x80SHELL"),
                     SOURCE.replace(b"'P0-PART'", b"'\x80\x01'")]:
            with self.subTest(kind="nonstring or control"):
                run, result = self.invoke(data, "--preserve-opaque-data-strings")
                self.assertNotEqual(0, run.returncode)
                self.assertIsNone(result["records"])

    def test_ascii_decoder_reference_and_resource_contracts_still_apply(self):
        cases = [(SOURCE.replace(b"'P0-PART'", b"'\\BAD\\'"), (), "InvalidStringEncoding"),
                 (SOURCE.replace(b"#21=MANIFOLD_SOLID_BREP('',#22)",
                                 b"#21=MANIFOLD_SOLID_BREP('\x80',#99999)"), (), "UnresolvedRequiredReference"),
                 (SOURCE.replace(b"'P0-PART'", b"'\x80'"), ("--max-string-bytes", "2"), "StringByteLimit")]
        for data, args, expected in cases:
            with self.subTest(expected=expected):
                run, result = self.invoke(data, "--preserve-opaque-data-strings", *args)
                self.assertNotEqual(0, run.returncode)
                self.assertEqual(expected, result["diagnostics"][0]["key"])
                self.assertIsNone(result["records"])
