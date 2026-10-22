# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Public original-circle controls for phase cuts and shared source-use correspondence."""
from collections import Counter, defaultdict
from fractions import Fraction
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from .test_band_repair import FIXTURE


def replace_once(data, old, new):
    if data.count(old) != 1:
        raise AssertionError('Public ring fixture mutation must identify exactly one original record')
    return data.replace(old, new, 1)


def phase_source(point='3.,0.,7.', *, legal=False):
    data = replace_once(FIXTURE.read_bytes(), b"#103=CARTESIAN_POINT('',(0.,3.,7.))",
                        f"#103=CARTESIAN_POINT('',({point}))".encode('ascii'))
    return replace_once(data, b'#173=FACE_OUTER_BOUND', b'#173=FACE_BOUND') if legal else data


def shared_ring_source(*, same_cut=False):
    """Two adjacent cylindrical faces share one complete middle source circle.

    All three original circle vertices remain distinct source definitions. Both
    cylindrical faces request a cut on the middle circle, either at the same
    longitude or at two different longitudes. The top cap uses the third circle.
    """
    data = phase_source()
    data = replace_once(data, b"#22=CLOSED_SHELL('',(#180,#181,#182))",
                        b"#22=CLOSED_SHELL('',(#180,#181,#182,#183))")
    data = replace_once(data, b"#141=PLANE('',#107)", b"#141=PLANE('',#108)")
    data = replace_once(data, b"#151=ORIENTED_EDGE('',*,*,#131,.T.)",
                        b"#151=ORIENTED_EDGE('',*,*,#132,.T.)")
    point = '0.,3.,14.' if same_cut else '-3.,0.,14.'
    records = f"""#109=CARTESIAN_POINT('',(0.,0.,14.));
#108=AXIS2_PLACEMENT_3D('',#109,#104,#105);
#112=CIRCLE('',#108,3.);
#113=CARTESIAN_POINT('',({point}));
#122=VERTEX_POINT('',#113);
#132=EDGE_CURVE('',#122,#122,#112,.T.);
#155=ORIENTED_EDGE('',*,*,#132,.F.);
#156=ORIENTED_EDGE('',*,*,#131,.T.);
#164=EDGE_LOOP('',(#155));
#165=EDGE_LOOP('',(#156));
#174=FACE_OUTER_BOUND('',#164,.T.);
#175=FACE_OUTER_BOUND('',#165,.T.);
#183=ADVANCED_FACE('',(#174,#175),#142,.T.);
""".encode('ascii')
    at = data.rindex(b'ENDSEC;')
    return data[:at] + records + data[at:]


def cone_frustum_source(*, legal=False, different_cut=False, milliradian=False, reverse_axis=False):
    """A radius-3 cylinder and a radius-3-to-10 cone share one whole source ring.

    The cone has two complete circular source bounds and no source meridian.
    Source axial height is 7 mm and the semi-angle is pi/4 rad; its STEP V
    remains generatrix distance, distinct from the native cone's axial V.
    """
    data = shared_ring_source(same_cut=True)
    data = replace_once(data, b"#102=CARTESIAN_POINT('',(0.,3.,0.))",
                        b"#102=CARTESIAN_POINT('',(3.,0.,0.))")
    data = replace_once(data, b"#103=CARTESIAN_POINT('',(3.,0.,7.))",
                        b"#103=CARTESIAN_POINT('',(0.,3.,7.))")
    data = replace_once(data, b"#112=CIRCLE('',#108,3.)", b"#112=CIRCLE('',#108,10.)")
    point = '-10.,0.,14.' if different_cut else '0.,10.,14.'
    data = replace_once(data, b"#113=CARTESIAN_POINT('',(0.,3.,14.))",
                        f"#113=CARTESIAN_POINT('',({point}))".encode('ascii'))
    data = replace_once(data, b'#175=FACE_OUTER_BOUND', b'#175=FACE_BOUND')
    data = replace_once(data, b"#183=ADVANCED_FACE('',(#174,#175),#142,.T.)",
                        b"#183=ADVANCED_FACE('',(#174,#175),#143,.T.)")
    angle = '785.3981633974483' if milliradian else '0.7853981633974483'
    if reverse_axis:
        # Reversing the source axis and using a negative physical semi-angle
        # describes the same cone, based at the upper radius-10 ring. Neither
        # source circles nor their physical orientations need to be changed.
        record = ("#114=DIRECTION('',(0.,0.,-1.));\n"
                  "#115=AXIS2_PLACEMENT_3D('',#109,#114,#105);\n"
                  f"#143=CONICAL_SURFACE('',#115,10.,-{angle});\n").encode('ascii')
    else:
        record = f"#143=CONICAL_SURFACE('',#107,3.,{angle});\n".encode('ascii')
    at = data.rindex(b'ENDSEC;')
    data = data[:at] + record + data[at:]
    if milliradian:
        data = replace_once(data, b'SI_UNIT($,.RADIAN.)', b'SI_UNIT(.MILLI.,.RADIAN.)')
    return replace_once(data, b'#173=FACE_OUTER_BOUND', b'#173=FACE_BOUND') if legal else data


def near_endpoint_source():
    # An exact decimal Pythagorean triple, scaled to a large public circle:
    # R=(m*m+1)/10^10, x=2*m/10^10, y=(m*m-1)/10^10, m=10^8.
    # Its chord is 0.02 mm, beyond the existing 0.001 mm consistency allowance,
    # while its native full-circle parameter separation is below the kernel's
    # parameter-resolution guard. This is numerical evidence failure, not repair.
    radius, x, y = '1000000.0000000001', '0.02', '999999.9999999999'
    if Fraction(x) ** 2 + Fraction(y) ** 2 != Fraction(radius) ** 2:
        raise AssertionError('Public near-endpoint source vertex must lie exactly on its original source circle')
    data = phase_source(f'{x},{y},7.', legal=True)
    data = replace_once(data, b"#102=CARTESIAN_POINT('',(0.,3.,0.))",
                        f"#102=CARTESIAN_POINT('',(0.,{radius},0.))".encode('ascii'))
    for old in ("CIRCLE('',#106,3.)", "CIRCLE('',#107,3.)", "CYLINDRICAL_SURFACE('',#106,3.)"):
        data = replace_once(data, old.encode('ascii'), old.replace('3.)', radius + ')').encode('ascii'))
    return data


def recipe(data, *, shared=False, proof_operations=100000):
    faces = ['7:21:182:172:173']
    if shared:
        faces.append('7:21:183:174:175')
    result = ['--band-repair-source-sha256', hashlib.sha256(data).hexdigest(),
              '--max-band-repair-faces', str(len(faces)), '--max-band-bound-edits', str(2 * len(faces)),
              '--max-band-proof-operations', str(proof_operations)]
    for face in faces:
        result.extend(['--repair-cylindrical-band', face])
    return result


# The source identities are part of this public fixture, independently of which
# ring the target planner chooses to subdivide. Directions are in the retained
# native curve basis, with the original face/bound senses accounted for.
SINGLE_USES = {
    '150': ('180', '170', '160', '130', True),
    '151': ('181', '171', '161', '131', True),
    '152': ('182', '172', '162', '130', True),
    '154': ('182', '173', '163', '131', False),
}
SHARED_USES = {
    **SINGLE_USES,
    '151': ('181', '171', '161', '132', True),
    '155': ('183', '174', '164', '132', False),
    '156': ('183', '175', '165', '131', True),
}


class RingSegmentationTests(unittest.TestCase):
    def invoke(self, data, *flags):
        with tempfile.TemporaryDirectory(prefix='step-public-ring-') as folder:
            source = Path(folder) / 'public.step'
            source.write_bytes(data)
            process = subprocess.run(
                [os.environ['STEP_NATIVE_BINARY'], '--fixture-limits', '--quality',
                 'Q-AP214-development-mm-01', str(source), *flags], capture_output=True, timeout=45)
            self.assertEqual(data, source.read_bytes())
        return process, json.loads(process.stdout) if process.stdout else None

    def assert_original(self, data, result):
        self.assertEqual(hashlib.sha256(data).hexdigest(), result['source_sha256'])
        self.assertEqual(data.decode('ascii'), result['source_inspection']['source']['input']['raw'])
        self.assertFalse(result['full_schema_conformance'])

    def assert_success(self, data, process, result, *, repaired=True, faces=3, edges=4, vertices=3,
                       area=60 * math.pi, volume=63 * math.pi):
        self.assertEqual(12 if repaired else 0, process.returncode, process.stderr or process.stdout)
        self.assertEqual('AcceptedWithRepairs' if repaired else 'Accepted', result['status'])
        self.assertEqual('ExactOrientedCylindricalBand-0.1' if repaired else 'None', result['repair'])
        self.assert_original(data, result)
        model = result['model']
        self.assertEqual((faces, edges, vertices), (model['faces'], model['edges'], model['vertices']))
        self.assertAlmostEqual(area, model['area_mm2'], delta=area * 2e-4)
        self.assertAlmostEqual(volume, model['material_volume']['value_mm3'], delta=volume * 2e-4)
        if repaired:
            proof = result['cylindrical_band_repair']['evidence']
            self.assertEqual(hashlib.sha256(data).hexdigest(), proof['source_sha256'])
            self.assertTrue(proof['native_definitions_identical'])
            self.assertTrue(proof['working_tolerances_identical'])
            self.assertEqual('ExplicitRepairPending', result['source_inspection']['source_conformance'])

    def assert_coverage(self, intervals, original):
        end = original[0]
        self.assertTrue(intervals)
        for low, high in sorted(intervals):
            self.assertEqual(end, low, 'Exact binary64 child endpoints must meet without overlap or gaps')
            self.assertGreater(high, low)
            end = high
        self.assertEqual(original[1], end)

    def assert_trace(self, result, expected_uses=SINGLE_USES):
        info = result['topology_normalization']
        for key in ('completed', 'source_curve_intervals_covered', 'source_vertices_preserved'):
            self.assertTrue(info[key])
        self.assertEqual('OriginalNativeBasis', info['parameter_convention'])
        self.assertNotIn('source_use_segments', result, 'Incidence evidence belongs to owning topology evidence')
        for key in ('faces', 'edges', 'vertices'):
            self.assertEqual(result['model'][key], len(info[key]))
            self.assertEqual(list(range(len(info[key]))), [entry['target'] for entry in info[key]])
        edges = {edge['target']: edge for edge in info['edges']}
        bases = {entry['source_use']: entry['parameters']['native_edge_interval']
                 for entry in result['parameter_mappings'] if entry['kind'] == 'ConicRationalParameter'}
        children = defaultdict(list)
        for edge in info['edges']:
            if 'source_edge_id' in edge:
                self.assertNotIn('derived_from_face_id', edge)
                children[edge['source_edge_id']].append(edge)
            else:
                self.assertIn(edge['derived_from_face_id'], {'182', '183'})
        for source, pieces in children.items():
            self.assert_coverage([edge['basis_parameter_interval'] for edge in pieces], bases[source])
        split_sources = {source for source, pieces in children.items() if len(pieces) > 1}
        self.assertTrue(split_sources)
        expected_ids = {use for use, fields in expected_uses.items() if fields[3] in split_sources}
        traces = info['source_use_segments']
        self.assertEqual(expected_ids, {entry['source_oriented_edge'] for entry in traces})
        by_use, by_target = defaultdict(list), defaultdict(list)
        fields = {'source_face', 'source_bound', 'source_loop', 'source_oriented_edge', 'source_edge',
                  'forward_in_native_curve', 'target_edge', 'target_start_vertex', 'target_end_vertex',
                  'parameter_convention', 'basis_parameter_interval'}
        for entry in traces:
            self.assertEqual(fields, set(entry))
            source_use = entry['source_oriented_edge']
            expected = expected_uses[source_use]
            self.assertEqual(expected, (entry['source_face'], entry['source_bound'], entry['source_loop'],
                                        entry['source_edge'], entry['forward_in_native_curve']))
            self.assertIs(type(entry['forward_in_native_curve']), bool)
            self.assertEqual('OriginalNativeBasis', entry['parameter_convention'])
            self.assertIs(type(entry['target_edge']), int)
            native = edges[entry['target_edge']]
            self.assertEqual(entry['source_edge'], native['source_edge_id'])
            self.assertEqual(native['basis_parameter_interval'], entry['basis_parameter_interval'])
            for field in ('target_start_vertex', 'target_end_vertex'):
                self.assertIs(type(entry[field]), int)
                self.assertGreaterEqual(entry[field], 0)
                self.assertLess(entry[field], len(info['vertices']))
            self.assertNotEqual(entry['target_start_vertex'], entry['target_end_vertex'])
            by_use[source_use].append(entry)
            by_target[entry['target_edge']].append(entry)
        for source_use, uses in by_use.items():
            source = expected_uses[source_use][3]
            self.assertEqual(len(children[source]), len(uses))
            self.assert_coverage([use['basis_parameter_interval'] for use in uses], bases[source])
            # The cylindrical traversal may rotate a closed source use to start
            # at its chart cut. Its target cycle must still be complete and closed.
            for first, following in zip(uses, uses[1:] + uses[:1]):
                self.assertEqual(first['target_end_vertex'], following['target_start_vertex'])
        expected_targets = {edge['target'] for source in split_sources for edge in children[source]}
        self.assertEqual(expected_targets, set(by_target))
        for uses in by_target.values():
            self.assertEqual(2, len(uses), 'Both original incident faces must reference the same child edge')
            self.assertEqual(2, len({use['source_oriented_edge'] for use in uses}))
            first, second = uses
            ends_a = (first['target_start_vertex'], first['target_end_vertex'])
            ends_b = (second['target_start_vertex'], second['target_end_vertex'])
            self.assertEqual(ends_a if first['forward_in_native_curve'] == second['forward_in_native_curve']
                             else ends_a[::-1], ends_b)
        return info, children, traces

    def test_aligned_existing_path_does_not_invent_ring_segmentation_evidence(self):
        data = FIXTURE.read_bytes()
        process, result = self.invoke(data, *recipe(data))
        self.assert_success(data, process, result, edges=3, vertices=2)
        self.assertIsNone(result.get('topology_normalization'))
        self.assertNotIn('source_use_segments', result)

    def test_original_phase_vertices_and_source_uses_survive_a_quarter_turn_cut(self):
        data = phase_source()
        process, result = self.invoke(data, *recipe(data))
        self.assert_success(data, process, result)
        info, children, traces = self.assert_trace(result)
        self.assertEqual({'120', '121'}, {v['source_vertex_id'] for v in info['vertices'] if 'source_vertex_id' in v})
        derived = [v for v in info['vertices'] if 'source_vertex_id' not in v]
        self.assertEqual(1, len(derived))
        self.assertEqual(1, len(derived[0]['basis_edge_parameters']))
        self.assertEqual([1, 2], sorted(len(pieces) for pieces in children.values()))
        self.assertEqual(4, len(traces))

    def test_legal_source_bounds_need_only_target_segmentation_and_no_source_repair(self):
        data = phase_source(legal=True)
        process, result = self.invoke(data)
        self.assert_success(data, process, result, repaired=False)
        self.assert_trace(result)
        self.assertNotIn('cylindrical_band_repair', result)

    def test_non_knot_phase_and_serialized_bound_order_preserve_the_same_source_contract(self):
        non_knot = phase_source('1.8,2.4,7.')
        reversed_bounds = replace_once(phase_source(), b"ADVANCED_FACE('',(#172,#173),#142",
                                       b"ADVANCED_FACE('',(#173,#172),#142")
        for label, data in [('non-knot', non_knot), ('bound-order', reversed_bounds)]:
            with self.subTest(label=label):
                process, result = self.invoke(data, *recipe(data))
                self.assert_success(data, process, result)
                self.assert_trace(result)

    def test_two_distinct_cuts_on_one_shared_source_ring_update_both_neighbor_faces(self):
        data = shared_ring_source()
        process, result = self.invoke(data, *recipe(data, shared=True))
        self.assert_success(data, process, result, faces=4, edges=7, vertices=5,
                            area=102 * math.pi, volume=126 * math.pi)
        info, children, traces = self.assert_trace(result, SHARED_USES)
        self.assertEqual(3, len(children['131']))
        self.assertEqual(Counter({'154': 3, '156': 3}), Counter(use['source_oriented_edge'] for use in traces))
        derived = [v for v in info['vertices'] if 'source_vertex_id' not in v]
        self.assertEqual(2, len(derived))
        self.assertEqual(2, len({v['basis_edge_parameters'][0]['parameter'] for v in derived}))

    def test_identical_shared_cut_reuses_one_derived_vertex_and_two_source_children(self):
        data = shared_ring_source(same_cut=True)
        process, result = self.invoke(data, *recipe(data, shared=True))
        self.assert_success(data, process, result, faces=4, edges=6, vertices=4,
                            area=102 * math.pi, volume=126 * math.pi)
        info, children, traces = self.assert_trace(result, SHARED_USES)
        self.assertEqual(2, len(children['131']))
        self.assertEqual(Counter({'154': 2, '156': 2}), Counter(use['source_oriented_edge'] for use in traces))
        self.assertEqual(1, sum('source_vertex_id' not in vertex for vertex in info['vertices']))

    def test_two_ring_cone_and_cylinder_share_every_subedge_after_only_the_scoped_cylinder_repair(self):
        # Independent analytic truth: V=63*pi+7*pi*(9+30+100)/3;
        # A=(9+100+42)*pi+13*pi*sqrt(7**2+7**2). This numerical
        # mass screen is distinct from the unchanged geometric error budgets.
        for different_cut, milliradian in ((False, False), (True, False), (True, True)):
            with self.subTest(different_cut=different_cut, milliradian=milliradian):
                data = cone_frustum_source(different_cut=different_cut, milliradian=milliradian)
                process, result = self.invoke(data, *recipe(data))
                self.assert_success(data, process, result, faces=4, edges=7 if different_cut else 6,
                                    vertices=5 if different_cut else 4, area=(151 + 91 * math.sqrt(2)) * math.pi,
                                    volume=1162 * math.pi / 3)
                info, children, traces = self.assert_trace(result, SHARED_USES)
                self.assertEqual(3 if different_cut else 2, len(children['131']))
                count = 3 if different_cut else 2
                self.assertEqual(Counter({'154': count, '156': count}),
                                 Counter(use['source_oriented_edge'] for use in traces))
                self.assertEqual({'120', '121', '122'},
                                 {v['source_vertex_id'] for v in info['vertices'] if 'source_vertex_id' in v})
                actions = result['cylindrical_band_repair']['evidence']['actions']
                self.assertEqual(['182'], [action['face_id'] for action in actions])
                self.assertEqual({'pointer_reports': 0, 'level2_reports': 0}, result['validations'])
                cone = next(face for face in info['faces'] if face['source_face_id'] == '183')
                self.assertEqual('143', cone['source_surface_id'])

    def test_two_legal_source_ring_faces_need_no_source_repair_for_the_cone_cut(self):
        for different_cut in (False, True):
            with self.subTest(different_cut=different_cut):
                data = cone_frustum_source(legal=True, different_cut=different_cut)
                process, result = self.invoke(data)
                self.assert_success(data, process, result, repaired=False, faces=4, edges=7 if different_cut else 6,
                                    vertices=5 if different_cut else 4, area=(151 + 91 * math.sqrt(2)) * math.pi,
                                    volume=1162 * math.pi / 3)
                self.assert_trace(result, SHARED_USES)
                self.assertNotIn('cylindrical_band_repair', result)
                self.assertEqual({'pointer_reports': 0, 'level2_reports': 0}, result['validations'])
                self.assertEqual('MaterialSolid', result['source_inspection']['snapshot']['part']['source_shape_role'])

    def test_reversed_source_cone_axis_and_negative_semiangle_retain_the_same_physical_solid(self):
        data = cone_frustum_source(legal=True, reverse_axis=True)
        process, result = self.invoke(data)
        self.assert_success(data, process, result, repaired=False, faces=4, edges=6, vertices=4,
                            area=(151 + 91 * math.sqrt(2)) * math.pi, volume=1162 * math.pi / 3)
        self.assert_trace(result, SHARED_USES)
        self.assertNotIn('cylindrical_band_repair', result)
        self.assertEqual({'pointer_reports': 0, 'level2_reports': 0}, result['validations'])

    def test_aligned_two_ring_cone_retains_the_derived_meridian_without_subdividing_source_circles(self):
        data = replace_once(FIXTURE.read_bytes(), b'#173=FACE_OUTER_BOUND', b'#173=FACE_BOUND')
        data = replace_once(data, b"#142=CYLINDRICAL_SURFACE('',#106,3.)",
                            b"#142=CONICAL_SURFACE('',#106,3.,0.7853981633974483)")
        data = replace_once(data, b"#111=CIRCLE('',#107,3.)", b"#111=CIRCLE('',#107,10.)")
        data = replace_once(data, b"#103=CARTESIAN_POINT('',(0.,3.,7.))",
                            b"#103=CARTESIAN_POINT('',(0.,10.,7.))")
        process, result = self.invoke(data)
        self.assert_success(data, process, result, repaired=False, faces=3, edges=3, vertices=2,
                            area=(109 + 91 * math.sqrt(2)) * math.pi, volume=973 * math.pi / 3)
        self.assertNotIn('cylindrical_band_repair', result)
        self.assertEqual({'pointer_reports': 0, 'level2_reports': 0}, result['validations'])
        info = result['topology_normalization']
        for key in ('completed', 'source_curve_intervals_covered', 'source_vertices_preserved'):
            self.assertTrue(info[key])
        self.assertEqual('OriginalNativeBasis', info['parameter_convention'])
        for key, count in (('faces', 3), ('edges', 3), ('vertices', 2)):
            self.assertEqual(count, len(info[key]))
            self.assertEqual(list(range(count)), [entry['target'] for entry in info[key]])
        self.assertEqual({'120', '121'}, {v['source_vertex_id'] for v in info['vertices']})
        self.assertTrue(all(not v['basis_edge_parameters'] for v in info['vertices']))
        sources = [edge for edge in info['edges'] if 'source_edge_id' in edge]
        self.assertEqual({'130', '131'}, {edge['source_edge_id'] for edge in sources})
        bases = {entry['source_use']: entry['parameters']['native_edge_interval']
                 for entry in result['parameter_mappings'] if entry['kind'] == 'ConicRationalParameter'}
        for edge in sources:
            self.assertNotIn('derived_from_face_id', edge)
            self.assertEqual(bases[edge['source_edge_id']], edge['basis_parameter_interval'])
        meridians = [edge for edge in info['edges'] if 'derived_from_face_id' in edge]
        self.assertEqual(1, len(meridians))
        self.assertEqual('182', meridians[0]['derived_from_face_id'])
        self.assertNotIn('source_edge_id', meridians[0])
        self.assertFalse(info.get('source_use_segments', []))
        self.assertNotIn('source_use_segments', result)
        cone = next(face for face in info['faces'] if face['source_face_id'] == '182')
        self.assertEqual('142', cone['source_surface_id'])

    def test_source_units_scale_geometry_once_without_scaling_basis_parameters(self):
        data = replace_once(phase_source(), b'SI_UNIT(.MILLI.,.METRE.)', b'SI_UNIT(.CENTI.,.METRE.)')
        process, result = self.invoke(data, *recipe(data))
        self.assert_success(data, process, result, area=6000 * math.pi, volume=63000 * math.pi)
        self.assert_trace(result)
        scale = result['source_inspection']['snapshot']['units']['length_scale_mm']
        self.assertEqual(('10', '1'), (scale['num'], scale['den']))

    def test_exact_source_proof_and_its_budget_remain_required_before_target_allocation(self):
        wrong_radius = replace_once(phase_source(), b"CYLINDRICAL_SURFACE('',#106,3.)",
                                    b"CYLINDRICAL_SURFACE('',#106,3.001)")
        for data, flags in [(wrong_radius, recipe(wrong_radius)),
                            (phase_source(), recipe(phase_source(), proof_operations=1))]:
            with self.subTest(flags=flags):
                process, result = self.invoke(data, *flags)
                self.assertNotEqual(12, process.returncode)
                self.assertIsNone(result['model'])
                self.assertEqual(0, result['native_calls'])

    def test_native_and_required_report_budgets_never_publish_partial_segmentation(self):
        data = phase_source()
        _, complete = self.invoke(data, *recipe(data))
        for limit in (1, complete['native_calls'] - 1):
            with self.subTest(native_calls=limit):
                process, result = self.invoke(data, *recipe(data), '--max-native-calls', str(limit))
                self.assertEqual(3, process.returncode)
                self.assertEqual('ResourceLimit', result['status'])
                self.assertIsNone(result['model'])
                self.assertLessEqual(result['native_calls'], limit)
        process, result = self.invoke(data, *recipe(data), '--max-report-bytes', '1')
        self.assertEqual(5, process.returncode)
        self.assertIsNone(result)
        self.assertEqual(b'', process.stdout)

    def test_unqualified_periodic_restriction_and_orientation_combinations_remain_blocking(self):
        data = phase_source(legal=True)
        orientation = ['--repair-source-sha256', hashlib.sha256(data).hexdigest(),
                       '--max-face-orientation-edits', '1', '--max-repair-proof-controls', '100000',
                       '--max-repair-proof-products', '100000', '--repair-face-orientation', '7:21:182:T:F']
        for flags in (['--normalize-periodic-faces'], ['--restrict-unused-support-domains'], orientation):
            with self.subTest(flags=flags):
                process, result = self.invoke(data, *flags)
                self.assertEqual(2, process.returncode, process.stderr or process.stdout)
                self.assertEqual('Unsupported', result['status'])
                self.assertIsNone(result['model'])

    def test_a_new_ring_cut_does_not_silently_compose_with_a_spherical_pole(self):
        data = replace_once(phase_source(legal=True), b"#141=PLANE('',#107)",
                            b"#141=SPHERICAL_SURFACE('',#107,3.)")
        process, result = self.invoke(data)
        self.assertEqual(2, process.returncode, process.stderr or process.stdout)
        self.assertEqual('Unsupported', result['status'])
        self.assertIsNone(result['model'])
        self.assertIn('RingSubdivisionPoleCompositionUnestablished', [d['key'] for d in result['diagnostics']])

    def test_large_radius_near_endpoint_parameter_refuses_without_repairing_the_exact_source_vertex(self):
        data = near_endpoint_source()
        process, result = self.invoke(data)
        self.assertEqual(8, process.returncode, process.stderr or process.stdout)
        self.assertEqual('Indeterminate', result['status'])
        self.assertIsNone(result['model'])
        self.assert_original(data, result)
        self.assertEqual('None', result['repair'])
        self.assertNotIn('cylindrical_band_repair', result)
        self.assertIn('RingSubdivisionNearSourceEndpoint', [d['key'] for d in result['diagnostics']])
