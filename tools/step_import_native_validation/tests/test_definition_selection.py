# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Native CLI selection keeps full source evidence and explicit native scope."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

FIXTURE = Path(__file__).resolve().parents[3] / 'source/STEP_IMPORT/tests/fixtures/independent_two_parts.step'


class DefinitionSelectionTests(unittest.TestCase):
    def invoke(self, data, *flags):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'public.step'
            source.write_bytes(data)
            process = subprocess.run([os.environ['STEP_NATIVE_BINARY'], '--fixture-limits', '--quality',
                                      'Q-AP214-development-mm-01', '--compact-source-report', str(source), *flags],
                                     capture_output=True, timeout=30)
            self.assertEqual(data, source.read_bytes())
        return process, json.loads(process.stdout) if process.stdout else None

    @staticmethod
    def selection(data, *ids):
        flags = ['--selection-source-sha256', hashlib.sha256(data).hexdigest()]
        for identity in ids:
            flags.extend(['--select-definition', identity])
        return flags

    def test_explicit_subset_is_complete_only_for_selected_native_definitions(self):
        data = FIXTURE.read_bytes().replace(b'SI_UNIT(.CENTI.,.METRE.)', b'SI_UNIT(.ATTO.,.METRE.)')
        failed, whole = self.invoke(data, '--independent-products')
        self.assertNotEqual(0, failed.returncode)
        self.assertFalse(whole['product_document']['complete'])
        process, selected = self.invoke(data, *self.selection(data, '7'))
        self.assertEqual(0, process.returncode, process.stderr)
        self.assertTrue(selected['product_document']['complete'])
        self.assertEqual('ExplicitSelectedProductDefinitions', selected['scope'])
        self.assertEqual(1, selected['product_document']['selected_definition_count'])
        definitions = selected['product_document']['definitions']
        self.assertEqual(['7'], [item['source_definition_id'] for item in definitions])
        self.assertAlmostEqual(6000, definitions[0]['checks']['model']['material_volume']['value_mm3'], delta=1e-6)
        receipt = selected['definition_selection']['qualified']
        self.assertEqual('AllDefinitionsChecked', receipt['source_scope'])
        self.assertEqual('ExplicitSelectedDefinitions', receipt['native_scope'])
        self.assertEqual(['902'], receipt['not_requested_definitions'])
        self.assertEqual(2, len(selected['source_inspection']['document']['parts']))
        self.assertEqual(data.decode('ascii'), selected['source_inspection']['source']['input']['raw'])

    def test_selection_is_source_ordered_and_cannot_bypass_source_unit_errors(self):
        data = FIXTURE.read_bytes()
        process, result = self.invoke(data, *self.selection(data, '902', '7'))
        self.assertEqual(0, process.returncode, process.stderr)
        self.assertEqual(['7', '902'], result['definition_selection']['qualified']['selected_definitions'])
        invalid = data.replace(b'SI_UNIT(.CENTI.,.METRE.)', b'SI_UNIT(.NOT_A_PREFIX.,.METRE.)')
        process, rejected = self.invoke(invalid, *self.selection(invalid, '7'))
        self.assertNotEqual(0, process.returncode)
        self.assertIsNone(rejected['definition_selection']['qualified'])
        self.assertEqual(0, rejected['native_calls'])

    def test_unknown_duplicate_stale_and_empty_requests_never_publish_native_models(self):
        data = FIXTURE.read_bytes()
        for flags in (self.selection(data, '900001'), self.selection(data, '7', '7'),
                      ['--selection-source-sha256', '0' * 64, '--select-definition', '7'], self.selection(data)):
            with self.subTest(flags=flags):
                process, result = self.invoke(data, *flags)
                self.assertEqual(2, process.returncode)
                if result:
                    self.assertEqual(0, result['native_calls'])
                    self.assertIsNone(result['definition_selection']['qualified'])
