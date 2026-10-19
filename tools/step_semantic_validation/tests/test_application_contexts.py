# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Distinct descriptive application identities do not create extra parts."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap203_general import BOX


def distinct_context_box():
    data = BOX.replace(b"#6=PRODUCT_DEFINITION_CONTEXT('part definition',#1,'design')",
                       b"#6=PRODUCT_DEFINITION_CONTEXT('part definition',#600,'design')")
    return data.replace(b"ENDSEC;\nEND-ISO",
                        b"#600=APPLICATION_CONTEXT('separate design application');\nENDSEC;\nEND-ISO")


class ApplicationContextTests(unittest.TestCase):
    def invoke(self, data, interop=True):
        with tempfile.TemporaryDirectory(prefix="step-application-context-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            args = [os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path), "--general-brep-source"]
            if interop:
                args.append("--interop-nongeometric")
            run = subprocess.run(args, capture_output=True, timeout=30)
        return run, json.loads(run.stdout)

    def test_actual_selected_context_references_are_preserved_without_merging(self):
        data = distinct_context_box()
        strict, old = self.invoke(data, False)
        run, report = self.invoke(data)
        self.assertEqual(2, strict.returncode)
        self.assertEqual("UnsupportedSinglePartGraph", old["diagnostics"][0]["key"])
        self.assertEqual(0, run.returncode, run.stderr or run.stdout)
        self.assertEqual("Passed", report["checks"]["interoperability_approval"])
        preserved = [d for d in report["preserved_diagnostics"] if d["key"] == "UnsupportedSinglePartGraph"]
        self.assertEqual(1, len(preserved))
        roles = report["snapshot"]["part"]["roles"]
        self.assertTrue(roles)
        raw = {r["id"]: r for r in report["source"]["records"]}
        self.assertEqual("#1", raw["3"]["components"][0]["parameters"][1]["raw"])
        self.assertEqual("#600", raw["6"]["components"][0]["parameters"][1]["raw"])
        self.assertEqual(data.decode(), report["source"]["input"]["raw"])

    def test_an_unreferenced_application_context_cannot_acquire_a_root(self):
        data = distinct_context_box().replace(b"ENDSEC;\nEND-ISO",
                                             b"#601=APPLICATION_CONTEXT('unattached');\nENDSEC;\nEND-ISO")
        run, report = self.invoke(data)
        self.assertEqual(2, run.returncode)
        self.assertIsNone(report["snapshot"])

    def test_an_application_context_is_not_a_geometric_unit_context(self):
        data = distinct_context_box().replace(b"('body',(#21),#12)", b"('body',(#21),#600)")
        run, report = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report["snapshot"])

    def test_protocols_follow_their_actual_application_references(self):
        extra = b"#601=APPLICATION_PROTOCOL_DEFINITION('international standard','separate_metadata_claim',1994,#600);\n"
        data = distinct_context_box().replace(b"ENDSEC;\nEND-ISO", extra + b"ENDSEC;\nEND-ISO")
        run, report = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or run.stdout)
        keys = [d["key"] for d in report["preserved_diagnostics"]]
        self.assertEqual(2, keys.count("UnsupportedSinglePartGraph"))
        self.assertEqual(data.decode(), report["source"]["input"]["raw"])
        duplicate = b"#602=APPLICATION_PROTOCOL_DEFINITION('international standard','other',2000,#600);\n"
        run, report = self.invoke(data.replace(b"ENDSEC;\nEND-ISO", duplicate + b"ENDSEC;\nEND-ISO"))
        self.assertEqual(2, run.returncode)
        self.assertIsNone(report["snapshot"])
