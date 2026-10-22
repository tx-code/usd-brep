# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Independent spherical belt truth; exact sphere definition, derived trim UV."""
import math
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

    def test_implicit_pole_remains_unestablished_without_partial_publication(self):
        # This legal analytic hemisphere requires a pole/seam representation
        # not yet qualified by this slice. Preserve its failure, not a mesh or
        # a partial candidate masquerading as spherical import support.
        path = Path(__file__).resolve().parents[3]/'source/STEP_IMPORT/tests/fixtures/analytic_hemisphere.step'
        source = path.read_bytes()
        run, report = self.invoke_bytes(source)
        self.assertNotEqual(0, run.returncode)
        self.assertEqual('GeneralFinalTrimGap',report['diagnostics'][-1]['key'])
        self.assertIsNone(report['model'])
        self.assertTrue(report['candidate_discarded'])
        self.assertEqual(source,report['source_inspection']['source']['input']['raw'].encode('latin1'))


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
        path = Path(__file__).resolve().parents[3]/'source/STEP_IMPORT/tests/fixtures/analytic_hemisphere_explicit_seam.step'
        original = path.read_bytes()
        for offset in (-3, 3):
            for positive in (False, True):
                with self.subTest(offset=offset, positive=positive):
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
