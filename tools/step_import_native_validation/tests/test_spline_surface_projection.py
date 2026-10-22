# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Exact rational cylindrical support tests derived UV without analytic shortcuts."""
import json
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tools.step_import_native_validation.tests.test_projection_tolerance import split_spline_cylinder, split_circle_cylinder
from tools.step_import_native_validation.tests import test_general


def rational_surface_cylinder(radius_scale=1., analytic_edges=False):
    source = split_circle_cylinder(0., .9) if analytic_edges else split_spline_cylinder()
    # Rotate the public boundary so the repeated meridian coincides with the
    # exact rational surface's longitude cut; no private geometry is involved.
    c, s = (1., 0.) if analytic_edges else (math.cos(-.2), math.sin(-.2))

    def rotate(match):
        x, y, z = [float(v) for v in match[2].split(b',')]
        return match[1] + f'{c*x-s*y:.17E},{s*x+c*y:.17E},{z:.17E}'.encode() + match[3]

    source = re.sub(rb"((?:CARTESIAN_POINT|DIRECTION)\('[^']*',\()([^)]*)(\)\))", rotate, source)
    xy = ((3, 0), (3, 3), (0, 3), (-3, 3), (-3, 0), (-3, -3), (0, -3), (3, -3), (3, 0))
    rows, weights, points = [], [], []
    for i, (x, y) in enumerate(xy):
        rows.append(f'(#{1000+2*i},#{1001+2*i})')
        weight = 1. if i % 2 == 0 else math.sqrt(.5)
        weights.append(f'({weight:.17E},{weight:.17E})')
        for j, z in enumerate((0, 7)):
            points.append(f"#{1000+2*i+j}=CARTESIAN_POINT('',({x*radius_scale:.17E},{y*radius_scale:.17E},{z}.));\n")
    surface = ("(BOUNDED_SURFACE() B_SPLINE_SURFACE(2,1,("+','.join(rows)+"),.UNSPECIFIED.,.T.,.F.,.F.) "
               "B_SPLINE_SURFACE_WITH_KNOTS((3,2,2,2,3),(2,2),(0.,1.,2.,3.,4.),(0.,1.),.UNSPECIFIED.) "
               "GEOMETRIC_REPRESENTATION_ITEM() RATIONAL_B_SPLINE_SURFACE(("+','.join(weights)+")) "
               "REPRESENTATION_ITEM('exact rational cylindrical support') SURFACE())")
    assert source.count(b"CYLINDRICAL_SURFACE('',#106,3.)") == 1
    source = source.replace(b"CYLINDRICAL_SURFACE('',#106,3.)", surface.encode())
    return source.replace(b'ENDSEC;\nEND-ISO', ''.join(points).encode() + b'ENDSEC;\nEND-ISO')


class SplineSurfaceProjectionTests(unittest.TestCase):
    invoke_bytes = test_general.GeneralTests.invoke_bytes

    def test_exact_rational_support_keeps_geometry_and_tolerance_budget(self):
        source = rational_surface_cylinder()
        run, report = self.invoke_bytes(source, '--integration-relative-accuracy', '1e-6')
        self.assertEqual(0, run.returncode, report['diagnostics'])
        self.assertEqual('None', report['repair'])
        self.assertEqual(source, report['source_inspection']['source']['input']['raw'].encode('latin1'))
        self.assertEqual((3, 5, 4), tuple(report['model'][k] for k in ('faces', 'edges', 'vertices')))
        self.assertAlmostEqual(63*math.pi, report['model']['material_volume']['value_mm3'], delta=1e-3)
        self.assertAlmostEqual(60*math.pi, report['model']['area_mm2'], delta=1e-3)
        ratio = lambda value: int(value['num']) / int(value['den'])
        quality = report['quality']
        limit = min(ratio(quality['maximum_work_tolerance_mm']),
                    ratio(quality['initial_work_tolerance_mm']) + ratio(quality['tolerance_growth_mm']))
        values = [x['value_mm'] for x in report['measurements'] if x['stage'] == 'Postchecked']
        self.assertTrue(values)
        self.assertTrue(all(0 < value <= limit for value in values))
        self.assertTrue(any(x['kind'] == 'ControlledSplineSurfaceUvProjection'
                            for x in report['representation_normalizations']))

    def test_true_source_support_gap_still_discards_candidate(self):
        source = rational_surface_cylinder(1.001)
        run, report = self.invoke_bytes(source)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report['model'])
        self.assertTrue(report['candidate_discarded'])
        self.assertEqual('None', report['repair'])
        self.assertEqual(source, report['source_inspection']['source']['input']['raw'].encode('latin1'))

    def test_copy_and_archive_preserve_spline_coefficients_and_trim_uv(self):
        cli = Path(os.environ['STEP_NATIVE_BINARY'])
        binary = os.environ.get('STEP_NATIVE_GENERAL_BINARY', str(cli.with_name('step_import_general_tests'+cli.suffix)))
        with tempfile.TemporaryDirectory(prefix='step-rational-support-') as directory:
            path = Path(directory) / 'source.step'
            path.write_bytes(rational_surface_cylinder())
            output = Path(directory) / 'result'
            run = subprocess.run([binary, '--development-file', str(path), str(output), 'normal',
                                  '--integration-relative-accuracy', '1e-6'], capture_output=True, timeout=30)
            self.assertEqual(0, run.returncode, run.stderr)
            checks = json.loads((output/'checks.json').read_bytes())
        self.assertEqual('Passed', checks['status'])
        self.assertTrue(checks['defining_coefficients_and_uv_checked'])
        for key in ('native_pointer_reports', 'native_level2_reports', 'reloaded_pointer_reports', 'reloaded_level2_reports'):
            self.assertEqual(0, checks[key])

    def test_analytic_circle_edges_on_the_same_support_keep_the_same_truth(self):
        source = rational_surface_cylinder(analytic_edges=True)
        run, report = self.invoke_bytes(source, '--integration-relative-accuracy', '1e-6')
        self.assertEqual(0, run.returncode, report['diagnostics'])
        self.assertAlmostEqual(63*math.pi, report['model']['material_volume']['value_mm3'], delta=1e-3)
        self.assertAlmostEqual(60*math.pi, report['model']['area_mm2'], delta=1e-3)
        self.assertEqual(source, report['source_inspection']['source']['input']['raw'].encode('latin1'))
