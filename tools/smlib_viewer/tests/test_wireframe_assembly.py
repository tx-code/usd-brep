# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Native wire definitions survive complete instance transport without BRep invention."""
import copy
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

os.environ.setdefault('PYVISTA_OFF_SCREEN', 'true')
import numpy as np

from tools.smlib_viewer.backend import native_worker, scene_file
from tools.smlib_viewer.assembly import read_assembly


FIXTURE = Path(__file__).resolve().parents[3] / 'source/STEP_SEMANTICS/tests/fixtures/wireframe_definition.step'


def repeated_wire_source():
    source = FIXTURE.read_bytes().replace(b"'parent',(#41),#61", b"'parent',(#41,#70),#61")
    records = b"""#70=AXIS2_PLACEMENT_3D('',#71,$,#72);
#71=CARTESIAN_POINT('',(10.,20.,30.));
#72=DIRECTION('',(0.,1.,0.));
#73=NEXT_ASSEMBLY_USAGE_OCCURRENCE('second-wire','second wire',$,#53,#7,$);
#74=PRODUCT_DEFINITION_SHAPE('',$,#73);
#75=CONTEXT_DEPENDENT_SHAPE_REPRESENTATION(#76,#74);
#76=(REPRESENTATION_RELATIONSHIP('',$,#20,#50) REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION(#77) SHAPE_REPRESENTATION_RELATIONSHIP());
#77=ITEM_DEFINED_TRANSFORMATION('',$,#40,#70);
"""
    return source.replace(b'ENDSEC;\nEND-ISO', records + b'ENDSEC;\nEND-ISO')


class WireframeAssemblyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.worker = native_worker()
        if not cls.worker.is_file():
            raise unittest.SkipTest('matching native worker required')
        cls.storage = tempfile.TemporaryDirectory(prefix='public-wire-assembly-')
        cls.addClassCleanup(cls.storage.cleanup)
        cls.root = Path(cls.storage.name)
        cls.source = cls.root / 'public.step'
        data = repeated_wire_source()
        cls.source.write_bytes(data)
        cls.output = cls.root / 'scene'
        cls.output.mkdir()
        run = subprocess.run([str(cls.worker), str(cls.source), str(cls.output), 'development', '0', '0', '1',
                              'DeclaredOnly', 'DeclaredOnly', '16', '0', '0', 'DeclaredOnly', '--assembly', '1'],
                             input=b'start\n', capture_output=True, timeout=60)
        if run.returncode:
            raise AssertionError(f'public wire scene refused: {run.stdout!r} {run.stderr!r}')
        if cls.source.read_bytes() != data:
            raise AssertionError('source bytes changed')

    def setUp(self):
        folder = tempfile.TemporaryDirectory(prefix='wire-assembly-read-')
        self.addCleanup(folder.cleanup)
        self.path = Path(folder.name)
        shutil.copytree(self.output, self.path, dirs_exist_ok=True)
        self.manifest = json.loads((self.path / 'scene.json').read_text(encoding='utf-8'))

    def read(self):
        return read_assembly(self.path, self.manifest, scene_file)

    def test_pure_wire_has_complete_instances_and_zero_brep_inventory(self):
        self.assertEqual('brep-view-scene.v7', self.manifest['format'])
        self.assertTrue(self.manifest['wireframe_required'])
        self.assertEqual(2, self.manifest['wireframe_instance_count'])
        for key in ('body_count', 'faces', 'edges', 'vertices', 'triangles', 'points'):
            self.assertEqual(0, self.manifest[key])
        mesh, boundary, topology = self.read()
        self.assertEqual((0, 0), (mesh.n_cells, boundary.n_cells))
        self.assertEqual((0, 0, 0, 0), tuple(len(getattr(topology, key)) for key in ('faces', 'edges', 'vertices', 'bodies')))
        wires = topology.wireframe
        self.assertEqual((4, 2), (len(wires.curves), len(wires.points)))
        self.assertEqual([1, 1, 2, 2], [curve.instance_index for curve in wires.curves])
        self.assertEqual(['30', '31', '30', '31'], [curve.source_id for curve in wires.curves])

    def test_independent_rotated_positions_trim_direction_and_native_lengths(self):
        _, _, topology = self.read()
        curves, points = topology.wireframe.curves, topology.wireframe.points
        np.testing.assert_allclose(curves[0].polyline_mm[0], (2, 0, 0), atol=1e-12, rtol=0)
        np.testing.assert_allclose(curves[0].polyline_mm[-1], (6, 0, 0), atol=1e-12, rtol=0)
        np.testing.assert_allclose(curves[2].polyline_mm[0], (10, 22, 30), atol=1e-12, rtol=0)
        np.testing.assert_allclose(curves[3].polyline_mm[0], (9.25, 21.5, 30), atol=1e-12, rtol=0)
        np.testing.assert_allclose(points[1].point_mm, (2, 27, 39), atol=1e-12, rtol=0)
        for index in (1, 3):
            self.assertEqual((2.75, 4.25), curves[index].parameter_domain)
            self.assertFalse(curves[index].source_forward_in_native)
            self.assertEqual((4.25, 2.75), (curves[index].parameters[0], curves[index].parameters[-1]))
            self.assertAlmostEqual((math.sqrt(2) + math.asinh(1)) / 2, curves[index].length_mm, places=5)
        for index in (0, 2):
            self.assertAlmostEqual(4, curves[index].length_mm, places=7)
        self.assertTrue(all(c.measurement_status == 'Available' for c in curves))

    def test_missing_members_wrong_ownership_and_parameter_drift_refuse(self):
        path = self.path / 'wireframes.json'
        original = json.loads(path.read_text(encoding='utf-8'))
        mutations = [lambda p: p['groups'].pop(),
                     lambda p: p['groups'].append(copy.deepcopy(p['groups'][0])),
                     lambda p: p['groups'][0].update(instance=0),
                     lambda p: p['groups'][0].update(role='MaterialSolid'),
                     lambda p: p['groups'][0]['curves'].pop(),
                     lambda p: p['groups'][0]['curves'][0].update(basis_id='999'),
                     lambda p: p['groups'][0]['curves'][0].update(native_domain=[0, 2]),
                     lambda p: p['groups'][0]['curves'][0].update(source_forward_in_native=False),
                     lambda p: p['groups'][0]['points'][0].update(point_mm=[700, 800, 900]),
                     lambda p: p.update(source_sha256='0' * 64)]
        for index, mutate in enumerate(mutations):
            value = copy.deepcopy(original)
            mutate(value)
            path.write_text(json.dumps(value), encoding='utf-8')
            with self.subTest(mutation=index), self.assertRaises(ValueError):
                self.read()

    def test_required_payload_and_unavailable_length_are_explicit(self):
        path = self.path / 'wireframes.json'
        value = json.loads(path.read_text(encoding='utf-8'))
        value['groups'][0]['curves'][0].update(length_mm=None, measurement_status='NativeQueryFailed')
        path.write_text(json.dumps(value), encoding='utf-8')
        _, _, topology = self.read()
        self.assertIsNone(topology.wireframe.curves[0].length_mm)
        path.unlink()
        with self.assertRaises((ValueError, FileNotFoundError)):
            self.read()

    def test_manifest_cannot_downgrade_a_wire_assembly_to_body_only(self):
        for updates in ({'format': 'brep-view-scene.v6'}, {'wireframe_required': False},
                        {'wireframe_instance_count': 1}, {'body_count': 1}):
            original = copy.deepcopy(self.manifest)
            self.manifest.update(updates)
            with self.assertRaises(ValueError):
                self.read()
            self.manifest = original


if __name__ == '__main__':
    unittest.main()
