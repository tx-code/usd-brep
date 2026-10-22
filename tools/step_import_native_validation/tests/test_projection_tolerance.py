# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Analytic split-circle controls; projection retries cannot enlarge import budgets."""
import math
import re
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_import_native_validation.tests import test_general


def split_circle_cylinder(first=0.2, second=1.1):
    """Radius 3, height 7; split both circular rims without changing the solid.

    The side still uses one seam at the first endpoint. All surfaces and curves
    are exact analytic definitions; native curve parameters need not be angles.
    This fixture is independent of private CAD inputs and importer output.
    """
    data = test_general.cylinder_source()
    data = data.replace(b"#130=EDGE_CURVE('',#120,#120,#110,.T.);", b"#130=EDGE_CURVE('',#120,#122,#110,.T.);")
    data = data.replace(b"#131=EDGE_CURVE('',#121,#121,#111,.T.);", b"#131=EDGE_CURVE('',#121,#123,#111,.T.);")
    data = data.replace(b"#160=EDGE_LOOP('',(#150));", b"#160=EDGE_LOOP('',(#150,#250));")
    data = data.replace(b"#161=EDGE_LOOP('',(#151));", b"#161=EDGE_LOOP('',(#151,#251));")
    data = data.replace(b"(#152,#153,#154,#155)", b"(#152,#252,#153,#254,#154,#155)")
    extra = b"""#124=CARTESIAN_POINT('',(0.,-3.,0.));
#125=CARTESIAN_POINT('',(0.,-3.,7.));
#122=VERTEX_POINT('',#124);
#123=VERTEX_POINT('',#125);
#134=EDGE_CURVE('',#122,#120,#110,.T.);
#135=EDGE_CURVE('',#123,#121,#111,.T.);
#250=ORIENTED_EDGE('',*,*,#134,.F.);
#251=ORIENTED_EDGE('',*,*,#135,.T.);
#252=ORIENTED_EDGE('',*,*,#134,.T.);
#254=ORIENTED_EDGE('',*,*,#135,.F.);
"""
    data = data.replace(b'ENDSEC;\nEND-ISO', extra + b'ENDSEC;\nEND-ISO')
    for identifier, angle, z in ((102, first, 0), (103, first, 7), (124, second, 0), (125, second, 7)):
        old = f"#{identifier}=CARTESIAN_POINT('',(0.,{'3.' if identifier < 124 else '-3.'},{z}.));".encode()
        new = f"#{identifier}=CARTESIAN_POINT('',({3*math.cos(angle):.17E},{3*math.sin(angle):.17E},{z}.));".encode()
        assert data.count(old) == 1
        data = data.replace(old, new)
    return data


def split_spline_cylinder():
    """Turn the public rational cone control into a radius-3, height-7 cylinder.

    Only its upper ring poles are scaled by 1/2 and moved from z=4 to z=7.
    Rational weights still define exact circles; the seam becomes vertical.
    This synthetic transformation is independent of any private model.
    """
    path = Path(__file__).resolve().parents[3] / 'source/STEP_IMPORT/tests/fixtures/analytic_split_spline_cone.step'
    source = path.read_bytes()
    def point(match):
        x, y, z = (float(value) for value in match[2].split(b','))
        assert z in (0., 4.)
        if z == 4.:
            x, y, z = x/2, y/2, 7.
        return match[1] + f'{x:.17E},{y:.17E},{z:.17E}'.encode() + match[3]
    source = re.sub(rb"(CARTESIAN_POINT\('',\()([^)]*)(\)\))", point, source)
    source = source.replace(b"CONICAL_SURFACE('',#106,3.,0.6435011087932843868)",
                            b"CYLINDRICAL_SURFACE('',#106,3.)")
    source = re.sub(rb"#114=DIRECTION\([^;]+;", b"#114=DIRECTION('',(0.,0.,1.));", source)
    return source.replace(b"VECTOR('',#114,5.)", b"VECTOR('',#114,7.)")


class ProjectionToleranceTests(unittest.TestCase):
    invoke_bytes = test_general.GeneralTests.invoke_bytes

    def test_exact_rational_cone_has_independent_area_and_volume(self):
        # Public 3-4-5 frustum, radii 3/6 and height 4. Each circular rim is
        # split at .2/1.1 radians into quadratic rational B-splines: tangent
        # intersections are the middle poles and cos(half span) their weights.
        path = Path(__file__).resolve().parents[3] / 'source/STEP_IMPORT/tests/fixtures/analytic_split_spline_cone.step'
        source = path.read_bytes()
        run, report = self.invoke_bytes(source, '--integration-relative-accuracy', '1e-6')
        self.assertEqual(0, run.returncode, run.stderr or report['diagnostics'])
        self.assertEqual('Accepted', report['status'])
        self.assertEqual('None', report['repair'])
        self.assertEqual(source, report['source_inspection']['source']['input']['raw'].encode('latin1'))
        self.assertEqual((3, 5, 4), tuple(report['model'][k] for k in ['faces', 'edges', 'vertices']))
        self.assertAlmostEqual(84*math.pi, report['model']['material_volume']['value_mm3'], delta=.001)
        self.assertAlmostEqual(90*math.pi, report['model']['area_mm2'], delta=.001)
        self.assertTrue(any(n['kind'] == 'ControlledConeUvProjection' for n in report['representation_normalizations']))
        self.assertTrue(all(0 < t['value_mm'] <= .001 for t in report['measurements'] if t['stage'] == 'Postchecked'))

    def test_cone_source_gap_is_not_hidden_by_tighter_projection(self):
        path = Path(__file__).resolve().parents[3] / 'source/STEP_IMPORT/tests/fixtures/analytic_split_spline_cone.step'
        source = path.read_bytes().replace(b"CONICAL_SURFACE('',#106,3.,", b"CONICAL_SURFACE('',#106,3.002,")
        run, report = self.invoke_bytes(source)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report['model'])
        self.assertTrue(report['candidate_discarded'])

    def test_cone_copy_and_archive_preserve_definitions_uv_and_analytic_volume(self):
        source = Path(__file__).resolve().parents[3] / 'source/STEP_IMPORT/tests/fixtures/analytic_split_spline_cone.step'
        cli = Path(os.environ['STEP_NATIVE_BINARY'])
        binary = os.environ.get('STEP_NATIVE_GENERAL_BINARY',
                                str(cli.with_name('step_import_general_tests' + cli.suffix)))
        with tempfile.TemporaryDirectory(prefix='step-public-cone-copy-') as directory:
            output = Path(directory)/'result'
            run = subprocess.run([binary, '--development-file', str(source), str(output), 'normal',
                                  '--integration-relative-accuracy', '1e-6'], capture_output=True, timeout=30)
            self.assertEqual(0, run.returncode, run.stderr)
            checks = json.loads((output/'checks.json').read_bytes())
        self.assertEqual('Passed', checks['status'])
        self.assertTrue(checks['defining_coefficients_and_uv_checked'])
        for field in ['volume_mm3', 'copied_volume_mm3', 'reloaded_volume_mm3']:
            self.assertAlmostEqual(84*math.pi, checks[field], delta=.001)

    def test_axis_aligned_partition_has_independent_cylinder_truth(self):
        source = split_circle_cylinder(math.pi/2, 3*math.pi/2)
        run, report = self.invoke_bytes(source, "--integration-relative-accuracy", "1e-6")
        self.assertEqual(0, run.returncode, run.stderr or report)
        self.assertEqual((3, 5, 4), tuple(report['model'][key] for key in ['faces', 'edges', 'vertices']))
        self.assertAlmostEqual(63*math.pi, report['model']['material_volume']['value_mm3'], delta=1e-3)

    def test_retry_cannot_publish_an_excessive_work_tolerance(self):
        source = split_circle_cylinder()
        run, report = self.invoke_bytes(source, "--integration-relative-accuracy", "1e-6")
        self.assertEqual(source, report['source_inspection']['source']['input']['raw'].encode('latin1'))
        self.assertEqual('None', report['repair'])
        quality = report['quality']
        rational = lambda value: int(value['num'])/int(value['den'])
        limit = min(rational(quality['maximum_work_tolerance_mm']),
                    rational(quality['initial_work_tolerance_mm']) + rational(quality['tolerance_growth_mm']))
        observed = [item for item in report['measurements'] if item['stage'] == 'Postchecked']
        self.assertTrue(observed)
        self.assertEqual('Accepted', report['status'], report['diagnostics'])
        self.assertEqual(0, run.returncode)
        self.assertTrue(all(0 < item['value_mm'] <= limit for item in observed))
        self.assertAlmostEqual(63*math.pi, report['model']['material_volume']['value_mm3'], delta=1e-3)
        self.assertTrue(any(n['kind'] == 'ControlledCircleUvProjection' for n in report['representation_normalizations']))

    def test_true_source_gap_still_rejects_the_entire_candidate(self):
        source = split_circle_cylinder().replace(b"CYLINDRICAL_SURFACE('',#106,3.)",
                                                b"CYLINDRICAL_SURFACE('',#106,3.002)")
        run, report = self.invoke_bytes(source)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report['model'])
        self.assertTrue(report['candidate_discarded'])
        self.assertEqual(source, report['source_inspection']['source']['input']['raw'].encode('latin1'))


    def test_exact_spline_cylinder_preserves_geometry_and_work_budget(self):
        source = split_spline_cylinder()
        run, report = self.invoke_bytes(source, '--integration-relative-accuracy', '1e-6')
        self.assertEqual(0, run.returncode, report['diagnostics'])
        self.assertEqual('None', report['repair'])
        self.assertEqual(source, report['source_inspection']['source']['input']['raw'].encode('latin1'))
        self.assertEqual((3, 5, 4), tuple(report['model'][k] for k in ('faces','edges','vertices')))
        self.assertAlmostEqual(63*math.pi, report['model']['material_volume']['value_mm3'], delta=1e-3)
        self.assertAlmostEqual(60*math.pi, report['model']['area_mm2'], delta=1e-3)
        rational = lambda v: int(v['num'])/int(v['den'])
        quality = report['quality']
        limit = min(rational(quality['maximum_work_tolerance_mm']),
                    rational(quality['initial_work_tolerance_mm'])+rational(quality['tolerance_growth_mm']))
        observed = [x['value_mm'] for x in report['measurements'] if x['stage']=='Postchecked']
        self.assertTrue(observed)
        self.assertTrue(all(0 < value <= limit for value in observed))
        self.assertTrue(any(x['kind']=='ControlledCylinderSplineUvProjection' for x in report['representation_normalizations']))

    def test_spline_cylinder_with_source_gap_cannot_publish(self):
        source = split_spline_cylinder().replace(b"CYLINDRICAL_SURFACE('',#106,3.)", b"CYLINDRICAL_SURFACE('',#106,3.002)")
        run, report = self.invoke_bytes(source)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report['model'])
        self.assertTrue(report['candidate_discarded'])

    def test_spline_cylinder_copy_and_archive_preserve_defining_data_and_uv(self):
        cli = Path(os.environ['STEP_NATIVE_BINARY'])
        binary = os.environ.get('STEP_NATIVE_GENERAL_BINARY', str(cli.with_name('step_import_general_tests'+cli.suffix)))
        with tempfile.TemporaryDirectory(prefix='step-public-cylinder-copy-') as directory:
            source = Path(directory)/'source.step';source.write_bytes(split_spline_cylinder())
            output = Path(directory)/'result'
            run = subprocess.run([binary,'--development-file',str(source),str(output),'normal',
                                  '--integration-relative-accuracy','1e-6'],capture_output=True,timeout=30)
            self.assertEqual(0, run.returncode, run.stderr)
            checks = json.loads((output/'checks.json').read_bytes())
        self.assertEqual('Passed', checks['status'])
        self.assertTrue(checks['defining_coefficients_and_uv_checked'])
        for key in ('native_pointer_reports','native_level2_reports','reloaded_pointer_reports','reloaded_level2_reports'):
            self.assertEqual(0, checks[key])


    def test_cone_spline_interior_gap_is_rejected_even_with_exact_endpoints(self):
        # The short rational arc spans .2..1.1 radians at radius 3, z=0.
        # Move only its middle pole radially by 1%; both endpoint poles and
        # topological vertices remain exact. At t=.5 the radial departure is
        # .03/(1+cos(.45)); multiplying by cos(atan(3/4)) gives the cone's
        # normal distance, independently of either CAD kernel.
        path = Path(__file__).resolve().parents[3] / 'source/STEP_IMPORT/tests/fixtures/analytic_split_spline_cone.step'
        original = path.read_bytes()
        def move_middle(match):
            xyz = [float(value) for value in match[2].split(b',')]
            xyz[0] *= 1.01
            xyz[1] *= 1.01
            return match[1]+','.join(f'{v:.17E}' for v in xyz).encode()+match[3]
        source, count = re.subn(rb"(#801=CARTESIAN_POINT\('',\()([^)]*)(\)\))", move_middle, original)
        self.assertEqual(1, count)
        for identity in (b'800', b'802', b'102', b'124'):
            pattern = rb'#'+identity+rb'=[^;]+;'
            self.assertEqual(re.search(pattern, original)[0], re.search(pattern, source)[0])
        run, report = self.invoke_bytes(source)
        budget = report['quality']['derived_uv_mm']
        budget_mm = int(budget['num'])/int(budget['den'])
        independent_gap = .03/(1+math.cos(.45))*.8
        self.assertGreater(independent_gap, budget_mm)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report['model'])
        self.assertTrue(report['candidate_discarded'])
        self.assertEqual('None', report['repair'])
        self.assertEqual(source, report['source_inspection']['source']['input']['raw'].encode('latin1'))
        self.assertIn(report['diagnostics'][-1]['key'],
                      ('GeneralDerivedUvGap', 'GeneralFinalTrimGap', 'GeneralUnexpectedToleranceGrowth'))
