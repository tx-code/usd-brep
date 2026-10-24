# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Independent spherical belt truth; exact sphere definition, derived trim UV."""
import math
import re
import json
import os
import subprocess
import tempfile
from pathlib import Path
import unittest

from tools.step_import_native_validation.tests import test_general
from tools.step_import_native_validation.tests.spherical_cap_oracle import spherical_cap_truth


def spherical_belt_source(longitude=math.pi/2):
    # Sphere radius5 centered at z3, clipped by planes z0/z6. Rim radius4.
    # Volume = integral[-3,3] pi*(25-z*z) dz = 132*pi.
    # Curved area = 2*pi*5*6; both caps contribute 32*pi: total92*pi.
    data = test_general.cylinder_source().replace(b'7.', b'6.')
    data = data.replace(b'(0.,3.,0.)', b'(0.,4.,0.)').replace(b'(0.,3.,6.)', b'(0.,4.,6.)')
    data = data.replace(b"CIRCLE('',#106,3.)", b"CIRCLE('',#106,4.)").replace(b"CIRCLE('',#107,3.)", b"CIRCLE('',#107,4.)")
    data = data.replace(b"CYLINDRICAL_SURFACE('',#106,3.)", b"SPHERICAL_SURFACE('',#116,5.)")
    data = data.replace(b"#112=LINE('',#102,#113);", b"#112=CIRCLE('',#117,5.);")
    data = data.replace(b"#113=VECTOR('',#104,6.);\n", b'')
    extra = b"""#115=CARTESIAN_POINT('',(0.,0.,3.));
#116=AXIS2_PLACEMENT_3D('',#115,#104,#105);
#117=AXIS2_PLACEMENT_3D('',#115,#105,#118);
#118=DIRECTION('',(0.,1.,0.));
"""
    data = data.replace(b'ENDSEC;\nEND-ISO', extra+b'ENDSEC;\nEND-ISO')
    if longitude != math.pi/2:
        c, sn = math.cos(longitude), math.sin(longitude)
        for z in (0, 6):
            data = data.replace(f'(0.,4.,{z}.)'.encode(), f'({4*c:.17E},{4*sn:.17E},{z}.)'.encode())
        data = data.replace(b"#117=AXIS2_PLACEMENT_3D('',#115,#105,#118);",
                            b"#117=AXIS2_PLACEMENT_3D('',#115,#119,#118);")
        data = data.replace(b"#118=DIRECTION('',(0.,1.,0.));",
                            f"#118=DIRECTION('',({c:.17E},{sn:.17E},0.));\n#119=DIRECTION('',({sn:.17E},{-c:.17E},0.));".encode())
    return data


def spherical_cap_source(offset, positive, implicit=False):
    path = Path(__file__).resolve().parents[3]/'source/STEP_IMPORT/tests/fixtures/analytic_hemisphere_explicit_seam.step'
    original = path.read_bytes()
    # Radius5, section radius4, center at z3. Move only the
    # clipping plane/rim and choose the retained spherical side.
    data = original.replace(b"CIRCLE('',#106,5.)", b"CIRCLE('',#107,4.)")
    data = data.replace(b"PLANE('',#106)", b"PLANE('',#107)")
    data = data.replace(b"#102=CARTESIAN_POINT('',(0.,5.,3.));",
                        f"#102=CARTESIAN_POINT('',(0.,4.,{3+offset}.));".encode())
    extra = (f"#101=CARTESIAN_POINT('',(0.,0.,{3+offset}.));\n"
             "#107=AXIS2_PLACEMENT_3D('',#101,#104,#105);\n").encode()
    data = data.replace(b'ENDSEC;\nEND-ISO', extra+b'ENDSEC;\nEND-ISO')
    if not positive:
        data = data.replace(b"#103=CARTESIAN_POINT('',(0.,0.,8.));",
                            b"#103=CARTESIAN_POINT('',(0.,0.,-2.));")
        data = data.replace(b"#132=EDGE_CURVE('',#120,#121,#112,.T.);",
                            b"#132=EDGE_CURVE('',#120,#121,#112,.F.);")
        data = data.replace(b"#150=ORIENTED_EDGE('',*,*,#130,.F.);",
                            b"#150=ORIENTED_EDGE('',*,*,#130,.T.);")
        data = data.replace(b"#152=ORIENTED_EDGE('',*,*,#130,.T.);",
                            b"#152=ORIENTED_EDGE('',*,*,#130,.F.);")
        data = data.replace(b"#180=ADVANCED_FACE('',(#170),#140,.F.);",
                            b"#180=ADVANCED_FACE('',(#170),#140,.T.);")
    if implicit:
        data = re.sub(rb"^#(?:103|121|112|132|153|155|117|118)=[^\n]*\n", b'', data, flags=re.MULTILINE)
        data = data.replace(b"#162=EDGE_LOOP('',(#152,#153,#155));", b"#162=EDGE_LOOP('',(#152));")
    return data


class SphericalImportTests(unittest.TestCase):
    invoke_bytes = test_general.GeneralTests.invoke_bytes

    def test_spherical_belt_has_independent_area_and_volume(self):
        data = spherical_belt_source()
        run, report = self.invoke_bytes(data, '--integration-relative-accuracy', '1e-6')
        self.assertEqual(0, run.returncode, report['diagnostics'])
        self.assertEqual('None', report['repair'])
        self.assertEqual(data, report['source_inspection']['source']['input']['raw'].encode('latin1'))
        self.assertEqual((3, 3, 2), tuple(report['model'][k] for k in ('faces','edges','vertices')))
        self.assertAlmostEqual(132*math.pi, report['model']['material_volume']['value_mm3'], delta=1e-3)
        self.assertAlmostEqual(92*math.pi, report['model']['area_mm2'], delta=1e-3)


    def test_shifted_longitude_and_legacy_source_keep_spherical_truth(self):
        from tools.step_semantic_validation.tests.test_ap203_n916 import legacy_source
        for angle in (.2, -1.1):
            source = spherical_belt_source(angle)
            legacy = legacy_source().split(b'#22=',1)[0]+b'#22='+source.split(b'#22=',1)[1]
            for data in (source, legacy):
                with self.subTest(angle=angle, legacy=data is legacy):
                    run, report = self.invoke_bytes(data, '--integration-relative-accuracy','1e-6')
                    self.assertEqual(0, run.returncode, report['diagnostics'])
                    self.assertAlmostEqual(132*math.pi, report['model']['material_volume']['value_mm3'], delta=1e-3)
                    self.assertAlmostEqual(92*math.pi, report['model']['area_mm2'], delta=1e-3)
                    self.assertTrue(any(x['kind']=='SphericalLongitudeChart' for x in report['parameter_mappings']))

    def test_inconsistent_sphere_radius_cannot_publish(self):
        data = spherical_belt_source().replace(b"SPHERICAL_SURFACE('',#116,5.)",b"SPHERICAL_SURFACE('',#116,5.01)")
        run, report = self.invoke_bytes(data)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report['model'])
        self.assertTrue(report['candidate_discarded'])
        self.assertEqual('None', report['repair'])

    def test_spherical_copy_and_archive_preserve_defining_data_and_uv(self):
        cli = Path(os.environ['STEP_NATIVE_BINARY'])
        binary = os.environ.get('STEP_NATIVE_GENERAL_BINARY',str(cli.with_name('step_import_general_tests'+cli.suffix)))
        with tempfile.TemporaryDirectory(prefix='step-public-sphere-copy-') as directory:
            source = Path(directory)/'source.step'
            source.write_bytes(spherical_belt_source(.2))
            output = Path(directory)/'result'
            run = subprocess.run([binary,'--development-file',str(source),str(output),'normal',
                                  '--integration-relative-accuracy','1e-6'],capture_output=True,timeout=30)
            self.assertEqual(0, run.returncode, run.stderr)
            checks = json.loads((output/'checks.json').read_bytes())
        self.assertEqual('Passed',checks['status'])
        self.assertTrue(checks['defining_coefficients_and_uv_checked'])
        for key in ('native_pointer_reports','native_level2_reports','reloaded_pointer_reports','reloaded_level2_reports'):
            self.assertEqual(0,checks[key])


    def test_positive_radius_below_kernel_range_is_explicitly_unsupported(self):
        data = spherical_belt_source().replace(b"SPHERICAL_SURFACE('',#116,5.)",b"SPHERICAL_SURFACE('',#116,1.E-30)")
        run, report = self.invoke_bytes(data)
        self.assertNotEqual(0, run.returncode)
        self.assertEqual('Unsupported', report['status'])
        self.assertEqual('GeneralSphericalKernelRadiusRange',report['diagnostics'][-1]['key'])
        self.assertIsNone(report['model'])

    def test_implicit_pole_has_derived_identity_and_complete_publication(self):
        path = Path(__file__).resolve().parents[3]/'source/STEP_IMPORT/tests/fixtures/analytic_hemisphere.step'
        source = path.read_bytes()
        run, report = self.invoke_bytes(source, '--integration-relative-accuracy', '1e-8')
        self.assertEqual(0, run.returncode, report['diagnostics'])
        self.assertEqual('None', report['repair'])
        self.assertFalse(report['periodic_normalization_requested'])
        info = report['topology_normalization']
        self.assertTrue(info['completed'])
        self.assertEqual((2, 2, 2), tuple(report['model'][k] for k in ('faces','edges','vertices')))
        poles = [v for v in info['vertices'] if 'surface_pole' in v]
        self.assertEqual(1, len(poles))
        self.assertNotIn('source_vertex_id', poles[0])
        self.assertEqual([], poles[0]['basis_edge_parameters'])
        self.assertEqual({'source_face_id':'182', 'source_surface_id':'142', 'axis_sign':1}, poles[0]['surface_pole'])
        self.assertAlmostEqual(250*math.pi/3, report['model']['material_volume']['value_mm3'], delta=1e-3)
        self.assertAlmostEqual(75*math.pi, report['model']['area_mm2'], delta=1e-3)
        self.assertEqual(source, report['source_inspection']['source']['input']['raw'].encode('latin1'))


    def test_explicit_source_pole_uses_the_other_endpoint_for_chart_selection(self):
        path = Path(__file__).resolve().parents[3]/'source/STEP_IMPORT/tests/fixtures/analytic_hemisphere_explicit_seam.step'
        original = path.read_bytes()
        reversed_edge = original.replace(b"#132=EDGE_CURVE('',#120,#121,#112,.T.);",
                                         b"#132=EDGE_CURVE('',#121,#120,#112,.F.);")
        reversed_edge = reversed_edge.replace(b"#153=ORIENTED_EDGE('',*,*,#132,.T.);",
                                               b"#153=ORIENTED_EDGE('',*,*,#132,.F.);")
        reversed_edge = reversed_edge.replace(b"#155=ORIENTED_EDGE('',*,*,#132,.F.);",
                                               b"#155=ORIENTED_EDGE('',*,*,#132,.T.);")
        # Reverse the defining circle frame too, so increasing native curve
        # parameter starts at the pole rather than the equatorial endpoint.
        reversed_basis = original.replace(b"#117=AXIS2_PLACEMENT_3D('',#100,#105,#118);",
                                          b"#117=AXIS2_PLACEMENT_3D('',#100,#119,#118);")
        reversed_basis = reversed_basis.replace(b"#132=EDGE_CURVE('',#120,#121,#112,.T.);",
                                                b"#132=EDGE_CURVE('',#120,#121,#112,.F.);")
        reversed_basis = reversed_basis.replace(b'ENDSEC;\nEND-ISO',
                                                b"#119=DIRECTION('',(-1.,0.,0.));\nENDSEC;\nEND-ISO")
        self.assertEqual(3, len({original, reversed_edge, reversed_basis}))
        for variant, data in enumerate((original, reversed_edge, reversed_basis)):
            with self.subTest(variant=variant):
                run, report = self.invoke_bytes(data, '--integration-relative-accuracy', '1e-6')
                self.assertEqual(0, run.returncode, report['diagnostics'])
                self.assertEqual((2, 2, 2), tuple(report['model'][k] for k in ('faces', 'edges', 'vertices')))
                self.assertAlmostEqual(250*math.pi/3, report['model']['material_volume']['value_mm3'], delta=1e-3)
                self.assertAlmostEqual(75*math.pi, report['model']['area_mm2'], delta=1e-3)
                for actual, expected in zip(report['model']['bounds_mm'], ((-5, -5, 3), (5, 5, 8))):
                    for a, b in zip(actual, expected):
                        self.assertAlmostEqual(a, b, delta=1e-5)
                self.assertEqual('None', report['repair'])
                self.assertEqual(data, report['source_inspection']['source']['input']['raw'].encode('latin1'))


    def test_explicit_pole_copy_and_archive_preserve_source_definition_and_uv(self):
        source = Path(__file__).resolve().parents[3]/'source/STEP_IMPORT/tests/fixtures/analytic_hemisphere_explicit_seam.step'
        cli = Path(os.environ['STEP_NATIVE_BINARY'])
        binary = os.environ.get('STEP_NATIVE_GENERAL_BINARY', str(cli.with_name('step_import_general_tests'+cli.suffix)))
        with tempfile.TemporaryDirectory(prefix='step-explicit-pole-copy-') as directory:
            output = Path(directory)/'result'
            run = subprocess.run([binary, '--development-file', str(source), str(output), 'normal',
                                  '--integration-relative-accuracy', '1e-6'], capture_output=True, timeout=30)
            self.assertEqual(0, run.returncode, run.stderr)
            checks = json.loads((output/'checks.json').read_bytes())
        self.assertEqual('Passed', checks['status'])
        self.assertTrue(checks['defining_coefficients_and_uv_checked'])
        for key in ('native_pointer_reports', 'native_level2_reports', 'reloaded_pointer_reports', 'reloaded_level2_reports'):
            self.assertEqual(0, checks[key])


    def test_explicit_small_and_large_caps_select_the_correct_material_side(self):
        for offset in (-3, 3):
            for positive in (False, True):
                with self.subTest(offset=offset, positive=positive):
                    data = spherical_cap_source(offset, positive)
                    truth = spherical_cap_truth(5, offset, positive_side=positive)
                    # Pole-adjacent integration needs a tighter stopping target
                    # here; the geometric quality and oracle thresholds stay fixed.
                    run, report = self.invoke_bytes(data, '--integration-relative-accuracy', '1e-8')
                    self.assertEqual(0, run.returncode, report['diagnostics'])
                    self.assertAlmostEqual(float(truth['volume_over_pi_mm3'])*math.pi,
                                           report['model']['material_volume']['value_mm3'], delta=1e-3)
                    self.assertAlmostEqual(float(truth['area_over_pi_mm2'])*math.pi,
                                           report['model']['area_mm2'], delta=1e-3)
                    self.assertAlmostEqual(3+float(truth['centroid_axis_mm']),
                                           report['model']['centroid_mm'][2], delta=1e-4)
                    self.assertEqual(data, report['source_inspection']['source']['input']['raw'].encode('latin1'))


    def test_implicit_pole_copy_archive_and_cancellation_contracts(self):
        source = Path(__file__).resolve().parents[3]/'source/STEP_IMPORT/tests/fixtures/analytic_hemisphere.step'
        cli = Path(os.environ['STEP_NATIVE_BINARY'])
        binary = os.environ.get('STEP_NATIVE_GENERAL_BINARY', str(cli.with_name('step_import_general_tests'+cli.suffix)))
        with tempfile.TemporaryDirectory(prefix='step-implicit-pole-copy-') as directory:
            output = Path(directory)/'result'
            run = subprocess.run([binary, '--development-file', str(source), str(output), 'normal',
                                  '--integration-relative-accuracy', '1e-8'], capture_output=True, timeout=30)
            self.assertEqual(0, run.returncode, run.stderr)
            checks = json.loads((output/'checks.json').read_bytes())
        self.assertEqual('Passed', checks['status'])
        self.assertTrue(checks['defining_coefficients_and_uv_checked'])

    def test_unestablished_normalization_composition_does_not_publish(self):
        path = Path(__file__).resolve().parents[3]/'source/STEP_IMPORT/tests/fixtures/analytic_hemisphere.step'
        for option in ('--restrict-unused-support-domains',):
            run, report = self.invoke_bytes(path.read_bytes(), option)
            self.assertNotEqual(0, run.returncode)
            self.assertIsNone(report['model'])
            self.assertTrue(report['candidate_discarded'])
            self.assertEqual('SphericalPoleNormalizationCompositionUnestablished', report['diagnostics'][-1]['key'])


    def test_implicit_caps_keep_material_side_and_surface_derived_pole_identity(self):
        for offset in (-3, 3):
            for positive in (False, True):
                with self.subTest(offset=offset, positive=positive):
                    data = spherical_cap_source(offset, positive, implicit=True)
                    truth = spherical_cap_truth(5, offset, positive_side=positive)
                    run, report = self.invoke_bytes(data, '--integration-relative-accuracy', '1e-8')
                    self.assertEqual(0, run.returncode, report['diagnostics'])
                    self.assertAlmostEqual(float(truth['volume_over_pi_mm3'])*math.pi,
                                           report['model']['material_volume']['value_mm3'], delta=1e-3)
                    self.assertAlmostEqual(float(truth['area_over_pi_mm2'])*math.pi, report['model']['area_mm2'], delta=1e-3)
                    self.assertAlmostEqual(3+float(truth['centroid_axis_mm']), report['model']['centroid_mm'][2], delta=1e-4)
                    poles = [v for v in report['topology_normalization']['vertices'] if 'surface_pole' in v]
                    self.assertEqual(1, len(poles))
                    self.assertEqual(1 if positive else -1, poles[0]['surface_pole']['axis_sign'])
                    self.assertNotIn('source_vertex_id', poles[0])
                    self.assertEqual(data, report['source_inspection']['source']['input']['raw'].encode('latin1'))

    def test_implicit_pole_resource_and_bad_source_failures_do_not_publish(self):
        path = Path(__file__).resolve().parents[3]/'source/STEP_IMPORT/tests/fixtures/analytic_hemisphere.step'
        source = path.read_bytes()
        for data, flags in (
            (source, ('--max-native-calls', 0)),
            (source, ('--max-report-bytes', 512)),
            (source.replace(b"SPHERICAL_SURFACE('',#106,5.)", b"SPHERICAL_SURFACE('',#106,5.01)"), ()),
        ):
            with self.subTest(flags=flags):
                run, report = self.invoke_bytes(data, *flags)
                self.assertNotEqual(0, run.returncode)
                if flags == ('--max-report-bytes', 512):
                    self.assertEqual(5, run.returncode)  # Required bounded report unavailable; no publication.
                    self.assertIsNone(report)
                else:
                    self.assertIsNotNone(report)
                    self.assertIsNone(report.get('model'))


    def test_implicit_pole_is_invariant_under_rigid_frame_change(self):
        source = spherical_cap_source(3, False, implicit=True)
        def transform(match):
            x, y, z = [float(v) for v in match[3].split(b',')]
            point = (.8*x+.6*z, y, -.6*x+.8*z)
            if match[1] == b'CARTESIAN_POINT':
                point = tuple(a+b for a,b in zip(point,(7,-2,11)))
            return match[1]+match[2]+','.join(f'{v:.17E}' for v in point).encode()+match[4]
        data = re.sub(rb"(CARTESIAN_POINT|DIRECTION)(\('[^']*',\()([^)]*)(\)\))",transform,source)
        truth = spherical_cap_truth(5, 3, positive_side=False)
        run, report = self.invoke_bytes(data, '--integration-relative-accuracy', '1e-8')
        self.assertEqual(0,run.returncode,report['diagnostics'])
        self.assertAlmostEqual(float(truth['volume_over_pi_mm3'])*math.pi,
                               report['model']['material_volume']['value_mm3'],delta=1e-3)
        z = 3+float(truth['centroid_axis_mm'])
        for actual, expected in zip(report['model']['centroid_mm'],(7+.6*z,-2,11+.8*z)):
            self.assertAlmostEqual(actual,expected,delta=1e-4)
        self.assertEqual(data,report['source_inspection']['source']['input']['raw'].encode('latin1'))


    def test_each_implicit_pole_native_checkpoint_fails_without_publication(self):
        source = (Path(__file__).resolve().parents[3]/'source/STEP_IMPORT/tests/fixtures/analytic_hemisphere.step').read_bytes()
        baseline, report = self.invoke_bytes(source)
        self.assertEqual(0, baseline.returncode)
        checkpoints = report['native_calls']
        self.assertLess(checkpoints, 500, 'Keep this allocation/cleanup sweep bounded')
        for limit in range(checkpoints):
            with self.subTest(limit=limit):
                run, rejected = self.invoke_bytes(source, '--max-native-calls', limit)
                self.assertEqual(3, run.returncode)
                self.assertEqual('ResourceLimit', rejected['status'])
                self.assertIsNone(rejected['model'])
        accepted, _ = self.invoke_bytes(source, '--max-native-calls', checkpoints)
        self.assertEqual(0, accepted.returncode)


    def test_two_implicit_caps_form_a_complete_sphere_with_distinct_target_poles(self):
        path = Path(__file__).resolve().parents[3]/'source/STEP_IMPORT/tests/fixtures/analytic_hemisphere.step'
        source = path.read_bytes().replace(b"#140=PLANE('',#106);\n", b'')
        source = source.replace(b"#180=ADVANCED_FACE('',(#170),#140,.F.);",
                                b"#180=ADVANCED_FACE('',(#170),#142,.T.);")
        run, report = self.invoke_bytes(source, '--integration-relative-accuracy', '1e-8')
        self.assertEqual(0, run.returncode, report['diagnostics'])
        self.assertEqual((2,3,3),tuple(report['model'][k] for k in ('faces','edges','vertices')))
        self.assertAlmostEqual(500*math.pi/3, report['model']['material_volume']['value_mm3'], delta=1e-3)
        self.assertAlmostEqual(100*math.pi, report['model']['area_mm2'], delta=1e-3)
        poles = [v for v in report['topology_normalization']['vertices'] if 'surface_pole' in v]
        self.assertEqual({-1,1},{v['surface_pole']['axis_sign'] for v in poles})
        self.assertTrue(all('source_vertex_id' not in v for v in poles))
        self.assertEqual(1,sum('source_vertex_id' in v for v in report['topology_normalization']['vertices']))
        self.assertEqual(source,report['source_inspection']['source']['input']['raw'].encode('latin1'))


    def test_implicit_pole_respects_source_length_units(self):
        source = (Path(__file__).resolve().parents[3]/'source/STEP_IMPORT/tests/fixtures/analytic_hemisphere.step').read_bytes()
        def to_metres(match):
            point = [float(v)/1000 for v in match[2].split(b',')]
            return match[1]+','.join(f'{v:.17E}' for v in point).encode()+match[3]
        data = re.sub(rb"(CARTESIAN_POINT\('[^']*',\()([^)]*)(\)\))",to_metres,source)
        data = data.replace(b'SI_UNIT(.MILLI.,.METRE.)',b'SI_UNIT($,.METRE.)')
        data = data.replace(b"CIRCLE('',#106,5.)",b"CIRCLE('',#106,0.005)")
        data = data.replace(b"SPHERICAL_SURFACE('',#106,5.)",b"SPHERICAL_SURFACE('',#106,0.005)")
        run,report = self.invoke_bytes(data,'--integration-relative-accuracy','1e-8')
        self.assertEqual(0,run.returncode,report['diagnostics'])
        self.assertAlmostEqual(250*math.pi/3,report['model']['material_volume']['value_mm3'],delta=1e-3)
        self.assertAlmostEqual(75*math.pi,report['model']['area_mm2'],delta=1e-3)
        self.assertEqual(data,report['source_inspection']['source']['input']['raw'].encode('latin1'))


    def test_closed_surface_model_with_implicit_pole_is_not_promoted_to_solid(self):
        source = (Path(__file__).resolve().parents[3]/'source/STEP_IMPORT/tests/fixtures/analytic_hemisphere.step').read_bytes()
        data = source.replace(b'ADVANCED_BREP_SHAPE_REPRESENTATION', b'MANIFOLD_SURFACE_SHAPE_REPRESENTATION')
        data = data.replace(b"MANIFOLD_SOLID_BREP('',#22)", b"SHELL_BASED_SURFACE_MODEL('',(#22))")
        run, report = self.invoke_bytes(data)
        self.assertEqual(0, run.returncode, report['diagnostics'])
        self.assertEqual('NotApplicable', report['model']['material_volume'])
        self.assertEqual(0, report['native_role']['material_regions'])
        self.assertEqual(1, report['native_role']['bounded_void_regions'])
        self.assertEqual('NotApplicable', report['material_census']['applicability'])
        self.assertEqual({'pointer_reports': 0, 'level2_reports': 0}, report['validations'])


    def test_unused_periodic_request_preserves_existing_pole_origins(self):
        source = (Path(__file__).resolve().parents[3]/'source/STEP_IMPORT/tests/fixtures/analytic_hemisphere.step').read_bytes()
        plain, baseline = self.invoke_bytes(source, '--integration-relative-accuracy', '1e-8')
        run, report = self.invoke_bytes(source, '--integration-relative-accuracy', '1e-8', '--normalize-periodic-faces')
        self.assertEqual(0, plain.returncode)
        self.assertEqual(0, run.returncode, report['diagnostics'])
        self.assertTrue(report['periodic_normalization_requested'])
        self.assertEqual(baseline['topology_normalization'], report['topology_normalization'])
        self.assertEqual(baseline['representation_normalizations'], report['representation_normalizations'])
        self.assertEqual(baseline['model'], report['model'])
        self.assertTrue(report['topology_normalization']['completed'])
        self.assertEqual(source, report['source_inspection']['source']['input']['raw'].encode('latin1'))

    def test_unused_periodic_request_still_runs_copy_archive_and_cancellation(self):
        source = Path(__file__).resolve().parents[3]/'source/STEP_IMPORT/tests/fixtures/analytic_hemisphere.step'
        cli = Path(os.environ['STEP_NATIVE_BINARY'])
        binary = os.environ.get('STEP_NATIVE_GENERAL_BINARY', str(cli.with_name('step_import_general_tests'+cli.suffix)))
        with tempfile.TemporaryDirectory(prefix='step-pole-periodic-noop-') as directory:
            output = Path(directory)/'result'
            run = subprocess.run([binary, '--development-file', str(source), str(output), 'normal',
                                  '--integration-relative-accuracy', '1e-8', '--normalize-periodic-faces'],
                                 capture_output=True, timeout=30)
            self.assertEqual(0, run.returncode, run.stderr)
            checks = json.loads((output/'checks.json').read_bytes())
        self.assertEqual('Passed', checks['status'])
        self.assertTrue(checks['defining_coefficients_and_uv_checked'])
