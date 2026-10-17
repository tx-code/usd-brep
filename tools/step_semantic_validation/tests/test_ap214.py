# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""AP214 source declaration contracts through the existing real semantic CLI."""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

CORPUS = Path(__file__).resolve().parents[2] / "step_import_validation/corpus/ap203e2-planar/v1/cases"
AP203 = b"AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF {1 0 10303 403 2 1 2}"
AP214 = b"AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}"
AP214_SHA = "71ab140fe7f774321beee6a31e6fee2afc3973fd60350ae2018c74c211fb4295"


def ap214_source(data):
    assert data.count(AP203) == 1
    return data.replace(AP203, AP214).replace(
        b"'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011",
        b"'automotive_design',2010")


def converted_ap214_source():
    data = ap214_source((CORPUS / "S203-P11/input.step").read_bytes())
    # The fixed AP214 artifact redeclares dimensions as derived. Remove the AP203-only
    # explicit dimensions record so the positive source has no orphan dependency.
    assert data.count(b"NAMED_UNIT(#17)") == 1
    data = data.replace(b"NAMED_UNIT(#17)", b"NAMED_UNIT(*)")
    data, count = re.subn(rb"(?m)^#17=[^\r\n]*\r?\n", b"", data)
    assert count == 1
    return data


class Ap214Tests(unittest.TestCase):
    def invoke(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="step-ap214-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path), *map(str, options)],
                                 capture_output=True, timeout=15)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_exact_ap214_profile_checks_source_fields_with_its_own_identity(self):
        data = ap214_source((CORPUS / "S203-P01/input.step").read_bytes())
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual("Checked", result["status"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        schema = result["snapshot"]["schema"]
        self.assertEqual("AUTOMOTIVE_DESIGN", schema["name"])
        self.assertEqual([1, 0, 10303, 214, 3, 1, 1], schema["oid"])
        self.assertEqual(AP214_SHA, schema["express_sha256"])
        self.assertEqual("214e3-P0-Source-0.1", schema["profile"])
        self.assertNotIn("MECHANICAL_CONTEXT", result["snapshot"]["type_populations"])
        self.assertNotIn("DESIGN_CONTEXT", result["snapshot"]["type_populations"])
        self.assertEqual("NotRun", result["checks"]["global_rules_47"])
        self.assertEqual("NotRun", result["checks"]["native_build"])

    def test_conversion_dimensions_remain_a_derived_source_marker(self):
        data = converted_ap214_source()
        run, result = self.invoke(data)
        self.assertEqual(7, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual("Deferred", result["status"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        record = next(r for r in result["snapshot"]["records"] if r["id"] == "19")
        attribute = next(a for a in record["attributes"] if a["name"] == "dimensions")
        self.assertEqual("NAMED_UNIT", attribute["declared_in"])
        self.assertEqual("CONVERSION_BASED_UNIT", attribute["redeclared_by"])
        self.assertTrue(attribute["derived_slot"])
        self.assertIsNone(result["snapshot"]["units"]["length_scale_mm"])
        raw_record = next(r for r in result["source"]["records"] if r["id"] == "19")
        self.assertIn("NAMED_UNIT(*)", raw_record["raw"])
        # All defining 3D geometry/topology records keep their literal source values.
        original = (CORPUS / "S203-P11/input.step").read_bytes()
        for line in original.splitlines():
            if any(name in line for name in (b"=CARTESIAN_POINT(", b"=LINE(", b"=PLANE(", b"=EDGE_CURVE(", b"=ORIENTED_EDGE(")):
                self.assertIn(line, data)

    def test_explicit_ap214_dimensions_are_rejected_before_publication(self):
        data = ap214_source((CORPUS / "S203-P11/input.step").read_bytes())
        run, result = self.invoke(data)
        self.assertEqual(1, run.returncode)
        self.assertIsNone(result["snapshot"])
        diagnostic = result["diagnostics"][0]
        self.assertEqual("DerivedSlotMarkerRequired", diagnostic["key"])
        self.assertEqual("19", diagnostic["record_id"])
        self.assertEqual("dimensions", diagnostic["field"])
        begin, end = diagnostic["span"]
        self.assertEqual(b"#17", data[begin:end])

    def test_retired_contexts_and_other_versions_do_not_use_the_ap214_catalog(self):
        data = ap214_source((CORPUS / "S203-P10/input.step").read_bytes())
        run, result = self.invoke(data)
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedEntityType", result["diagnostics"][0]["key"])
        for identifier in (b"AUTOMOTIVE_DESIGN", b"AUTOMOTIVE_DESIGN {1 0 10303 214 2 1 1}",
                           b"AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1 1}"):
            data = ap214_source((CORPUS / "S203-P01/input.step").read_bytes()).replace(AP214, identifier)
            run, result = self.invoke(data)
            self.assertEqual(2, run.returncode)
            self.assertEqual("UnsupportedSchemaBinding", result["diagnostics"][0]["key"])
            self.assertIsNone(result["schema_selection"]["qualified_profile"])

    def test_source_visits_and_report_limits_remain_bounded(self):
        data = ap214_source((CORPUS / "S203-P01/input.step").read_bytes())
        run, result = self.invoke(data)
        count = result["statistics"]["dependency_visits"]
        self.assertEqual(0, self.invoke(data, "--max-semantic-visits", count)[0].returncode)
        limited, result = self.invoke(data, "--max-semantic-visits", count - 1)
        self.assertEqual(3, limited.returncode)
        self.assertIsNone(result["snapshot"])
        self.assertEqual("DependencyVisitLimit", result["diagnostics"][0]["key"])
        maximum = len(run.stdout)
        for _ in range(3):
            run, _ = self.invoke(data, "--max-report-bytes", maximum)
            self.assertEqual(0, run.returncode)
            maximum = len(run.stdout)
        run, result = self.invoke(data, "--max-report-bytes", maximum - 1)
        self.assertEqual(5, run.returncode)
        self.assertEqual(b"", run.stdout)
        self.assertIsNone(result)
