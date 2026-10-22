# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit selected source admission retains local excluded errors and inverse obligations."""
import hashlib
from pathlib import Path
import unittest

from . import test_definition_selection as selection_tests
from tools.step_semantic_validation.tests.test_definition_inspection import break_face
from tools.step_semantic_validation.tests.test_product_structure import append_record

FIXTURE = (Path(__file__).resolve().parents[3]
           / 'source/STEP_IMPORT/tests/fixtures/independent_distinct_parts.step')
CONFIGURATION = 'SourceFaceClauseSelection-0.1'


def isolated(data, *ids):
    result = ['--selection-source-sha256', hashlib.sha256(data).hexdigest(), '--source-error-isolation',
              'FaceBoundsClause', '--max-isolated-source-failures', '4', '--max-source-influence-visits', '100000']
    for identity in ids or ('7',):
        result.extend(['--select-definition', identity])
    return result


def invalid_second():
    return break_face(FIXTURE.read_bytes(), second=True)


def presentation(data, targets=('507', '10507'), override=False):
    styles = []
    for index, target in enumerate(targets):
        number = 980001 + index
        item = f"#{number}=STYLED_ITEM('style',(#980010),#{target});\n"
        if override and index == 0:
            item = f"#{number}=OVER_RIDING_STYLED_ITEM('override',(#980010),#{target},#980002);\n"
        styles.append(item)
    records = ("#980000=MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('display',"
               f"({','.join('#'+str(980001+i) for i in range(1 if override else len(targets)))}),#12);\n"
               + ''.join(styles) + "#980010=PRESENTATION_STYLE_ASSIGNMENT((#980011));\n"
               "#980011=SURFACE_STYLE_USAGE(.BOTH.,#980012);\n"
               "#980012=SURFACE_SIDE_STYLE('side',(#980013));\n"
               "#980013=SURFACE_STYLE_FILL_AREA(#980014);\n"
               "#980014=FILL_AREA_STYLE('fill',(#980015));\n"
               "#980015=FILL_AREA_STYLE_COLOUR('colour',#980016);\n"
               "#980016=COLOUR_RGB('RGB',0.25,0.5,0.75);\n")
    return append_record(data, records.encode('ascii'))


class SourceIsolationTests(unittest.TestCase):
    invoke = selection_tests.DefinitionSelectionTests.invoke

    def assert_locality_refused(self, data):
        process, result = self.invoke(data, *isolated(data))
        self.assertNotEqual(13, process.returncode)
        self.assertEqual(0, result['native_calls'])
        source = result['source_inspection']
        self.assertTrue(any(d['key'].startswith('SourceIsolation') for d in source['diagnostics']),
                        source['diagnostics'])
        proof = source['source_selection']
        self.assertGreater(proof['influence_visits'], 0)
        self.assertFalse(proof['locality_established'])
        self.assertEqual(1, len(proof['isolated_failures']))

    def test_complete_selected_native_result_keeps_original_source_and_excluded_failure(self):
        data = invalid_second()
        process, result = self.invoke(data, *isolated(data))
        self.assertEqual(13, process.returncode, process.stderr or result['diagnostics'])
        self.assertEqual('AcceptedWithSourceIsolation', result['status'])
        document = result['product_document']
        self.assertTrue(document['complete'])
        self.assertEqual(1, document['selected_definition_count'])
        definition = document['definitions'][0]['checks']
        self.assertIsNotNone(definition['model'])
        self.assertAlmostEqual(6000, definition['model']['material_volume']['value_mm3'], delta=1e-6)
        self.assertEqual('AcceptedWithSourceIsolation', definition['status'])
        self.assertEqual(CONFIGURATION, result['definition_selection']['configuration'])
        self.assertEqual('SelectedDefinitionsOnly', result['definition_selection']['qualified']['source_scope'])
        source = result['source_inspection']
        self.assertEqual('SelectedDefinitionsOnly', source['source_conformance'])
        self.assertTrue(source['source_selection']['locality_established'])
        self.assertEqual(['902'], source['source_selection']['excluded_definitions'])
        diagnostic = source['source_selection']['isolated_failures'][0]['original_diagnostics'][0]
        self.assertEqual('10507', diagnostic['record_id'])
        self.assertEqual(1, len(source['document']['parts']))
        self.assertEqual(data.decode('ascii'), source['source']['input']['raw'])

    def test_selected_material_collection_keeps_complete_nested_native_reports(self):
        data = invalid_second()
        before = b"ADVANCED_BREP_SHAPE_REPRESENTATION('body',(#21),#12)"
        self.assertEqual(1, data.count(before))
        data = data.replace(before, b"ADVANCED_BREP_SHAPE_REPRESENTATION('body',(#21,#980030),#12)")
        data = append_record(data, b"#980030=MANIFOLD_SOLID_BREP('second source body',#22);")
        process, result = self.invoke(data, *isolated(data))
        self.assertEqual(13, process.returncode, process.stderr or result['diagnostics'])
        self.assertTrue(result['product_document']['complete'])
        collection = result['product_document']['definitions'][0]['checks']['material_collection']
        self.assertTrue(collection['complete'])
        self.assertEqual(2, collection['selected_body_count'])
        self.assertEqual(2, len(collection['bodies']))
        for body in collection['bodies']:
            self.assertTrue(body['checks_passed'])
            self.assertEqual('AcceptedWithSourceIsolation', body['checks']['status'])
            self.assertAlmostEqual(6000, body['checks']['model']['material_volume']['value_mm3'], delta=1e-6)

    def test_old_selection_and_selected_error_still_refuse(self):
        data = invalid_second()
        for flags in (selection_tests.DefinitionSelectionTests.selection(data, '7'), isolated(data, '902')):
            process, result = self.invoke(data, *flags)
            self.assertEqual(1, process.returncode)
            self.assertEqual(0, result['native_calls'])

    def test_source_isolation_requires_an_explicit_cli_policy(self):
        data = invalid_second()
        flags = isolated(data)
        position = flags.index('--source-error-isolation')
        del flags[position:position + 2]
        process, result = self.invoke(data, *flags)
        self.assertEqual(2, process.returncode)
        self.assertIsNone(result)

    def test_mixed_presentation_is_not_filtered_into_a_successful_subset(self):
        self.assert_locality_refused(presentation(invalid_second()))

    def test_override_ancestor_remains_in_the_complete_presentation(self):
        data = invalid_second().replace(b'AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}',
                                        b'AUTOMOTIVE_DESIGN {1 0 10303 214 1 1 1 1}')
        self.assert_locality_refused(presentation(data, ('10507', '507'), override=True))

    def test_inverse_excluded_only_presentation_needs_attribution_not_absence_of_forward_use(self):
        self.assert_locality_refused(presentation(invalid_second(), ('10507',)))

    def test_mixed_layer_and_administrative_assignments_are_complete_dependencies(self):
        data = invalid_second().replace(b'AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}',
                                        b'AUTOMOTIVE_DESIGN {1 0 10303 214 1 1 1 1}')
        layer = append_record(data, b"#980100=PRESENTATION_LAYER_ASSIGNMENT('layer','source',(#507,#10507));")
        self.assert_locality_refused(layer)
        admin = append_record(data, b"#980100=DATE_TIME_ROLE('creation_date');\n"
                              b"#980101=APPLIED_DATE_AND_TIME_ASSIGNMENT(#980102,#980100,(#7,#902));\n"
                              b"#980102=DATE_AND_TIME(#980103,#980104);\n"
                              b"#980103=CALENDAR_DATE(2024,29,2);\n"
                              b"#980104=LOCAL_TIME(12,30,17.,#980105);\n"
                              b"#980105=COORDINATED_UNIVERSAL_TIME_OFFSET(0,$,.BEHIND.);")
        self.assert_locality_refused(admin)

    def test_selected_unsupported_property_is_not_dropped_by_isolation(self):
        data = append_record(invalid_second(), b"#980020=PROPERTY_DEFINITION('property',$,#7);\n"
                             b"#980021=PROPERTY_DEFINITION_REPRESENTATION(#980020,#980022);\n"
                             b"#980022=REPRESENTATION('property',(#10507),#12);")
        process, result = self.invoke(data, *isolated(data))
        self.assertEqual(2, process.returncode)
        self.assertEqual(0, result['native_calls'])
        source = result['source_inspection']
        self.assertIn('UnsupportedPropertyItem', [x['key'] for x in source['diagnostics']])
        self.assertFalse(source['source_selection']['locality_established'])

    def test_an_additional_excluded_unit_failure_is_not_isolated_as_a_face_clause(self):
        data = invalid_second().replace(b"ADVANCED_BREP_SHAPE_REPRESENTATION('second',(#10021),#12)",
                                        b"ADVANCED_BREP_SHAPE_REPRESENTATION('second',(#10021),#990200)")
        data = append_record(data, b"#990200=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#10)) "
                             b"REPRESENTATION_CONTEXT('missing angle','3D'));")
        process, result = self.invoke(data, *isolated(data))
        self.assertEqual(2, process.returncode)
        self.assertEqual(0, result['native_calls'])
        proof = result['source_inspection']['source_selection']
        self.assertFalse(proof['locality_established'])
        self.assertEqual(0, proof['influence_visits'])

    def test_shared_context_is_not_an_anchor_for_an_unrelated_presentation(self):
        data = presentation(invalid_second(), ('10517',))
        data = append_record(data, b"#980020=PROPERTY_DEFINITION('property',$,#7);\n"
                             b"#980021=PROPERTY_DEFINITION_REPRESENTATION(#980020,#980022);\n"
                             b"#980022=REPRESENTATION('property',(#980023),#12);\n"
                             b"#980023=DESCRIPTIVE_REPRESENTATION_ITEM('value','text');")
        process, result = self.invoke(data, *isolated(data))
        self.assertEqual(13, process.returncode, process.stderr or result['diagnostics'])
        roots = result['source_inspection']['document']['ownership_roots']
        self.assertNotIn('980000', [r['source_id'] for r in roots])
