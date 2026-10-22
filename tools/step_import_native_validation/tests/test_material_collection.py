# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Complete source inventory precedes atomic native collection publication."""
from decimal import Decimal
import re
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from tools.step_import_native_validation.tests import test_general


def two_material_boxes():
    """Two independent 10x20x30 boxes, with the second translated +100 mm X.

    Independent truth: each volume 6000 mm^3 / area 2200 mm^2; collection sums
    12000 / 4400, mass centroid (57,7,20), bounds (2,-3,5)..(112,17,35).
    No Boolean union, shape selection or geometry approximation is intended.
    """
    source = (test_general.CORPUS / "S203-P04/input.step").read_bytes()
    geometry = source[source.index(b"#21="):source.index(b"ENDSEC;", source.index(b"#21="))]
    copied = re.sub(rb"#(\d+)", lambda m: b"#" + str(int(m[1]) + 10000).encode(), geometry)
    copied = re.sub(rb"(CARTESIAN_POINT\('',\()([^,]+)",
                    lambda m: m[1] + format(Decimal(m[2].decode()) + 100, '.1f').encode(), copied)
    source = source.replace(b"'body',(#21)", b"'body',(#21,#10021)")
    return source.replace(b"ENDSEC;\nEND-ISO", copied + b"ENDSEC;\nEND-ISO")


class MaterialCollectionTests(unittest.TestCase):
    invoke_bytes = test_general.GeneralTests.invoke_bytes

    def test_all_source_bodies_retained_and_native_collection_is_not_silently_truncated(self):
        source = two_material_boxes()
        run, result = self.invoke_bytes(source)
        self.assertEqual('Unsupported', result['status'])
        self.assertEqual('Checked', result['source_inspection']['status'])
        self.assertEqual(0, result['native_calls'])
        self.assertIsNone(result['model'])
        self.assertEqual('GeneralMaterialCollectionNotImplemented', result['diagnostics'][-1]['key'])
        snapshot = result['source_inspection']['snapshot']['part']
        self.assertEqual('MaterialSolidCollection', snapshot['source_shape_role'])
        self.assertEqual([{'shape': '21', 'shell': '22', 'role': 'MaterialSolid'},
                          {'shape': '10021', 'shell': '10022', 'role': 'MaterialSolid'}], snapshot['source_shapes'])
        self.assertEqual(source, result['source_inspection']['source']['input']['raw'].encode('ascii'))

    def test_bad_second_body_cannot_be_hidden_by_valid_first_body(self):
        source = two_material_boxes().replace(b"#10022=CLOSED_SHELL", b"#10022=OPEN_SHELL")
        run, result = self.invoke_bytes(source)
        self.assertNotEqual(0, run.returncode)
        self.assertNotEqual('Checked', result['source_inspection']['status'])
        self.assertIsNone(result['model'])
        self.assertEqual(0, result['native_calls'])

    def test_shared_shell_does_not_erase_distinct_source_solid_identities(self):
        source = two_material_boxes()
        # Use the same source shell twice without leaving the cloned definitions orphaned.
        source = source[:source.index(b"#10021=")] + b"#10021=MANIFOLD_SOLID_BREP('',#22);\nENDSEC;\nEND-ISO-10303-21;\n"
        run, result = self.invoke_bytes(source)
        self.assertEqual('Checked', result['source_inspection']['status'])
        shapes = result['source_inspection']['snapshot']['part']['source_shapes']
        self.assertEqual(['21', '10021'], [s['shape'] for s in shapes])
        self.assertEqual(['22', '22'], [s['shell'] for s in shapes])
        self.assertIsNone(result['model'])
        self.assertEqual(0, result['native_calls'])

    def test_planar_source_profile_keeps_its_single_shape_guard(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/"public.step"
            path.write_bytes(two_material_boxes())
            run = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path)],
                                 capture_output=True, timeout=30)
            result = json.loads(run.stdout)
            self.assertNotEqual(0, run.returncode)
            self.assertIsNone(result['snapshot'])
            self.assertEqual('UnsupportedShapeMultiplicity', result['diagnostics'][0]['key'])

    def test_opaque_caption_proof_covers_the_second_selected_body(self):
        source = two_material_boxes().replace(b"#10021=MANIFOLD_SOLID_BREP('',", b"#10021=MANIFOLD_SOLID_BREP('\x80\xfe',")
        run, result = self.invoke_bytes(source, "--interop-nongeometric", "--preserve-opaque-data-strings")
        self.assertEqual('CheckedWithDiagnostics', result['source_inspection']['status'])
        self.assertEqual('GeneralMaterialCollectionNotImplemented', result['diagnostics'][-1]['key'])
        self.assertEqual(0, result['native_calls'])
        self.assertIsNone(result['model'])
        self.assertEqual(2, len(result['source_inspection']['snapshot']['part']['source_shapes']))
