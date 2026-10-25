# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Real native repeated-box transport, root coordinates and picking."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import Mock

os.environ.setdefault('PYVISTA_OFF_SCREEN', 'true')
import numpy as np
import pyvista as pv

from tools.smlib_viewer.assembly import arguments, read_assembly, validate_request
from tools.smlib_viewer.backend import ImportSettings, native_worker, scene_file
from tools.smlib_viewer.backend import SceneLoader
from PySide6 import QtCore, QtWidgets
from tools.smlib_viewer.picking import TopologyPicker


class AssemblyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        worker = native_worker()
        if not worker.is_file():
            raise unittest.SkipTest('matching native viewer worker required')
        cls.transport = tempfile.TemporaryDirectory(prefix='public-assembly-')
        cls.addClassCleanup(cls.transport.cleanup)
        cls.directory = Path(cls.transport.name)
        fixture = (Path(__file__).resolve().parents[3]
                   / 'source/STEP_SEMANTICS/tests/fixtures/assembly_repeated_box.step')
        run = subprocess.run([str(worker), str(fixture), str(cls.directory), 'development', '0', '0', '1',
                              'DeclaredOnly', 'DeclaredOnly', '16', '0', '0', 'DeclaredOnly', '--assembly', '1',
                              '--unreferenced-placement-policy', 'Reject'], input=b'start\n',
                             capture_output=True, timeout=60)
        if run.returncode:
            raise AssertionError(f'public assembly worker failed: {run.returncode}\n'
                                 f'{run.stdout!r} {run.stderr!r}')

    def setUp(self):
        folder = tempfile.TemporaryDirectory(prefix='assembly-scene-')
        self.addCleanup(folder.cleanup)
        self.path = Path(folder.name)
        shutil.copytree(self.directory, self.path, dirs_exist_ok=True)
        self.manifest = json.loads((self.path / 'scene.json').read_text(encoding='utf-8'))

    def read(self):
        return read_assembly(self.path, self.manifest, scene_file)

    def test_explicit_request_and_scope_match(self):
        settings = ImportSettings(assembly=True)
        self.assertEqual(['--assembly', '1', '--unreferenced-placement-policy', 'Reject'], arguments(settings))
        validate_request(self.manifest, settings)
        with self.assertRaises(ValueError):
            validate_request(self.manifest, ImportSettings())
        with self.assertRaises(ValueError):
            validate_request(self.manifest, ImportSettings(
                assembly=True, unreferenced_placement_policy='RetainDisconnected'))
        with self.assertRaises(ValueError):
            ImportSettings(unreferenced_placement_policy='RetainDisconnected')

    def test_native_instances_have_independent_world_topology(self):
        mesh, boundary, topology = self.read()
        self.assertEqual((12, 24, 16), (len(topology.faces), len(topology.edges), len(topology.vertices)))
        self.assertEqual([1, 2], [b.instance_index for b in topology.bodies])
        self.assertEqual([0, 0], [b.definition_index for b in topology.bodies])
        self.assertEqual(['21', '21'], [b.source_shape_id for b in topology.bodies])
        expected_bounds = (((83, 202, 305), (103, 212, 335)), ((-38, -3, 5), (-28, 17, 35)))
        for body, expected in zip(topology.bodies, expected_bounds):
            points = np.array([v.point_mm for v in
                               topology.vertices[body.vertex_start:body.vertex_start + body.vertex_count]])
            np.testing.assert_allclose([points.min(axis=0), points.max(axis=0)], expected, rtol=0, atol=1e-9)
        self.assertEqual(set(range(12)), set(mesh.cell_data['TopoFace']))
        self.assertEqual(set(range(24)), set(boundary.cell_data['TopoEdge']))

    def test_incomplete_or_duplicated_instance_payload_is_rejected(self):
        original = copy.deepcopy(self.manifest)
        for mutate in (lambda m: m['body_scenes'].pop(),
                       lambda m: m['body_scenes'][1].update(instance=1),
                       lambda m: m.update(root_count=True),
                       lambda m: m['instances'][1].update(parent=2),
                       lambda m: m['body_scenes'][0].update(source_shape_id='999')):
            self.manifest = copy.deepcopy(original)
            mutate(self.manifest)
            with self.assertRaises(ValueError):
                self.read()

    def test_core_status_and_definition_evidence_are_required(self):
        report_path = self.path / 'import-report.json'
        original = json.loads(report_path.read_text(encoding='utf-8'))
        for mutate in (lambda r: r.update(status='Cancelled'),
                       lambda r: r['product_document']['definitions'][0].update(checks_passed=False),
                       lambda r: r['assembly']['instances'].pop()):
            invalid = copy.deepcopy(original)
            mutate(invalid)
            report_path.write_text(json.dumps(invalid), encoding='utf-8')
            with self.assertRaises(ValueError):
                self.read()

    def test_wire_definition_survives_mixed_brep_scene(self):
        fixture = (Path(__file__).resolve().parents[3]
                   / 'source/STEP_SEMANTICS/tests/fixtures/assembly_box_and_wireframe.step')
        with tempfile.TemporaryDirectory(prefix='assembly-wire-scope-') as folder:
            output = Path(folder)
            run = subprocess.run([str(native_worker()), str(fixture), str(output), 'development', '0', '0', '1',
                                  'DeclaredOnly', 'DeclaredOnly', '16', '0', '0', 'DeclaredOnly',
                                  '--assembly', '1'], input=b'start\n', capture_output=True, timeout=60)
            self.assertEqual(0, run.returncode, run.stdout or run.stderr)
            manifest = json.loads((output / 'scene.json').read_text(encoding='utf-8'))
            self.assertEqual('Ready', manifest['status'])
            self.assertEqual('brep-view-scene.v7', manifest['format'])
            report = json.loads((output / 'import-report.json').read_text(encoding='utf-8'))
            self.assertEqual('Accepted', report['status'])
            mesh, boundary, topology = read_assembly(output, manifest, scene_file)
            self.assertEqual(12, len(topology.faces))
            self.assertEqual(2, len(topology.bodies))
            self.assertEqual((2, 1), (len(topology.wireframe.curves), len(topology.wireframe.points)))
            np.testing.assert_allclose(topology.wireframe.points[0].point_mm, (-39, 2, 3), rtol=0, atol=1e-12)

    def test_multiple_bodies_per_repeated_definition_keep_progress_and_inventory(self):
        fixture = (Path(__file__).resolve().parents[3]
                   / 'source/STEP_SEMANTICS/tests/fixtures/assembly_repeated_box.step')
        source = fixture.read_bytes().replace(b"'body',(#21,#931),#12", b"'body',(#21,#950,#931),#12")
        source = source.replace(b'ENDSEC;\nEND-ISO',
                                b"#950=MANIFOLD_SOLID_BREP('',#22);\nENDSEC;\nEND-ISO")
        with tempfile.TemporaryDirectory(prefix='assembly-multi-') as folder:
            path = Path(folder)
            input_path = path / 'source.step'
            input_path.write_bytes(source)
            output = path / 'display'
            output.mkdir()
            run = subprocess.run([str(native_worker()), str(input_path), str(output), 'development', '0', '0', '1',
                                  'DeclaredOnly', 'DeclaredOnly', '16', '0', '0', 'DeclaredOnly',
                                  '--assembly', '1'], input=b'start\n', capture_output=True, timeout=60)
            self.assertEqual(0, run.returncode, run.stdout or run.stderr)
            self.assertIn(b'@brep-view:bodies,4,4', run.stdout)
            manifest = json.loads((output / 'scene.json').read_text(encoding='utf-8'))
            mesh, _, topology = read_assembly(output, manifest, scene_file)
            self.assertEqual(4, len(topology.bodies))
            self.assertEqual(24, len(topology.faces))
            self.assertEqual([1, 1, 2, 2], [b.instance_index for b in topology.bodies])

    def test_failed_later_payload_never_emits_partial_scene(self):
        application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        loader = SceneLoader(ImportSettings(assembly=True))
        loaded, failed = [], []
        loader.loaded.connect(lambda *args: loaded.append(args))
        loader.failed.connect(lambda *args: failed.append(args))
        for corrupt in (False, True):
            folder = tempfile.TemporaryDirectory(prefix='assembly-adopt-')
            path = Path(folder.name)
            shutil.copytree(self.directory, path, dirs_exist_ok=True)
            if corrupt:
                (path / 'body-1/topology.json').write_text('{}', encoding='utf-8')
            loader.generation += 1
            process = Mock()
            process.readAllStandardOutput.return_value = QtCore.QByteArray()
            process.readAllStandardError.return_value = QtCore.QByteArray()
            loader.jobs[loader.generation] = dict(directory=folder, path=path / 'public.step', stopped=False,
                                                guard=None, timer=Mock(), process=process)
            loader._adopt(loader.generation, 0)
            self.assertFalse(path.exists())
        self.assertEqual(1, len(loaded))
        self.assertEqual(1, len(failed))
        self.assertEqual(12, len(loaded[0][3]['_topology'].faces))

    def test_face_edge_vertex_picks_keep_instance_measurements(self):
        mesh, boundary, topology = self.read()
        plot = pv.Plotter(off_screen=True, window_size=(400, 400))
        self.addCleanup(plot.close)
        actor = plot.add_mesh(mesh, smooth_shading=True)
        picker = TopologyPicker(plot)
        self.addCleanup(picker.clear)
        picker.set_scene(mesh, boundary, topology, actor)
        plot.camera.parallel_projection = True
        for body_index, center, corner, edge in ((0, (93, 207, 335), (83, 202, 335), (93, 202, 335)),
                                                  (1, (-33, 7, 35), (-38, -3, 35), (-33, -3, 35))):
            plot.camera_position = [(center[0], center[1], center[2] + 200), center, (0, 1, 0)]
            plot.camera.parallel_scale = 30
            plot.reset_camera_clipping_range()
            plot.show(auto_close=False, interactive=False)
            def pick(point):
                plot.renderer.SetWorldPoint(*point, 1)
                plot.renderer.WorldToDisplay()
                return picker.pick(*plot.renderer.GetDisplayPoint()[:2])
            for kind, point in (('face', center), ('edge', edge), ('vertex', corner)):
                hit = pick(point)
                self.assertIsNotNone(hit)
                self.assertEqual(kind, hit.kind)
                info = picker.info(hit)
                self.assertEqual(body_index, info.body)
                if kind == 'face':
                    self.assertAlmostEqual(200, info.area_mm2, places=6)
                elif kind == 'edge':
                    self.assertAlmostEqual(20 if body_index == 0 else 10, info.length_mm, places=6)
                else:
                    np.testing.assert_allclose(info.point_mm, corner, rtol=0, atol=1e-9)


if __name__ == '__main__':
    unittest.main()
