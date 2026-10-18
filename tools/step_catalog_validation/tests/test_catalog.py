# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Corruption controls for complete metadata, immutable baselines and compiler failures."""

from argparse import Namespace
from copy import deepcopy
import os
from pathlib import Path
import shutil
import tempfile
import unittest

from tools.step_catalog_validation import catalog, compile_contracts

BASELINE = Path(os.environ.get("STEP_CATALOG_TEST_BASELINE",
                              catalog.ROOT / "source/STEP_SEMANTICS/tests/catalog/baseline"))


class CatalogControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.expected, cls.manifest = catalog.read_baseline(BASELINE)

    def changed(self, modify, location):
        actual = deepcopy(self.expected)
        modify(actual["profiles"])
        changes = catalog.differences(self.expected, actual)
        self.assertTrue(changes)
        self.assertIn(location, "\n".join(changes))

    def test_sealed_baseline_covers_all_profiles_and_full_registries(self):
        catalog.validate_catalog(self.expected)
        self.assertEqual(catalog.PROFILES, tuple(p["kind"] for p in self.expected["profiles"]))
        self.assertTrue(all(p["exclusive_groups"] and p["si_prefixes"] for p in self.expected["profiles"]))
        self.assertTrue(all(p["declarations"] and p["reference_selects"] for p in self.expected["profiles"]))
        self.assertEqual([], catalog.differences(self.expected, deepcopy(self.expected)))

    def test_order_corruption_is_visible(self):
        self.changed(lambda p: p.reverse(), "profiles[0]")
        self.changed(lambda p: p[0]["declarations"].reverse(), "declarations[0]")
        self.changed(lambda p: p[0]["registered_types"].reverse(), "registered_types[0]")
        def parents(profiles):
            declaration = next(d for d in profiles[0]["declarations"] if len(d["parents"]) > 1)
            declaration["parents"].reverse()
        self.changed(parents, "parents[0]")

    def test_unused_local_field_and_nested_metadata_corruption_is_visible(self):
        def scalar(profiles):
            field = next(d["fields"][0] for d in profiles[0]["declarations"] if d["fields"])
            field["optional"] = not field["optional"]
        self.changed(scalar, "optional")
        def nested(profiles):
            field = next(f for d in profiles[1]["declarations"] for f in d["fields"] if f["nested_aggregates"])
            field["nested_aggregates"][0]["minimum"] += 1
        self.changed(nested, "nested_aggregates[0].minimum")

    def test_membership_including_high_bits_and_flattened_origin_is_visible(self):
        self.changed(lambda p: p[0]["declarations"][0]["ancestors"].append(255), "ancestors")
        def binding(profiles):
            entry = next(d["flattened"][0] for d in profiles[0]["declarations"] if d["flattened"])
            entry["local_field_index"] += 1
        self.changed(binding, "local_field_index")

    def test_each_fixed_registry_is_compared(self):
        sections = ("reference_selects", "enumerations", "complex_signatures", "measure_branches",
                    "derived_slots", "exclusive_groups", "si_prefixes", "name_index")
        for section in sections:
            with self.subTest(section=section):
                self.changed(lambda p: p[0][section].pop(), section)

    def test_identity_capacity_and_scalar_types_are_compared(self):
        self.changed(lambda p: p[0].update(schema_sha256="wrong"), "schema_sha256")
        self.changed(lambda p: p[0].update(membership_capacity=128), "membership_capacity")
        self.assertTrue(catalog.differences({"id": 1}, {"id": True}))

    def test_duplicate_json_keys_and_nonfinite_values_are_rejected(self):
        for value in ('{"a":1,"a":2}', '{"a":NaN}', '{"a":Infinity}'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                catalog.loads(value)

    def test_missing_snapshot_sections_are_rejected(self):
        for section in catalog.PROFILE_KEYS:
            value = deepcopy(self.expected)
            value["profiles"][0].pop(section)
            with self.subTest(section=section), self.assertRaises(ValueError):
                catalog.validate_catalog(value)

    def test_tampered_payload_or_missing_role_cannot_be_used_as_baseline(self):
        with tempfile.TemporaryDirectory(prefix="catalog-controls-") as directory:
            copy = Path(directory) / "baseline"
            shutil.copytree(BASELINE, copy)
            (copy / "catalog.json").write_text("{}", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "hash mismatch"):
                catalog.read_baseline(copy)
            manifest = deepcopy(self.manifest)
            manifest["payload_sha256"].pop("catalog.json")
            (copy / "manifest.json").write_bytes(catalog.encoded(manifest))
            with self.assertRaisesRegex(ValueError, "roles"):
                catalog.read_baseline(copy)

    def test_capture_refuses_to_replace_existing_expected_data(self):
        with tempfile.TemporaryDirectory(prefix="catalog-existing-") as directory:
            target = Path(directory)
            sentinel = target / "keep.txt"
            sentinel.write_bytes(b"existing expected data")
            with self.assertRaisesRegex(ValueError, "Refusing to overwrite"):
                catalog.capture(Namespace(output=target))
            self.assertEqual(b"existing expected data", sentinel.read_bytes())

    def test_committed_baseline_contains_only_fixed_comparison_inputs(self):
        self.assertEqual({"format", "scope", "payload_sha256"}, set(self.manifest))
        self.assertEqual({"catalog.json"}, set(self.manifest["payload_sha256"]))


class CompilerControls(unittest.TestCase):
    def test_expected_rejection_needs_error_marker_and_nonzero_exit(self):
        case = {"expect": "fail", "marker": "CATALOG_INVALID"}
        self.assertTrue(compile_contracts.assess(case, 1, "file.cpp(5): error C2338: CATALOG_INVALID"))
        self.assertTrue(compile_contracts.assess(case, 1, "file.cpp:5: error: CATALOG_INVALID"))
        self.assertFalse(compile_contracts.assess(case, 0, "error: CATALOG_INVALID"))
        self.assertFalse(compile_contracts.assess(case, 1, "fatal error: header missing"))
        self.assertFalse(compile_contracts.assess(case, 1, "cl /DCATALOG_INVALID\nerror: missing header"))
        self.assertFalse(compile_contracts.assess(case, 1, "warning: CATALOG_INVALID\nerror: other failure"))

    def test_rejections_require_a_real_positive_case(self):
        manifest = catalog.loads(compile_contracts.DEFAULT_MANIFEST.read_text(encoding="utf-8"))
        compile_contracts.validate_cases(manifest)
        for key, replacement in (("marker", ""), ("positive_control", "missing"),
                                 ("positive_control", "driver_control_reject")):
            changed = deepcopy(manifest)
            changed["cases"][-1][key] = replacement
            with self.subTest(key=key, replacement=replacement), self.assertRaises(ValueError):
                compile_contracts.validate_cases(changed)

    def test_empty_compilation_suite_cannot_pass(self):
        with self.assertRaisesRegex(ValueError, "empty compile suite"):
            compile_contracts.validate_cases({"format": "step-catalog-compile-cases.v1", "cases": []})

    def test_runtime_controls_require_positive_executable_targets(self):
        manifest = catalog.loads(compile_contracts.DEFAULT_MANIFEST.read_text(encoding="utf-8"))
        for index in (0, -1):
            changed = deepcopy(manifest)
            changed["cases"][index]["run"] = True
            with self.subTest(index=index), self.assertRaises(ValueError):
                compile_contracts.validate_cases(changed)


if __name__ == "__main__":
    unittest.main()
