# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Original periodic-boundary source errors need a scoped repair and an exact region proof."""
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

FIXTURE = (Path(__file__).resolve().parents[3]
           / 'source/STEP_IMPORT/tests/fixtures/cylindrical_band_duplicate_outer.step')


def recipe(data):
    return ['--band-repair-source-sha256', hashlib.sha256(data).hexdigest(), '--max-band-repair-faces', '1',
            '--max-band-bound-edits', '2', '--max-band-proof-operations', '100000',
            '--repair-cylindrical-band', '7:21:182:172:173']


class BandRepairTests(unittest.TestCase):
    def invoke(self, data, flags=()):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'public.step'
            source.write_bytes(data)
            process = subprocess.run(
                [os.environ['STEP_NATIVE_BINARY'], '--fixture-limits', '--quality',
                 'Q-AP214-development-mm-01', str(source), *flags], capture_output=True, timeout=45)
            self.assertEqual(data, source.read_bytes())
        return process, json.loads(process.stdout) if process.stdout else None

    def test_exact_source_region_and_native_identity_preserve_original_rejected_input(self):
        data = FIXTURE.read_bytes()
        _, strict = self.invoke(data)
        self.assertEqual('Rejected', strict['status'])
        process, result = self.invoke(data, recipe(data))
        self.assertEqual(12, process.returncode, process.stderr)
        self.assertEqual('AcceptedWithRepairs', result['status'])
        self.assertEqual('ExactOrientedCylindricalBand-0.1', result['repair'])
        history = result['cylindrical_band_repair']['evidence']
        self.assertTrue(history['native_definitions_identical'])
        self.assertTrue(history['working_tolerances_identical'])
        self.assertEqual(['172', '173'], history['actions'][0]['height_ordered_bounds'])
        self.assertEqual('ExplicitRepairPending', result['source_inspection']['source_conformance'])
        self.assertEqual('Rejected', result['source_inspection']['explicit_source_repair']['original_source_verdict'])
        self.assertEqual(data.decode('ascii'), result['source_inspection']['source']['input']['raw'])
        self.assertAlmostEqual(63 * math.pi, result['model']['material_volume']['value_mm3'], delta=1e-4)

    def test_no_sampled_fallback_for_exact_geometry_conflicts_or_budget(self):
        data = FIXTURE.read_bytes()
        for old, new in ((b"CYLINDRICAL_SURFACE('',#106,3.)", b"CYLINDRICAL_SURFACE('',#106,3.001)"),
                         (b"#101=CARTESIAN_POINT('',(0.,0.,7.))", b"#101=CARTESIAN_POINT('',(0.000000000001,0.,7.))")):
            bad = data.replace(old, new, 1)
            process, result = self.invoke(bad, recipe(bad))
            self.assertNotEqual(12, process.returncode)
            self.assertIsNone(result['model'])
            self.assertEqual(0, result['native_calls'])
        flags = recipe(data)
        flags[flags.index('--max-band-proof-operations') + 1] = '1'
        _, result = self.invoke(data, flags)
        self.assertEqual('ResourceLimit', result['status'])
        self.assertIsNone(result['model'])

    def test_stale_wrong_scope_and_unnecessary_plan_do_not_publish(self):
        data = FIXTURE.read_bytes()
        for flag, value in (('--band-repair-source-sha256', '0' * 64),
                            ('--repair-cylindrical-band', '7:21:182:170:171')):
            flags = recipe(data)
            flags[flags.index(flag) + 1] = value
            _, result = self.invoke(data, flags)
            self.assertIsNone(result['model'])
            self.assertEqual(0, result['native_calls'])
        valid = data.replace(b'#173=FACE_OUTER_BOUND', b'#173=FACE_BOUND')
        _, result = self.invoke(valid, recipe(valid))
        self.assertEqual('Unsupported', result['status'])
        self.assertIsNone(result['model'])

    def test_bound_edits_and_native_budgets_are_explicit(self):
        data = FIXTURE.read_bytes()
        incomplete = recipe(data)
        incomplete[incomplete.index('--max-band-bound-edits') + 1] = '1'
        process, result = self.invoke(data, incomplete)
        self.assertEqual(2, process.returncode)
        self.assertIsNone(result)
        _, result = self.invoke(data, [*recipe(data), '--max-native-calls', '1'])
        self.assertEqual('ResourceLimit', result['status'])
        self.assertIsNone(result['model'])

    def test_unsupported_nested_edge_returns_a_structured_failure(self):
        data = FIXTURE.read_bytes().replace(b"#152=ORIENTED_EDGE('',*,*,#130,.T.)",
                                          b"#152=ORIENTED_EDGE('',*,*,#150,.T.)", 1)
        process, result = self.invoke(data, recipe(data))
        self.assertNotEqual(2, process.returncode, process.stderr)
        self.assertEqual('Indeterminate', result['status'])
        self.assertEqual(0, result['native_calls'])
        self.assertIn('CylindricalBandWholeCircleRequired', process.stdout.decode('utf-8'))
