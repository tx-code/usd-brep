# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Unknown filename encoding must not relax any DATA or schema token."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_inspect_validation.tests.test_inspect import document

FLAG = "--preserve-opaque-header-name"
NAME = b"legacy-\x81\\''quoted-\xc9\xa2\xc8\xc8.stp"


def opaque_document():
    return document("#1=A('standard');\n").replace(b"FILE_NAME('x'", b"FILE_NAME('" + NAME + b"'")


class OpaqueHeaderTests(unittest.TestCase):
    def invoke(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="step-opaque-header-") as directory:
            path = Path(directory) / "input.step"
            path.write_bytes(data)
            result = subprocess.run(
                [os.environ["STEP_INSPECT_BINARY"], "--fixture-limits", str(path), *map(str, options)],
                capture_output=True,
                timeout=20,
            )
        return result, json.loads(result.stdout) if result.stdout else None

    def test_opaque_header_is_explicit_and_byte_preserving_without_codepage_guess(self):
        data = opaque_document()
        strict, rejected = self.invoke(data)
        self.assertEqual(2, strict.returncode)
        self.assertEqual("UnsupportedEncoding", rejected["diagnostics"][0]["key"])
        run, report = self.invoke(data, FLAG)
        self.assertEqual(0, run.returncode, report)
        self.assertEqual("PreserveOpaqueBytes", report["header_name_policy"])
        self.assertEqual("OpaqueHeaderFileName", report["diagnostics"][0]["key"])
        value = report["header"][1]["parameters"][0]
        self.assertEqual("OpaqueString", value["kind"])
        self.assertIsNone(value["text"])
        self.assertEqual("Unestablished", value["interpretation"])
        self.assertEqual(b"'" + NAME + b"'", value["raw"].encode("latin1"))
        begin, end = value["span"]
        self.assertEqual(data[begin:end], value["raw"].encode("latin1"))
        self.assertEqual(data, report["input"]["raw"].encode("latin1"))
        self.assertEqual(hashlib.sha256(data).hexdigest(), report["input"]["sha256"])
        self.assertEqual("standard", report["records"][0]["components"][0]["parameters"][0]["text"])

    def test_non_ascii_anywhere_else_is_rejected_including_skipped_comments(self):
        for marker, replacement in [
            (b"'standard'", b"'nonascii-\x81\\'"),
            (b"'TEST'", b"'TEST\x81'"),
            (b"('a')", b"('\x81')"),
            (b"'independent test'", b"'\x81'"),
            (b"DATA;", b"/*\x81*/DATA;"),
            (b"#1=A", b"#1=\x81A"),
        ]:
            with self.subTest(marker=marker):
                run, report = self.invoke(opaque_document().replace(marker, replacement), FLAG)
                self.assertNotEqual(0, run.returncode)
                self.assertFalse(report["source_complete"])
                self.assertIsNone(report["records"])
                if marker in [b"DATA;", b"#1=A", b"'standard'"]:
                    self.assertEqual("", report["diagnostics"][0]["component"])

    def test_ascii_and_standard_escaped_names_keep_the_existing_decoded_meaning(self):
        for name in [b"", b"ASCII''quote", b"\\X2\\4E2D\\X0\\"]:
            with self.subTest(name=name):
                data = document("#1=A();\n").replace(b"FILE_NAME('x'", b"FILE_NAME('" + name + b"'")
                old_run, old = self.invoke(data)
                new_run, new = self.invoke(data, FLAG)
                self.assertEqual((0, 0), (old_run.returncode, new_run.returncode))
                new.pop("header_name_policy")
                self.assertEqual(old, new)
                self.assertEqual("String", new["header"][1]["parameters"][0]["kind"])

    def test_opaque_name_cannot_hide_invalid_header_shape_or_missing_reference(self):
        data = opaque_document()
        for bad in [
            data.replace(b"FILE_NAME('", b"FILE_NAME(('", 1).replace(b"','2026", b"'),'2026", 1),
            data.replace(b"#1=A('standard')", b"#1=A(#999)"),
        ]:
            run, report = self.invoke(bad, FLAG)
            self.assertEqual(1, run.returncode)
            self.assertIsNone(report["records"])

    def test_original_string_bytes_and_actual_emitted_report_bytes_remain_bounded(self):
        data = opaque_document()
        exact, report = self.invoke(data, FLAG, "--max-string-bytes", len(NAME) + 2)
        self.assertEqual(0, exact.returncode)
        short, failed = self.invoke(data, FLAG, "--max-string-bytes", len(NAME) + 1)
        self.assertEqual(3, short.returncode)
        self.assertIsNone(failed["records"])
        size = len(exact.stdout)
        while True:
            actual_size = len(exact.stdout) + len(str(size)) - len(str(report["limits"]["report_bytes"]))
            if actual_size == size:
                break
            size = actual_size
        full, _ = self.invoke(data, FLAG, "--max-string-bytes", len(NAME) + 2, "--max-report-bytes", size)
        limited, partial = self.invoke(data, FLAG, "--max-string-bytes", len(NAME) + 2, "--max-report-bytes", size - 1)
        self.assertEqual(0, full.returncode)
        self.assertEqual((5, b"", None), (limited.returncode, limited.stdout, partial))

    def test_del_and_control_bytes_are_not_accepted_even_inside_opaque_name(self):
        for bad in [b"\x7f", b"\x00", b"\x1b"]:
            with self.subTest(byte=bad):
                run, report = self.invoke(opaque_document().replace(NAME, NAME + bad), FLAG)
                self.assertEqual(2, run.returncode)
                self.assertIsNone(report["records"])

    def test_invalid_ascii_name_keeps_existing_decoder_failure_priority(self):
        data = document("#1=A();\n").replace(b"FILE_NAME('x'", b"FILE_NAME('\\Q\\" + b"a" * 200 + b"'")
        old_run, old = self.invoke(data, "--max-string-bytes", 32)
        new_run, new = self.invoke(data, FLAG, "--max-string-bytes", 32)
        self.assertEqual(1, old_run.returncode)
        self.assertEqual("InvalidStringEncoding", old["diagnostics"][0]["key"])
        new.pop("header_name_policy")
        self.assertEqual(old_run.returncode, new_run.returncode)
        self.assertEqual(old, new)
