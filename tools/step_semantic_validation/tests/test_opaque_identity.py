# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Unavailable identity/caption text needs exact selected ownership."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap203_general import BOX


def opaque_identity_box(admin=True):
    data = BOX.replace(b"PRODUCT_DEFINITION_FORMATION('A'", b"PRODUCT_DEFINITION_FORMATION('\x80'")
    data = data.replace(b"PRODUCT_DEFINITION('D1'", b"PRODUCT_DEFINITION('\x81'")
    data = data.replace(b"MANIFOLD_SOLID_BREP('',#22)", b"MANIFOLD_SOLID_BREP('\x82',#22)")
    if admin:
        extra = (
            b"#900=APPROVAL_STATUS('approved');\n#901=APPROVAL(#900,'\x83');\n"
            b"#902=CC_DESIGN_APPROVAL(#901,(#4));\n#903=PERSON('\x84','tester',$,$,$,$);\n"
            b"#904=APPROVAL_ROLE('approver');\n#905=APPROVAL_PERSON_ORGANIZATION(#903,#901,#904);\n"
        )
        data = data.replace(b"ENDSEC;\nEND-ISO", extra + b"ENDSEC;\nEND-ISO")
    return data


class OpaqueIdentityTests(unittest.TestCase):
    def invoke(self, data, interop=True, *flags):
        with tempfile.TemporaryDirectory(prefix="step-opaque-identity-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            args = [os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path),
                    "--general-brep-source", "--preserve-opaque-data-strings"]
            if interop:
                args.append("--interop-nongeometric")
            run = subprocess.run(args + list(map(str, flags)), capture_output=True, timeout=30)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_singleton_identities_selected_shape_and_attached_admin_keep_unavailable_text(self):
        data = opaque_identity_box()
        run, report = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or run.stdout)
        self.assertEqual("CheckedWithDiagnostics", report["status"])
        self.assertEqual("Passed", report["checks"]["interoperability_approval"])
        preserved = [d for d in report["preserved_diagnostics"] if d["key"] == "OpaqueDataTextUnestablished"]
        self.assertEqual(5, len(preserved))
        records = {r["id"]: r for r in report["source"]["records"]}
        for record, slot in (("5", 0), ("7", 0), ("21", 0), ("901", 1), ("903", 0)):
            value = records[record]["components"][0]["parameters"][slot]
            self.assertEqual("OpaqueString", value["kind"])
            self.assertIsNone(value["text"])
        self.assertEqual(data, report["source"]["input"]["raw"].encode("latin1"))

    def test_strict_source_and_extra_definitions_do_not_inherit_the_exception(self):
        run, report = self.invoke(opaque_identity_box(), False)
        self.assertEqual(2, run.returncode)
        self.assertIsNone(report["snapshot"])
        extra = b"#600=PRODUCT_DEFINITION_FORMATION('B',$,#4);\n"
        data = opaque_identity_box(False).replace(b"ENDSEC;\nEND-ISO", extra + b"ENDSEC;\nEND-ISO")
        run, report = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report["snapshot"])

    def test_orphan_administrative_labels_cannot_invent_part_ownership(self):
        data = opaque_identity_box().replace(b"#905=APPROVAL_PERSON_ORGANIZATION(#903,#901,#904);\n", b"")
        run, report = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report["snapshot"])

    def test_representation_purpose_and_geometry_fields_are_still_not_captions(self):
        for data in (
            BOX.replace(b"'body',(#21)", b"'\x80',(#21)"),
            BOX.replace(b"#100=CARTESIAN_POINT(''", b"#100=CARTESIAN_POINT('\x80'"),
            BOX.replace(b"(2.,-3.,5.)", b"('\x80',-3.,5.)"),
        ):
            run, report = self.invoke(data)
            self.assertNotEqual(0, run.returncode)
            self.assertIsNone(report["snapshot"])

    def test_preservation_capacity_is_still_a_failure_before_snapshot_publication(self):
        run, report = self.invoke(opaque_identity_box(), True, "--max-preserved-diagnostics", 0)
        self.assertEqual(3, run.returncode)
        self.assertIsNone(report["snapshot"])
