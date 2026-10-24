# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""A caller-selected finer work zone keeps the same geometric admission budgets."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

BOX = (Path(__file__).resolve().parents[3]
       / 'tools/step_import_validation/corpus/ap203e2-planar/v1/cases/S203-P04/input.step')
FINE = 'Q-AP214-fine-work-tolerance-study-mm-01'
ORDINARY = 'Q-AP214-development-mm-01'


def thin_box_source():
    # Frozen synthetic box spans x=2..12, y=-3..17, z=5..35. Change only
    # x=12 to x=2.0005: exact thickness 0.0005, area 1200.05, volume 0.3.
    data = BOX.read_bytes()
    assert data.count(b'(12.,') == 4
    return data.replace(b'(12.,', b'(2.0005,')


class FineWorkQualityTests(unittest.TestCase):
    def invoke(self, data, quality):
        with tempfile.TemporaryDirectory(prefix='public-fine-work-') as folder:
            path = Path(folder) / 'source.step'
            path.write_bytes(data)
            process = subprocess.run([os.environ['STEP_NATIVE_BINARY'], '--fixture-limits', '--quality', quality,
                                      str(path)], capture_output=True, timeout=30)
            self.assertEqual(data, path.read_bytes())
        return process, json.loads(process.stdout)

    def test_explicit_work_zone_retains_shape_and_default_refusal(self):
        data = thin_box_source()
        _, ordinary = self.invoke(data, ORDINARY)
        self.assertNotEqual('Accepted', ordinary['status'])
        self.assertIsNone(ordinary['model'])
        process, fine = self.invoke(data, FINE)
        self.assertEqual(0, process.returncode, process.stderr)
        self.assertEqual('Accepted', fine['status'])
        self.assertEqual('None', fine['repair'])
        self.assertEqual((6, 12, 8), tuple(fine['model'][k] for k in ('faces', 'edges', 'vertices')))
        self.assertAlmostEqual(0.3, fine['model']['material_volume']['value_mm3'], delta=1e-5)
        self.assertAlmostEqual(1200.05, fine['model']['area_mm2'], delta=1e-4)
        self.assertEqual(data.decode('ascii'), fine['source_inspection']['source']['input']['raw'])

    def test_geometry_and_query_budgets_are_separate_from_work_zone(self):
        _, ordinary = self.invoke(BOX.read_bytes(), ORDINARY)
        _, fine = self.invoke(BOX.read_bytes(), FINE)
        self.assertEqual(FINE, fine['quality_id'])
        work = {'initial_work_tolerance_mm', 'maximum_work_tolerance_mm'}
        for key, value in ordinary['quality'].items():
            if key not in work:
                self.assertEqual(value, fine['quality'][key], key)
        expected = dict(kind='rational', num='1', den='10000')
        for key in work:
            self.assertEqual(expected, fine['quality'][key])
        self.assertAlmostEqual(6000, fine['model']['material_volume']['value_mm3'], delta=1e-5)
