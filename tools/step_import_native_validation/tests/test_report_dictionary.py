# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Lossless schema metadata factoring, independently expanded back to v1."""
import copy
import unittest
from tools.step_import_native_validation.tests import test_general
from tools.step_import_native_validation.tests.test_opaque_data import opaque_source
from tools.step_import_native_validation.tests.test_material_collection import two_material_boxes


def expanded_source(report):
    result = copy.deepcopy(report)
    if result['format_version'] != 'step-semantics.attributes.v2':
        raise AssertionError('unrecognized compact semantic encoding')
    result['format_version'] = 'step-semantics.v1'
    raw = result['source']
    if raw['format_version'] != 'step-source.spans.v2':
        raise AssertionError('unrecognized source span encoding')
    raw['format_version'] = 'step-source.v1'
    captured = raw['input']['raw']
    def fragment(node):
        start, end = node['span']
        if not 0 <= start <= end <= len(captured):
            raise AssertionError('source span outside capture')
        node['raw'] = captured[start:end]
    def value(node):
        fragment(node)
        for child in node['children']:
            value(child)
    def component(node):
        fragment(node)
        for parameter in node['parameters']:
            value(parameter)
    for node in raw['header'] or []:
        component(node)
    for node in raw['records'] or []:
        fragment(node)
        for item in node['components']:
            component(item)
    snapshot = result['snapshot']
    if snapshot is not None:
        definitions = snapshot.pop('attribute_descriptors')
        for record in snapshot['records']:
            for index, value in enumerate(record['attributes']):
                local = dict(value)
                descriptor = local.pop('descriptor')
                if type(descriptor) is not int or not 0 <= descriptor < len(definitions):
                    raise AssertionError('invalid descriptor identity')
                definition = definitions[descriptor]
                if set(local) & set(definition):
                    raise AssertionError('descriptor overwrites source identity/span')
                record['attributes'][index] = dict(definition) | local
    return result


class ReportDictionaryTests(unittest.TestCase):
    invoke_bytes = test_general.GeneralTests.invoke_bytes

    def test_every_report_field_round_trips_for_analytic_nested_and_interpreted_sources(self):
        cases = [(test_general.cylinder_source(), ()),
                 (test_general.bulged_box_source(), ()),
                 (two_material_boxes(), ()),
                 (opaque_source('representation'), ('--interop-nongeometric', '--assume-ascii-invariant-data')),
                 (b'not a STEP carrier', ())]
        for source, flags in cases:
            with self.subTest(case=source[:20]):
                original_run, original = self.invoke_bytes(source, *flags)
                compact_run, compact = self.invoke_bytes(source, *flags, '--compact-source-report')
                self.assertEqual(original_run.returncode, compact_run.returncode)
                self.assertEqual('CompactV2', compact.pop('source_report_encoding'))
                if compact['source_inspection'] is not None:
                    compact['source_inspection'] = expanded_source(compact['source_inspection'])
                self.assertEqual(original, compact)

    def test_encoding_is_deterministic_and_reuses_definitions_without_aliasing_spans(self):
        source = two_material_boxes()
        first_run, first = self.invoke_bytes(source, '--compact-source-report')
        second_run, second = self.invoke_bytes(source, '--compact-source-report')
        self.assertEqual(first_run.stdout, second_run.stdout)
        snapshot = first['source_inspection']['snapshot']
        uses = [a for r in snapshot['records'] for a in r['attributes']]
        self.assertLess(len(snapshot['attribute_descriptors']), len(uses))
        self.assertEqual(len(uses), len({tuple(a['span']) for a in uses}))

    def test_same_capacity_can_hold_compact_report_without_changing_acceptance(self):
        source = two_material_boxes()
        expanded_run, original = self.invoke_bytes(source)
        compact_run, compact = self.invoke_bytes(source, '--compact-source-report')
        self.assertLess(len(compact_run.stdout), len(expanded_run.stdout))
        limit = (len(compact_run.stdout) + len(expanded_run.stdout)) // 2
        failed_run, failed = self.invoke_bytes(source, '--max-report-bytes', str(limit))
        self.assertEqual(5, failed_run.returncode)
        self.assertIsNone(failed)
        run, result = self.invoke_bytes(source, '--max-report-bytes', str(limit), '--compact-source-report')
        self.assertEqual(compact_run.returncode, run.returncode)
        self.assertEqual(original['status'], result['status'])
        self.assertEqual(original['quality'], result['quality'])
        self.assertEqual(0, result['native_calls'])
        self.assertIsNone(result['model'])
        run, result = self.invoke_bytes(source, '--max-report-bytes', '512', '--compact-source-report')
        self.assertEqual(5, run.returncode)
        self.assertIsNone(result)
