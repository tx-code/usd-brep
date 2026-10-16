# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Black-box source-reader contracts. Set STEP_INSPECT_BINARY to the compiled CLI."""

from __future__ import annotations

from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_import_validation.common import InvalidEvidence, digest, load_json, write_json
from tools.step_import_validation.corpus import verify
from tools.step_inspect_validation import runner

REPO = Path(__file__).resolve().parents[3]
CORPUS = REPO / "tools/step_import_validation/corpus/ap203e2-planar/v1"


def document(records: str) -> bytes:
    return ("ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('independent test'),'2;1');\n"
            "FILE_NAME('x','2026-10-16',('a'),('b'),'p','s','');\nFILE_SCHEMA(('TEST'));\n"
            "ENDSEC;\nDATA;\n" + records + "ENDSEC;\nEND-ISO-10303-21;\n").encode("ascii")


class InspectTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        configured = os.environ.get("STEP_INSPECT_BINARY")
        if not configured:
            raise RuntimeError("Set STEP_INSPECT_BINARY; these tests must execute the actual C++ reader")
        cls.binary = Path(configured).resolve(strict=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="step-ticket02-")
        cls.root = Path(cls.temporary.name).resolve()
        assert cls.root.parent == Path(tempfile.gettempdir()).resolve()
        cls.cases = verify(CORPUS)["cases"]

    @classmethod
    def tearDownClass(cls):
        assert cls.root.parent == Path(tempfile.gettempdir()).resolve() and cls.root.name.startswith("step-ticket02-")
        cls.temporary.cleanup()

    def invoke(self, data: bytes, *options):
        path = self.root / "probe.step"
        path.write_bytes(data)
        actual = subprocess.run([str(self.binary), "--fixture-limits", str(path), *map(str, options)],
                                capture_output=True, timeout=10)
        return actual, json.loads(actual.stdout) if actual.stdout else None

    def good(self, data: bytes, *options):
        actual, result = self.invoke(data, *options)
        self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
        self.assertEqual("Inspected", result["status"])
        self.assertTrue(result["source_complete"])
        self.assertEqual(data, result["input"]["raw"].encode("ascii"))
        return result

    def rejected(self, data: bytes, key=None, code=1, *options):
        actual, result = self.invoke(data, *options)
        self.assertEqual(code, actual.returncode, actual.stderr or actual.stdout)
        self.assertFalse(result["source_complete"])
        self.assertIsNone(result["records"])
        self.assertIsNone(result["header"])
        if key:
            self.assertEqual(key, result["diagnostics"][0]["key"])
        return result

    def test_all_forty_nine_frozen_source_contracts(self):
        statuses = []
        for case in self.cases:
            with self.subTest(case=case["case_id"]):
                path = CORPUS / case["identity"]["materials"]["input"]["path"]
                data = path.read_bytes()
                actual, result = self.invoke(data)
                inventory = load_json(CORPUS / case["identity"]["materials"]["inventory"]["path"])
                comparison = runner.compare(case, result, actual.returncode, data, inventory)
                self.assertEqual("Matched", comparison["status"])
                self.assertEqual("NotChecked", comparison["full_import_contract"])
                statuses.append(result["status"])
        self.assertEqual((47, 2), (statuses.count("Inspected"), statuses.count("Rejected")))

    def test_all_value_kinds_and_exact_spelling(self):
        data = document("#1=THING('it''s; #999',12,-03,+4,1.,-0.0E+999,.U.,$,* ,(),TYPED((#2)));\n#2=OTHER();\n")
        result = self.good(data)
        p = result["records"][0]["components"][0]["parameters"]
        self.assertEqual(["String", "Integer", "Integer", "Integer", "Real", "Real", "Enumeration", "Unset",
                          "Derived", "Aggregate", "Typed"], [v["kind"] for v in p])
        self.assertEqual("it's; #999", p[0]["text"])
        self.assertEqual("-03", p[2]["text"])
        self.assertEqual("-0.0E+999", p[5]["text"])
        self.assertEqual(1, len(result["references"]))
        self.assertEqual([11, 1, 1], result["references"][0]["parameter_path"])

    def test_multiline_comments_and_crlf_positions(self):
        data = document("/* ; #888=IGNORED(); */\n#2=NEXT(\n/* ) */#1);\n#1=FIRST('/*literal*/');\n")
        result = self.good(data.replace(b"\n", b"\r\n"))
        self.assertEqual(["2", "1"], [r["id"] for r in result["records"]])
        self.assertEqual({"line": 9, "column": 1}, result["records"][0]["location"])
        self.assertEqual("1", result["references"][0]["target_id"])

    def test_forward_references_and_reordering_preserve_graph(self):
        data = (CORPUS / "cases/S203-P01/input.step").read_bytes()
        before = self.good(data)
        prefix, tail = data.split(b"\nDATA;\n", 1)
        records, suffix = tail.split(b"ENDSEC;", 1)
        reordered = prefix + b"\nDATA;\n" + b"\n".join(reversed(records.rstrip().splitlines()))
        reordered += b"\nENDSEC;" + suffix
        after = self.good(reordered)
        self.assertEqual({r["id"]: r["raw"] for r in before["records"]}, {r["id"]: r["raw"] for r in after["records"]})
        self.assertEqual(51, len(after["records"]))

    def test_huge_sparse_ids_use_string_identity(self):
        huge = "9" * 100
        result = self.good(document(f"#{huge}=A(#1);\n#1=B(#{huge});\n"))
        self.assertEqual(huge, result["records"][0]["id"])
        self.assertEqual(2, len(result["records"]))

    def test_leading_zero_ids_are_the_same_identity(self):
        result = self.rejected(document("#0001=A();\n#1=B();\n"), "DuplicateInstanceId")
        self.assertEqual("1", result["diagnostics"][0]["record_id"])
        self.assertEqual(1, len(result["diagnostics"][0]["related"]))

    def test_zero_id_is_invalid(self):
        self.rejected(document("#0=A();\n"), "InvalidInstanceId")

    def test_cyclic_raw_graph_is_not_a_recursive_walk(self):
        result = self.good(document("#1=A(#2);\n#2=B(#1);\n"))
        self.assertEqual(2, len(result["references"]))

    def test_unknown_records_and_all_complex_members_are_preserved(self):
        result = self.good(document("#1=(UNKNOWN_A($) UNKNOWN_B(*) UNKNOWN_C(VALUE(7)));\n"))
        self.assertEqual(["UNKNOWN_A", "UNKNOWN_B", "UNKNOWN_C"],
                         [c["name"] for c in result["records"][0]["components"]])
        self.assertEqual("NotRun", result["schema_validation"])

    def test_late_errors_do_not_publish_an_earlier_graph(self):
        result = self.rejected(document("#1=A();\n#2=B(#999);\n"), "UnresolvedRequiredReference")
        self.assertEqual("2", result["diagnostics"][0]["record_id"])
        self.assertFalse(result["statistics"]["complete"])
        between = self.rejected(document("#1=A();\n?\n"), "UnexpectedCharacter")
        self.assertEqual("", between["diagnostics"][0]["record_id"])
        self.assertEqual("", between["diagnostics"][0]["component"])

    def test_truncated_files_have_no_complete_source(self):
        data = document("#1=A('text',(1.,2.,3.));\n")
        for cut in (0, 4, 30, len(data) // 2, len(data) - 2, len(data) - 10):
            with self.subTest(cut=cut):
                self.rejected(data[:cut])

    def test_unterminated_comments_and_strings_are_located(self):
        self.rejected(document("#1=A();\n") + b"/* open", "UnterminatedComment")
        self.rejected(document("#1=A('open\n"), "UnsupportedStringEncoding", 2)
        data = document("#1=A();\n").split(b"#1=")[0] + b"#1=A('open"
        self.rejected(data, "UnterminatedString")

    def test_invalid_number_spellings_are_rejected(self):
        for value in (".5", "1E3", "2.E+", "+ 2", "1.0e4", "1.2.3", "-", "NaN"):
            with self.subTest(value=value):
                self.rejected(document(f"#1=A({value});\n"))

    def test_typed_value_and_separator_arity(self):
        for value in ("TYPE()", "TYPE(1,2)", "(1,)", "(,1)", "1 2", "", "1,,2"):
            if value == "":
                continue  # An empty entity parameter list is valid source syntax.
            with self.subTest(value=value):
                self.rejected(document(f"#1=A({value});\n"))

    def test_empty_parameter_lists_and_data_are_inspectable(self):
        self.assertEqual([], self.good(document(""))["records"])
        self.assertEqual([], self.good(document("#1=A();\n"))["records"][0]["components"][0]["parameters"])

    def test_non_ascii_and_bom_are_explicitly_unsupported(self):
        for data in (b"\xef\xbb\xbf" + document(""), b"\xff\xfeI\x00", document("") + "中".encode("utf-8"),
                     document("") + b"\x00"):
            with self.subTest(prefix=data[:5]):
                self.rejected(data, "UnsupportedEncoding", 2)

    def test_archive_xml_and_gzip_carriers_are_unsupported(self):
        for data in (b"PK\x03\x04rest", b"<?xml version='1.0'?>", b"\x1f\x8brest"):
            self.rejected(data, "UnsupportedCarrier", 2)

    def test_unimplemented_string_directives_are_not_silently_decoded(self):
        for directive in ("\\X2\\4E2D\\X0\\", "\\X4\\00004E2D\\X0\\", "\\S\\D", "\\PA\\"):
            self.rejected(document("#1=A('" + directive + "');\n"), "UnsupportedStringEncoding", 2)

    def test_literal_backslash_and_apostrophe_decoding(self):
        result = self.good(document("#1=A('a\\\\b','it''s');\n"))
        self.assertEqual(["a\\b", "it's"], [p["text"] for p in result["records"][0]["components"][0]["parameters"]])

    def test_binary_user_defined_and_print_extensions_are_unsupported(self):
        for record in ('#1=A("0F");\n', '#1=!USER();\n', '@1=A();\n', '\\N\\\n#1=A();\n'):
            self.rejected(document(record), "UnsupportedToken", 2)

    def test_other_implementation_levels_are_unsupported(self):
        for level in (b"1", b"2;2", b"3;1"):
            self.rejected(document("").replace(b"'2;1'", b"'" + level + b"'"), "UnsupportedImplementationLevel", 2)

    def test_sections_and_trailing_content_are_not_ignored(self):
        data = document("#1=A();\n")
        self.rejected(data.replace(b"END-ISO-10303-21;", b"DATA; #2=B(); ENDSEC; END-ISO-10303-21;"),
                      "UnsupportedSection", 2)
        self.rejected(data.replace(b"DATA;", b"DATA('name',('TEST'));"), "UnsupportedDataSection", 2)
        self.rejected(data.replace(b"DATA;", b"REFERENCE; ENDSEC; DATA;"), "UnsupportedSection", 2)
        self.rejected(data + b"#9=EXTRA();", "TrailingContent")

    def test_header_completeness_and_shapes(self):
        data = document("")
        self.rejected(data.replace(b"FILE_SCHEMA(('TEST'));\n", b""))
        self.rejected(data.replace(b"FILE_SCHEMA(('TEST'))", b"FILE_SCHEMA(3)"), "InvalidHeader")
        self.rejected(data.replace(b"FILE_NAME('x','2026-10-16',('a'),('b'),'p','s','')", b"FILE_NAME('x')"),
                      "InvalidHeader")
        self.rejected(data.replace(b"ENDSEC;\nDATA;", b"FILE_POPULATION(); ENDSEC;\nDATA;"),
                      "UnsupportedHeaderRecord", 2)

    def test_all_basic_resource_boundaries(self):
        data = document("#1=A('abc',(#2,3.),TYPE(4));\n#2=B();\n")
        baseline = self.good(data)
        stats = baseline["statistics"]
        boundaries = [("input-bytes", len(data), "InputByteLimit"), ("records", 2, "RecordLimit"),
                      ("values", stats["values_including_header"], "ValueLimit"),
                      ("references", stats["references"], "ReferenceLimit"),
                      ("depth", stats["value_depth"], "NestingLimit"),
                      ("string-bytes", stats["max_string_bytes"], "StringByteLimit"),
                      ("record-bytes", stats["max_record_bytes"], "RecordByteLimit"), ("id-digits", 1, "IdDigitLimit")]
        for option, value, diagnostic in boundaries:
            with self.subTest(option=option):
                self.good(data, f"--max-{option}", value)
                self.rejected(data, diagnostic, 3, f"--max-{option}", value - 1)

    def test_report_byte_limit_never_emits_partial_json(self):
        data = document("#1=A();\n")
        initial, result = self.invoke(data)
        self.assertEqual(0, initial.returncode)
        maximum = len(initial.stdout)
        # Report includes its own limit value; converge using actual stdout bytes.
        for _ in range(3):
            actual, _ = self.invoke(data, "--max-report-bytes", maximum)
            self.assertEqual(0, actual.returncode, actual.stderr)
            maximum = len(actual.stdout)
        actual, result = self.invoke(data, "--max-report-bytes", maximum - 1)
        self.assertEqual(5, actual.returncode)
        self.assertIsNone(result)
        self.assertEqual(b"", actual.stdout)
        self.assertIn(b"ReportByteLimit", actual.stderr)

    def test_implementation_safety_ceiling_and_invalid_arguments(self):
        self.rejected(document(""), "InvalidLimits", 3, "--max-depth", 129)
        for options in (("--max-records", "-1"), ("--max-records", "999999999999999999999999"),
                        ("--unknown", "2")):
            actual, result = self.invoke(document(""), *options)
            self.assertEqual(64, actual.returncode)
            self.assertIsNone(result)

    def test_sha256_independent_multi_block_vectors(self):
        for length in (0, 3, 55, 56, 63, 64, 65, 1000):
            data = b"a" * length
            actual, result = self.invoke(data)
            self.assertEqual(hashlib.sha256(data).hexdigest(), result["input"]["sha256"])
            self.assertNotEqual(0, actual.returncode)

    def test_missing_file_is_io_failure_without_captured_snapshot(self):
        actual = subprocess.run([str(self.binary), "--fixture-limits", str(self.root / "nonexistent.step")],
                                capture_output=True, timeout=10)
        result = json.loads(actual.stdout)
        self.assertEqual(4, actual.returncode)
        self.assertEqual("IoFailure", result["status"])
        self.assertFalse(result["input"]["capture_complete"])
        self.assertIsNone(result["input"]["sha256"])
        self.assertIsNone(result["records"])

    def test_checker_rejects_dropped_constituent_and_reference(self):
        case = next(c for c in self.cases if c["case_id"] == "S203-P01")
        data = (CORPUS / "cases/S203-P01/input.step").read_bytes()
        actual, result = self.invoke(data)
        inventory = load_json(CORPUS / "cases/S203-P01/inventory.json")
        for label in ("constituent", "reference", "false_complete"):
            bad = deepcopy(result)
            if label == "constituent":
                next(r for r in bad["records"] if r["complex"])["components"].pop()
            elif label == "reference":
                bad["references"].pop()
            else:
                bad["source_complete"] = False
            with self.subTest(label=label), self.assertRaises(InvalidEvidence):
                runner.compare(case, bad, actual.returncode, data, inventory)

    def test_evidence_invokes_reader_and_commits_selected_cases_only(self):
        output = self.root / "evidence"
        before = digest((CORPUS / "manifest.json").read_bytes())
        selected = ["S203-P01", "S203-N02", "S203-N03"]
        first = runner.run(self.binary, CORPUS, output, selected)
        second = runner.run(self.binary, CORPUS, output, ["S203-P01"])
        self.assertNotEqual(first.name, second.name)
        self.assertEqual(3, runner.check(first)["source_contract_matched"])
        self.assertEqual(1, runner.check(second)["source_contract_matched"])
        summary = load_json(first / "summary.json")
        self.assertEqual({"Inspected": 1, "Rejected": 2}, summary["source_statuses"])
        self.assertEqual(selected, [r["case_id"] for r in summary["cases"]])
        self.assertIsNone(summary["import_success_count"])
        self.assertEqual(before, digest((CORPUS / "manifest.json").read_bytes()))

    def test_small_mutations_fail_safely_without_partial_graphs(self):
        data = document("#1=A('q',(.T.,$,*),TYPE(3.));\n")
        for pos in range(0, len(data), 7):
            with self.subTest(position=pos):
                actual, result = self.invoke(data[:pos] + b"?" + data[pos + 1:])
                self.assertIn(actual.returncode, (0, 1, 2))
                self.assertEqual(actual.returncode == 0, result["source_complete"])
                if actual.returncode:
                    self.assertIsNone(result["records"])


if __name__ == "__main__":
    unittest.main()
