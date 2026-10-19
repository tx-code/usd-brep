# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit DATA hex-digit interpretation with immutable source-byte evidence."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

FLAG = "--interpret-level1-with-lowercase-hex"
MODE = "Level1AsCorrectedEncodingWithLowercaseHex"
PAYLOAD = b"\\X\\af\\X2\\4e2d\\X0\\\\X4\\0001f600\\X0\\"


def source_file(payload=PAYLOAD, level=b"1"):
    return (
        b"ISO-10303-21;HEADER;FILE_DESCRIPTION(('x'),'" + level
        + b"');FILE_NAME('','',('a'),('o'),'','','');FILE_SCHEMA(('S'));ENDSEC;DATA;"
        + b"#1=A('" + payload + b"',LENGTH_MEASURE(+1.2500E-2));ENDSEC;END-ISO-10303-21;"
    )


class LowercaseHexTests(unittest.TestCase):
    def invoke(self, data, *flags):
        with tempfile.TemporaryDirectory(prefix="step-lower-hex-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run(
                [os.environ["STEP_INSPECT_BINARY"], "--fixture-limits", str(path), *map(str, flags)],
                capture_output=True, timeout=30,
            )
        return run, json.loads(run.stdout) if run.stdout else None

    def test_independent_unicode_truth_raw_tokens_and_scalar_lexemes(self):
        data = source_file()
        old, before = self.invoke(data, "--interpret-level1-as-corrected")
        run, report = self.invoke(data, FLAG)
        self.assertEqual(1, old.returncode)
        self.assertEqual("InvalidStringEncoding", before["diagnostics"][0]["key"])
        self.assertEqual(0, run.returncode, run.stderr or run.stdout)
        self.assertEqual(data.decode(), report["input"]["raw"])
        value, typed = report["records"][0]["components"][0]["parameters"]
        self.assertEqual("¯中😀", value["text"])  # Independent code points U+00AF, U+4E2D, U+1F600.
        self.assertEqual("'" + PAYLOAD.decode() + "'", value["raw"])
        self.assertEqual("+1.2500E-2", typed["children"][0]["text"])
        carrier = report["carrier_interpretation"]
        self.assertEqual(MODE, carrier["interpretation"])
        self.assertEqual("1", carrier["declared_level"])
        self.assertEqual("UnsupportedImplementationLevel", carrier["original_diagnostics"][0]["key"])
        (evidence,) = carrier["string_hex_interpretations"]
        self.assertTrue(evidence["complete"])
        self.assertEqual(value["span"], evidence["token_span"])
        self.assertEqual(5, evidence["lowercase_digits"])
        start, end = evidence["first_strict_span"]
        self.assertEqual(b"\\X\\a", data[start:end])

    def test_policy_is_only_for_data_after_an_applied_level1_assumption(self):
        run, report = self.invoke(source_file(level=b"2;1"), FLAG)
        self.assertEqual(1, run.returncode)
        self.assertFalse(report["carrier_interpretation"]["applied"])
        header = source_file(b"x").replace(b"FILE_NAME(''", b"FILE_NAME('\\X2\\4e2d\\X0\\'", 1)
        run, report = self.invoke(header, FLAG)
        self.assertEqual(1, run.returncode)
        self.assertEqual("InvalidStringEncoding", report["diagnostics"][0]["key"])
        self.assertNotIn("string_hex_interpretations", report["carrier_interpretation"])
        for level in (b"2;2", b"3;1", b"4;1", b"1;1"):
            run, report = self.invoke(source_file(level=level), FLAG)
            self.assertEqual(2, run.returncode)
            self.assertFalse(report["carrier_interpretation"]["applied"])

    def test_whole_payload_and_unicode_validation_still_reject_bad_input(self):
        for payload, key in (
            (b"\\X2\\4e2dGGGG\\X0\\", "InvalidStringEncoding"),
            (b"\\X2\\d800\\X0\\", "InvalidStringCodePoint"),
            (b"\\X4\\0011ffff\\X0\\", "InvalidStringCodePoint"),
            (b"\\X2\\4e2d\\x0\\", "InvalidStringEncoding"),
            (b"\\X2\\4e2d", "InvalidStringEncoding"),
        ):
            with self.subTest(key=key):
                run, report = self.invoke(source_file(payload), FLAG)
                self.assertEqual(1, run.returncode)
                self.assertFalse(report["source_complete"])
                self.assertIsNone(report["records"])
                self.assertEqual(key, report["diagnostics"][0]["key"])
                entries = report["carrier_interpretation"]["string_hex_interpretations"]
                self.assertTrue(entries)
                self.assertFalse(entries[-1]["complete"])

    def test_multiple_values_have_distinct_source_identity(self):
        data = source_file(b"\\X\\e9").replace(b",LENGTH_MEASURE", b",'\\X2\\4e2d\\X0\\',LENGTH_MEASURE")
        run, report = self.invoke(data, FLAG)
        self.assertEqual(0, run.returncode, run.stderr or run.stdout)
        entries = report["carrier_interpretation"]["string_hex_interpretations"]
        self.assertEqual(2, len(entries))
        self.assertTrue(all(entry["complete"] for entry in entries))
        self.assertNotEqual(entries[0]["token_span"], entries[1]["token_span"])

    def test_unicode_scalar_matrix_matches_python_codepoint_truth(self):
        import random

        rng = random.Random(214203)
        points = [0, 1, 0x27, 0x5c, 0x7f, 0x80, 0xff, 0x7ff, 0x800,
                  0xd7ff, 0xe000, 0xffff, 0x10000, 0x10ffff]
        while len(points) < 256:
            point = rng.randrange(0x110000)
            if not 0xd800 <= point <= 0xdfff:
                points.append(point)
        payload = b"\\X4\\" + b"".join(f"{p:08x}".encode() for p in points) + b"\\X0\\"
        run, report = self.invoke(source_file(payload), FLAG, "--max-string-bytes", 10000)
        self.assertEqual(0, run.returncode, run.stderr or run.stdout)
        self.assertEqual("".join(map(chr, points)), report["records"][0]["components"][0]["parameters"][0]["text"])

    def test_opaque_bytes_are_not_partially_decoded_or_given_hex_evidence(self):
        data = source_file(b"\\X2\\4e2d\\X0\\\x80")
        run, report = self.invoke(data, FLAG, "--preserve-opaque-data-strings")
        self.assertEqual(0, run.returncode, run.stderr or run.stdout)
        value = report["records"][0]["components"][0]["parameters"][0]
        self.assertEqual("OpaqueString", value["kind"])
        self.assertIsNone(value["text"])
        self.assertNotIn("string_hex_interpretations", report["carrier_interpretation"])

    def test_partial_budget_failure_retains_incomplete_receipt_without_a_graph(self):
        run, report = self.invoke(source_file(b"\\X2\\4e2d\\X0\\"), FLAG, "--max-string-bytes", 12)
        self.assertEqual(3, run.returncode)
        self.assertEqual("StringByteLimit", report["diagnostics"][0]["key"])
        self.assertFalse(report["source_complete"])
        self.assertFalse(report["carrier_interpretation"]["string_hex_interpretations"][-1]["complete"])
        run, report = self.invoke(source_file(), FLAG, "--max-report-bytes", 1)
        self.assertEqual(5, run.returncode)
        self.assertIsNone(report)

    def test_only_requested_capability_is_not_an_executed_interpretation(self):
        data = source_file(b"ASCII", level=b"2;1")
        run, report = self.invoke(data, FLAG)
        self.assertEqual(0, run.returncode)
        self.assertFalse(report["carrier_interpretation"]["applied"])
        self.assertNotIn("string_hex_interpretations", report["carrier_interpretation"])
        for flags in ((FLAG, "--interpret-level1-as-corrected"), ("--interpret-level1-as-corrected", FLAG)):
            run, report = self.invoke(source_file(), *flags)
            self.assertEqual(64, run.returncode)
            self.assertIsNone(report)
