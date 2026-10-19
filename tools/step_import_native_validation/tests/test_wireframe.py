# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Linked geometry is complete, separately scoped and atomically published."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_wireframe import linked_wire_source
from tools.step_semantic_validation.tests.test_ap214 import AP203, AP214
from tools.step_semantic_validation.tests.test_ap214_curve_style import MBX
from tools.step_semantic_validation.tests.test_ap214_entities import append_records


def conic_wire_source():
    return append_records(linked_wire_source().replace(b"(#911,#912)", b"(#911,#912,#940)"),
                          b"#940=TRIMMED_CURVE('',#941,(PARAMETER_VALUE(0.)),"
                          b"(PARAMETER_VALUE(1.5707963267948966)),.T.,.PARAMETER.);\n"
                          b"#941=CIRCLE('',#904,2.);\n")


class LinkedWireframeNativeTests(unittest.TestCase):
    def invoke(self, data, *flags, write=False):
        with tempfile.TemporaryDirectory(prefix="step-wire-publication-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            command = [os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality",
                       "Q-AP214-development-mm-01", str(path), *map(str, flags)]
            if write:
                command += ["--write-native", str(Path(directory) / "model.smb")]
            run = subprocess.run(command, capture_output=True, timeout=30)
            self.assertTrue(run.stdout, run.stderr)
            report = json.loads(run.stdout)
            self.assertEqual(data.decode(), report["source_inspection"]["source"]["input"]["raw"])
            if write and run.returncode == 0:
                self.assertGreater((Path(directory) / "model.smb").stat().st_size, 0)
        return run, report

    def test_all_fixed_profiles_publish_separate_line_point_and_material_roles(self):
        for schema in (AP214, MBX, AP203, b"CONFIG_CONTROL_DESIGN"):
            with self.subTest(schema=schema):
                run, report = self.invoke(linked_wire_source(schema), write=True)
                self.assertEqual(0, run.returncode, run.stderr or report["diagnostics"])
                self.assertEqual("Accepted", report["status"])
                self.assertAlmostEqual(6000., report["model"]["material_volume"]["value_mm3"], places=5)
                group, = report["linked_wireframes"]
                self.assertEqual(("920", "931", "910", False), tuple(group[k] for k in
                                 ("source_representation", "source_relationship", "source_set", "material_boundary")))
                self.assertEqual(["912"], [x["source_id"] for x in group["curves"]])
                self.assertEqual(["911"], [x["source_id"] for x in group["points"]])

    def test_circle_parameter_mapping_and_complete_archive(self):
        run, report = self.invoke(conic_wire_source(), write=True)
        self.assertEqual(0, run.returncode, run.stderr or report["diagnostics"])
        self.assertEqual(["912", "940"], [x["source_id"] for x in report["linked_wireframes"][0]["curves"]])
        mapping, = [x for x in report["parameter_mappings"] if x["kind"] == "WireframeConicRationalParameter"]
        self.assertEqual(("940", "941", 1), tuple(mapping[k] for k in
                         ("source_geometry", "source_basis_geometry", "target_curve_index")))
        self.assertEqual(1., mapping["parameters"]["angle_parameter_to_rad"][0])

    def test_dimensionless_line_parameters_scale_vector_length_once(self):
        data = linked_wire_source().replace(b"SI_UNIT(.MILLI.,.METRE.)", b"SI_UNIT(.CENTI.,.METRE.)")
        data = data.replace(b"(#914),(#915),.T.,.CARTESIAN.",
                            b"(PARAMETER_VALUE(0.),#914),(PARAMETER_VALUE(5.),#915),.T.,.PARAMETER.")
        run, report = self.invoke(data, write=True)
        self.assertEqual(0, run.returncode, run.stderr or report["diagnostics"])
        self.assertEqual([70., 80., 90.], report["linked_wireframes"][0]["points"][0]["point_mm"])
        mapping, = [x for x in report["parameter_mappings"] if x["kind"] == "WireframeTrimmedLine"]
        self.assertEqual([0., 5.], mapping["parameters"]["basis_range"])
        self.assertEqual([10.], mapping["parameters"]["vector_magnitude_mm"])

    def test_conflicting_cuts_and_unsupported_nested_geometry_publish_nothing(self):
        conflict = linked_wire_source().replace(b"(#914),(#915),.T.,.CARTESIAN.",
                    b"(PARAMETER_VALUE(0.),#914),(PARAMETER_VALUE(6.),#915),.T.,.PARAMETER.")
        nested = append_records(linked_wire_source().replace(b"TRIMMED_CURVE('',#913", b"TRIMMED_CURVE('',#919"),
                                b"#919=TRIMMED_CURVE('',#913,(#914),(#915),.T.,.CARTESIAN.);\n")
        for data, key in ((conflict, "GeneralTrimmedRepresentationsConflict"),
                          (nested, "GeneralWireframeGeometryUnsupported")):
            with self.subTest(key=key):
                run, report = self.invoke(data)
                self.assertNotEqual(0, run.returncode)
                self.assertIsNone(report["model"])
                self.assertEqual(key, report["diagnostics"][0]["key"])

    def test_last_native_budget_cannot_publish_body_only(self):
        data = conic_wire_source()
        run, complete = self.invoke(data)
        self.assertEqual(0, run.returncode)
        run, limited = self.invoke(data, "--max-native-calls", complete["native_calls"] - 1)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(limited["model"])
        self.assertEqual("NativeCallLimit", limited["diagnostics"][0]["key"])
