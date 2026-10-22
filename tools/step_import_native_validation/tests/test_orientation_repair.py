# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""An explicit edit recipe never replaces the immutable source or ordinary admission."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[3]
VALID = REPO / 'source/STEP_IMPORT/tests/fixtures/conic_bounds_bulged_box.step'


def broken_roof():
    data = VALID.read_bytes()
    changed = data.replace(b"#517=ADVANCED_FACE('',(#516),#514,.T.);",
                           b"#517=ADVANCED_FACE('',(#516),#514,.F.);", 1)
    assert changed != data
    return changed


def recipe(data, old='F', new='T'):
    return ['--repair-source-sha256', hashlib.sha256(data).hexdigest(), '--max-face-orientation-edits', '1',
            '--max-repair-proof-controls', '10000', '--max-repair-proof-products', '1000000',
            '--repair-face-orientation', f'7:21:517:{old}:{new}']


class OrientationRepairContracts(unittest.TestCase):
    def invoke(self, data, flags):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'public.step'
            path.write_bytes(data)
            run = subprocess.run([os.environ['STEP_NATIVE_BINARY'], '--fixture-limits', '--quality',
                                  'Q-AP214-development-mm-01', '--max-native-calls', '100000', str(path), *flags],
                                 capture_output=True, timeout=60)
            self.assertEqual(data, path.read_bytes())
        return run, json.loads(run.stdout) if run.stdout else None

    def test_repaired_result_retains_actual_original_and_continuous_evidence(self):
        data = broken_roof()
        run, original = self.invoke(data, [])
        self.assertEqual(9, run.returncode)
        self.assertEqual('None', original['repair'])
        run, result = self.invoke(data, recipe(data))
        self.assertEqual(12, run.returncode, run.stderr or result)
        self.assertEqual('AcceptedWithRepairs', result['status'])
        self.assertEqual('FaceOrientationZeroGeometry-0.1', result['repair'])
        self.assertEqual(data.decode('ascii'), result['source_inspection']['source']['input']['raw'])
        self.assertEqual(hashlib.sha256(data).hexdigest(), result['source_sha256'])
        self.assertEqual('PostcheckFailure', result['original_attempt']['status'])
        self.assertTrue(result['original_attempt']['candidate_discarded'])
        proof = result['repair_evidence']
        self.assertTrue(proof['definitions_identical'])
        self.assertTrue(proof['working_tolerances_identical'])
        self.assertEqual((8, 12, 6), tuple(proof[k] for k in
                                         ('vertices_compared', 'edges_compared', 'surfaces_compared')))
        self.assertGreater(proof['actions'][0]['symmetric_determinant_lower'], 0)
        self.assertLessEqual(proof['actions'][0]['continuous_lift_bound_mm'], .001)
        self.assertGreater(result['repair_request']['proof_products_spent'], 0)
        self.assertIsNotNone(result['model'])

    def test_stale_plan_and_wrong_scope_do_not_blame_or_mutate_source(self):
        data = broken_roof()
        for flag, value in (('--repair-source-sha256', '0' * 64), ('--repair-face-orientation', '999:21:517:F:T')):
            flags = recipe(data)
            flags[flags.index(flag) + 1] = value
            run, result = self.invoke(data, flags)
            self.assertEqual(2, run.returncode)
            self.assertEqual('Unsupported', result['status'])
            self.assertEqual(0, result['native_calls'])
            self.assertIsNone(result['model'])

    def test_proof_capacity_and_mandatory_report_fail_closed(self):
        data = broken_roof()
        flags = recipe(data)
        flags[flags.index('--max-repair-proof-products') + 1] = '1'
        run, result = self.invoke(data, flags)
        self.assertEqual(3, run.returncode)
        self.assertEqual('ResourceLimit', result['status'])
        self.assertIsNone(result['model'])

    def test_no_repair_of_an_already_valid_source(self):
        data = VALID.read_bytes()
        run, result = self.invoke(data, recipe(data, 'T', 'F'))
        self.assertEqual(2, run.returncode)
        self.assertIsNone(result['model'])
        self.assertEqual('RepairNotRequired', result['diagnostics'][-1]['key'])
        self.assertEqual('Accepted', result['original_attempt']['status'])

    def test_source_assumption_is_independent_of_repaired_qualification(self):
        data = broken_roof().replace(b"'2;1'", b"'1'", 1)
        run, result = self.invoke(data, [*recipe(data), '--interop-nongeometric', '--interpret-level1-as-corrected'])
        self.assertEqual(12, run.returncode, run.stderr or result)
        self.assertEqual('AcceptedWithRepairs', result['status'])
        self.assertTrue(result['repair_evidence']['source_interpretation_applied'])
        self.assertFalse(result['source_carrier_verified'])

    def test_incomplete_or_duplicate_edit_plan_is_an_argument_error(self):
        data = broken_roof()
        for flags in ([*recipe(data), '--repair-face-orientation', '7:21:517:F:T'], recipe(data)[2:]):
            run, result = self.invoke(data, flags)
            self.assertEqual(2, run.returncode)
            self.assertIsNone(result)
