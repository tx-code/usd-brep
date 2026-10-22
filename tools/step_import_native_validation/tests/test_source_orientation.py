# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Equivalent STEP orientations retain an independently defined curved solid."""
from itertools import product
import re
import unittest

from tools.step_import_native_validation.tests import test_integration as integration
from tools.step_import_native_validation.tests.test_general import bulged_box_source


def toggle(flag):
    return b'F' if flag == b'T' else b'T'


def oriented_roof(reverse_u=False, reverse_loops=False, reverse_edges=False, compensate_surface=True):
    data = bulged_box_source()
    if reverse_u:
        data = data.replace(b'((#600,#601,#602),(#603,#604,#605),(#606,#607,#608))',
                            b'((#606,#607,#608),(#603,#604,#605),(#600,#601,#602))')
        if compensate_surface:
            # u -> 1-u reverses the surface normal; same_sense must reverse
            # too to preserve the material side. Knots/units are unchanged.
            data = data.replace(b"#517=ADVANCED_FACE('',(#516),#514,.T.);",
                                b"#517=ADVANCED_FACE('',(#516),#514,.F.);")
    if reverse_edges:
        # Reversing the edge's topological endpoints and same_sense preserves
        # its 3D curve. Reverse every incidence to preserve loop traversal.
        data = re.sub(rb"(EDGE_CURVE\('[^']*',)(#\d+),(#\d+),(#\d+),\.([TF])\.",
                      lambda m: m[1]+m[3]+b','+m[2]+b','+m[4]+b',.'+toggle(m[5])+b'.', data)
    if reverse_loops:
        # Reverse the complete ordered loop and its bound sense together.
        data = re.sub(rb"(EDGE_LOOP\('[^']*',\()([^)]*)(\))",
                      lambda m: m[1]+b','.join(reversed(m[2].split(b',')))+m[3], data)
        data = re.sub(rb"(FACE_(?:OUTER_)?BOUND\('[^']*',#\d+,\.)([TF])(\.)",
                      lambda m:m[1]+toggle(m[2])+m[3], data)
    if reverse_edges != reverse_loops:
        data = re.sub(rb"(ORIENTED_EDGE\('[^']*',\*,\*,#\d+,\.)([TF])(\.)",
                      lambda m:m[1]+toggle(m[2])+m[3], data)
    return data


class SourceOrientationTests(unittest.TestCase):
    invoke = integration.NumericalIntegrationTests.invoke

    def test_equivalent_orientations_keep_curved_solid_and_source(self):
        # The roof is z=35+80u(1-u)v(1-v), above a 10x20x30 box.
        # Integrating its polynomial height gives V=6000+4000/9, without CAD.
        truth = 6000 + 4000 / 9
        variants = set()
        for u, loops, edges in product((False, True), repeat=3):
            with self.subTest(reverse_u=u, reverse_loops=loops, reverse_edges=edges):
                data = oriented_roof(u, loops, edges)
                variants.add(data)
                run, report = self.invoke(data, '--integration-relative-accuracy', '1e-6')
                self.assertEqual(0, run.returncode, report['diagnostics'])
                self.assertEqual((6, 12, 8), tuple(report['model'][k] for k in ('faces', 'edges', 'vertices')))
                self.assertAlmostEqual(truth, report['model']['material_volume']['value_mm3'], delta=truth*1e-6)
                self.assertEqual({'pointer_reports': 0, 'level2_reports': 0}, report['validations'])
                self.assertEqual(data.decode('ascii'), report['source_inspection']['source']['input']['raw'])
        self.assertEqual(8, len(variants), "Every requested orientation transform must change the source")

    def test_uncompensated_surface_normal_does_not_get_silently_repaired(self):
        data = oriented_roof(reverse_u=True, compensate_surface=False)
        run, report = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report['model'])
        self.assertEqual('GeneralPointerValidation', report['diagnostics'][-1]['key'])
        self.assertTrue(report['candidate_discarded'])
        self.assertEqual(data.decode('ascii'), report['source_inspection']['source']['input']['raw'])
