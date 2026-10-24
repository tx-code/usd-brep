# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""A named source vertex repair never grants arbitrary geometry/tolerance edits."""
import math
import re
import unittest

from . import test_band_repair as band

FIXTURE = band.FIXTURE
recipe = band.recipe


def displaced_source(two=False):
    data = FIXTURE.read_bytes().replace(b"#102=CARTESIAN_POINT('',(0.,3.,0.))",
                                       b"#102=CARTESIAN_POINT('',(0.,3.0001,0.))", 1)
    if two:
        data = data.replace(b"#103=CARTESIAN_POINT('',(0.,3.,7.))",
                            b"#103=CARTESIAN_POINT('',(0.,3.0001,7.))", 1)
    return data


def vertex_recipe(data, two=False):
    flags = [*recipe(data), '--max-band-circle-vertices', '2' if two else '1',
             '--max-band-vertex-move-mm', '0.001', '--max-band-total-vertex-move-mm', '0.001',
             '--project-band-circle-vertex', '7:21:120:110']
    if two:
        flags.extend(['--project-band-circle-vertex', '7:21:121:111'])
    return flags


class CircleVertexCorrectionTests(unittest.TestCase):
    invoke = band.BandRepairTests.invoke

    def test_complete_repaired_native_body_retains_source_and_distinct_displacement_evidence(self):
        data = displaced_source()
        process, result = self.invoke(data, vertex_recipe(data))
        self.assertEqual(12, process.returncode, process.stderr)
        self.assertEqual('AcceptedWithRepairs', result['status'])
        self.assertEqual('ExactOrientedCylindricalBandWithCircleVertices-0.1', result['repair'])
        repair = result['cylindrical_band_repair']
        evidence = repair['evidence']
        self.assertFalse(evidence['native_definitions_identical'])
        self.assertTrue(evidence['native_curves_and_surfaces_identical'])
        self.assertTrue(evidence['unrequested_vertices_identical'])
        self.assertTrue(evidence['working_tolerances_identical'])
        self.assertEqual('120', evidence['circle_vertices'][0]['vertex_id'])
        receipt = repair['circle_vertex_budget']
        self.assertTrue(receipt['request_complete'])
        self.assertEqual(1, receipt['corrected_vertices'])
        self.assertGreater(receipt['cumulative_displacement_upper_mm'], 0.0001)
        self.assertLess(receipt['cumulative_displacement_upper_mm'], 0.000101)
        self.assertEqual({'kind': 'rational', 'num': '1', 'den': '1000'}, receipt['maximum_displacement_mm'])
        self.assertEqual(data.decode('ascii'), result['source_inspection']['source']['input']['raw'])
        self.assertAlmostEqual(63 * math.pi, result['model']['material_volume']['value_mm3'], delta=1e-4)

    def test_per_vertex_and_cumulative_displacement_are_independent(self):
        data = displaced_source(two=True)
        for flag, value, key in (('--max-band-vertex-move-mm', '0.00001', 'CircleVertexDisplacementBudget'),
                                 ('--max-band-total-vertex-move-mm', '0.00015',
                                  'CircleVertexCumulativeMovementBudget')):
            flags = vertex_recipe(data, two=True)
            flags[flags.index(flag) + 1] = value
            process, result = self.invoke(data, flags)
            self.assertNotEqual(12, process.returncode)
            self.assertIsNone(result['model'])
            self.assertIn(key, process.stdout.decode('utf-8'))
        process, result = self.invoke(data, vertex_recipe(data, two=True))
        self.assertEqual(12, process.returncode, process.stderr)
        self.assertEqual(2, result['cylindrical_band_repair']['circle_vertex_budget']['corrected_vertices'])

    def test_wrong_circle_scope_duplicate_and_unnecessary_edits_never_publish(self):
        data = displaced_source()
        for edit in ('7:21:120:111', '7:21:121:111', '900:21:120:110'):
            flags = vertex_recipe(data)
            flags[flags.index('--project-band-circle-vertex') + 1] = edit
            _, result = self.invoke(data, flags)
            self.assertIsNone(result['model'])
        exact = FIXTURE.read_bytes()
        process, result = self.invoke(exact, vertex_recipe(exact))
        self.assertIsNone(result['model'])
        self.assertIn('CircleVertexProjectionNotRequired', process.stdout.decode('utf-8'))
        process, result = self.invoke(data, [*vertex_recipe(data), '--project-band-circle-vertex', '7:21:120:110'])
        self.assertEqual(2, process.returncode)
        self.assertIsNone(result)

    def test_thin_feature_protection_does_not_borrow_work_tolerance(self):
        data = displaced_source().replace(b"#101=CARTESIAN_POINT('',(0.,0.,7.))",
                                          b"#101=CARTESIAN_POINT('',(0.,0.,0.05))", 1)
        data = data.replace(b"#103=CARTESIAN_POINT('',(0.,3.,7.))",
                            b"#103=CARTESIAN_POINT('',(0.,3.,0.05))", 1)
        process, result = self.invoke(data, vertex_recipe(data))
        self.assertIsNone(result['model'])
        self.assertIn('CircleVertexLocalProtectionUnestablished', process.stdout.decode('utf-8'))

    def test_surface_interior_clearance_cannot_be_inferred_from_distant_edges(self):
        # A second closed cylinder has a large bottom disk 0.05 mm above the
        # repaired vertex. All its boundary curves/vertices are far away, so an
        # edge-only guard misses the nearby face interior. No valid material
        # claim is made for the overlapping pair; this is a protection refusal.
        geometry = FIXTURE.read_bytes().split(b'#100=', 1)[1].split(b'ENDSEC;', 1)[0]
        geometry = b'#100=' + geometry
        for old, new in ((b'(0.,0.,0.)', b'(0.,0.,0.05)'), (b'(0.,0.,7.)', b'(0.,0.,14.)'),
                         (b'(0.,3.,0.)', b'(0.,10.,0.05)'), (b'(0.,3.,7.)', b'(0.,10.,14.)'),
                         (b'#106,3.)', b'#106,10.)'), (b'#107,3.)', b'#107,10.)'),
                         (b'#173=FACE_OUTER_BOUND', b'#173=FACE_BOUND')):
            geometry = geometry.replace(old, new)
        geometry = re.sub(rb'#([0-9]+)', lambda m: b'#' + str(int(m[1]) + 1000).encode(), geometry)
        data = displaced_source().replace(b'#22=CLOSED_SHELL(\'\',(#180,#181,#182))',
                                           b'#22=CLOSED_SHELL(\'\',(#180,#181,#182,#1180,#1181,#1182))')
        data = data.replace(b'ENDSEC;\nEND-ISO', geometry + b'ENDSEC;\nEND-ISO', 1)
        process, result = self.invoke(data, vertex_recipe(data))
        self.assertIsNone(result['model'])
        self.assertIn('CircleVertexSurfaceProtectionUnestablished', process.stdout.decode('utf-8'))

    def test_proof_and_native_limits_still_discard_the_whole_candidate(self):
        data = displaced_source()
        for flag, value in (('--max-band-proof-operations', '1'), ('--max-native-calls', '20')):
            flags = vertex_recipe(data)
            if flag in flags:
                flags[flags.index(flag) + 1] = value
            else:
                flags.extend([flag, value])
            _, result = self.invoke(data, flags)
            self.assertEqual('ResourceLimit', result['status'])
            self.assertIsNone(result['model'])
