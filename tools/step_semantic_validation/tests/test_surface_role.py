# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Necessary surface-role evidence; passing WR7 never grants native/whole-source support."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import AP203, AP214
from tools.step_semantic_validation.tests.test_ap214_curve_style import MBX
from tools.step_semantic_validation.tests.test_ap214_entities import append_records
from tools.step_semantic_validation.tests.test_wireframe import linked_wire_source

CLAUSE = "SourceRuleViolation:GEOMETRICALLY_BOUNDED_SURFACE_SHAPE_REPRESENTATION.WR7"


def surface_role_source(schema=AP214, *, surface=False, second_set=False, curve_set=False, sphere=False):
    data = linked_wire_source(schema).replace(b"GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION",
                                             b"GEOMETRICALLY_BOUNDED_SURFACE_SHAPE_REPRESENTATION")
    if not curve_set:
        data = data.replace(b"#910=GEOMETRIC_CURVE_SET", b"#910=GEOMETRIC_SET")
    if sphere:
        data = append_records(data, b"#940=SPHERICAL_SURFACE('original sphere',#904,+3.250E0);\n")
    else:
        data = append_records(data, b"#940=PLANE('other surface',#904);\n")
    if surface:
        if second_set:
            data = append_records(data, b"#950=GEOMETRIC_SET('surface set',(#940));\n")
            data = data.replace(b"(#910,#904)", b"(#910,#904,#950)")
        else:
            data = data.replace(b"(#911,#912)", b"(#911,#912,#940)")
    return data


class SurfaceRoleSourceTests(unittest.TestCase):
    def invoke(self, data, *flags):
        with tempfile.TemporaryDirectory(prefix="step-surface-role-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path),
                                  "--general-brep-source", *flags], capture_output=True, timeout=30)
        self.assertTrue(run.stdout, run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(data.decode(), report["source"]["input"]["raw"])
        self.assertIsNone(report["snapshot"])
        return run, report

    def test_other_surfaces_in_the_file_never_satisfy_direct_membership(self):
        for schema in (AP214, MBX):
            for sphere, curve_set in ((False, False), (True, False), (False, True)):
                with self.subTest(schema=schema, sphere=sphere, curve_set=curve_set):
                    run, report = self.invoke(surface_role_source(schema, sphere=sphere, curve_set=curve_set))
                    self.assertEqual(1, run.returncode, run.stderr or report["diagnostics"])
                    issue = report["diagnostics"][0]
                    self.assertEqual(CLAUSE, issue["key"])
                    self.assertEqual(("920", "items", [2]),
                                     tuple(issue[k] for k in ("record_id", "field", "parameter_path")))
                    self.assertIn("WR7", issue["responsibility"])

    def test_a_direct_surface_in_any_set_passes_only_the_necessary_clause(self):
        for schema in (AP214, MBX):
            for second_set, sphere in ((False, False), (True, False), (True, True)):
                with self.subTest(schema=schema, second_set=second_set, sphere=sphere):
                    run, report = self.invoke(surface_role_source(schema, surface=True, second_set=second_set, sphere=sphere))
                    self.assertEqual(2, run.returncode, run.stderr or report["diagnostics"])
                    self.assertEqual("UnsupportedRepresentationGraph", report["diagnostics"][0]["key"])
                    self.assertEqual("Passed", report["checks"]["geometry_dependencies"])

    def test_sphere_radius_keeps_the_existing_positive_length_requirement(self):
        data = surface_role_source(sphere=True).replace(b"+3.250E0", b"0.")
        run, report = self.invoke(data)
        self.assertEqual(1, run.returncode)
        self.assertEqual("SourceRuleViolation:POSITIVE_LENGTH_MEASURE.WR1", report["diagnostics"][0]["key"])

    def test_other_protocol_catalogs_do_not_gain_the_new_surface_role(self):
        for schema in (AP203, b"CONFIG_CONTROL_DESIGN"):
            run, report = self.invoke(surface_role_source(schema))
            self.assertEqual(2, run.returncode)
            self.assertEqual("UnsupportedEntityMapping", report["diagnostics"][0]["key"])

    def test_resource_failure_never_publishes_a_semantic_snapshot(self):
        run, report = self.invoke(surface_role_source(), "--max-semantic-visits", "0")
        self.assertEqual(3, run.returncode)
        self.assertEqual("ResourceLimit", report["status"])
