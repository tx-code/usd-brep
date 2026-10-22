# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Facet-name interpretation preserves nonconformance and every later source check."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

FIXTURE = Path(__file__).resolve().parents[3] / 'source/STEP_IMPORT/tests/fixtures/conic_bounds_bulged_box.step'
CANONICAL = b'LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.)'
REVERSED = b'SI_UNIT(.MILLI.,.METRE.) NAMED_UNIT(*) LENGTH_UNIT()'


def reordered_unit():
    data = FIXTURE.read_bytes()
    assert CANONICAL in data
    return data.replace(CANONICAL, REVERSED, 1)


class ComplexMappingTests(unittest.TestCase):
    def invoke(self, data, *, interpret=True, extra=(), semantic=False):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'public.step'
            source.write_bytes(data)
            args = ([os.environ['STEP_SEMANTIC_BINARY'], '--fixture-limits', '--general-brep-source'] if semantic else
                    [os.environ['STEP_NATIVE_BINARY'], '--fixture-limits', '--quality', 'Q-AP214-development-mm-01'])
            args += ['--interop-nongeometric', str(source)]
            if interpret:
                args += ['--complex-mapping-interpretation', 'RegisteredFacetNames']
            process = subprocess.run([*args, *extra], capture_output=True, timeout=45)
            self.assertEqual(data, source.read_bytes())
        return process, json.loads(process.stdout) if process.stdout else None

    def test_raw_order_and_original_error_survive_complete_native_acceptance(self):
        data = reordered_unit()
        _, rejected = self.invoke(data, interpret=False)
        self.assertEqual('Rejected', rejected['status'])
        process, result = self.invoke(data)
        self.assertEqual(11, process.returncode, process.stderr)
        self.assertEqual('AcceptedUnderInterpretation', result['status'])
        source = result['source_inspection']
        mapping = source['complex_mapping_interpretation']
        self.assertEqual('RegisteredFacetNames', mapping['applied'])
        self.assertFalse(mapping['source_order_conforming'])
        self.assertEqual('ComplexConstituentOrderInvalid', mapping['original_diagnostics'][0]['key'])
        self.assertEqual([2, 1, 0], mapping['records'][0]['canonical_component_order'])
        self.assertEqual('ComplexMappingInterpreted', source['source_conformance'])
        self.assertEqual(data.decode('ascii'), source['source']['input']['raw'])
        self.assertIsNotNone(result['model'])

    def test_noop_and_semantic_only_scope_do_not_invent_native_or_source_conformance(self):
        process, noop = self.invoke(FIXTURE.read_bytes())
        self.assertEqual(0, process.returncode, process.stderr)
        self.assertEqual('DeclaredOnly', noop['source_inspection']['complex_mapping_interpretation']['applied'])
        process, source = self.invoke(reordered_unit(), semantic=True)
        self.assertIn(process.returncode, (0, 7), process.stderr)
        self.assertEqual('RegisteredFacetNames', source['complex_mapping_interpretation']['applied'])
        self.assertEqual('NotRun', source['source']['native_build'])

    def test_duplicate_missing_slots_and_physical_markers_are_not_recovered(self):
        data = reordered_unit()
        for replacement in (b'NAMED_UNIT(*) NAMED_UNIT(*)', b'', b'NAMED_UNIT($)'):
            invalid = data.replace(b'NAMED_UNIT(*)', replacement, 1)
            process, result = self.invoke(invalid)
            self.assertNotEqual(11, process.returncode)
            self.assertIsNone(result['model'])
            self.assertEqual(0, result['native_calls'])
            self.assertEqual([], result['source_inspection']['complex_mapping_interpretation']['records'])
        invalid = data.replace(b'SI_UNIT(.MILLI.,.METRE.)', b'SI_UNIT(.MILLI.,.METRE.,42)', 1)
        _, result = self.invoke(invalid)
        self.assertIsNone(result['model'])
        mapping = result['source_inspection']['complex_mapping_interpretation']
        self.assertEqual('DeclaredOnly', mapping['applied'])
        self.assertEqual('ComplexConstituentOrderInvalid', mapping['original_diagnostics'][0]['key'])

    def test_explicit_scope_budget_and_closed_option_values(self):
        _, limited = self.invoke(reordered_unit(), extra=('--max-preserved-diagnostics', '0'))
        self.assertEqual('ResourceLimit', limited['status'])
        self.assertIsNone(limited['model'])
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'public.step'
            source.write_bytes(reordered_unit())
            process = subprocess.run([os.environ['STEP_NATIVE_BINARY'], '--fixture-limits', '--quality',
                                      'Q-AP214-development-mm-01', str(source), '--complex-mapping-interpretation',
                                      'RegisteredFacetNames'], capture_output=True, timeout=30)
            self.assertEqual(2, process.returncode)
            self.assertFalse(process.stdout)
