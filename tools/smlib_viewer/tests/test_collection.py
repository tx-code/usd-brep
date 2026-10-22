# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Complete body transport, global identities and atomic collection adoption."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch, Mock
import numpy as np
from PySide6 import QtCore, QtWidgets
from tools.smlib_viewer.backend import ImportSettings, SceneLoader, scene_file, native_worker
from tools.smlib_viewer.collection import read_collection
from tools.smlib_viewer.tests import test_topology
box_transport = test_topology.box_transport
REPO = test_topology.REPO


def collection_transport(directory, coincident=False):
    bodies = []
    for i in range(2):
        folder = directory/f'body-{i}'
        folder.mkdir()
        mesh, boundary, topology, body = box_transport()
        dx = 0 if coincident else 100*i
        mesh.points[:, 0] += dx
        boundary.points[:, 0] += dx
        for point in topology['vertices']:
            point['point_mm'][0] += dx
        mesh.save(folder/'mesh.vtk')
        boundary.save(folder/'edges.vtk')
        (folder/'topology.json').write_text(json.dumps(topology), encoding='utf-8')
        body.update(ordinal=i, source_shape_id=str(21+10000*i), mesh=f'body-{i}/mesh.vtk',
                    boundary=f'body-{i}/edges.vtk', topology=f'body-{i}/topology.json')
        bodies.append(body)
    root = dict(format='brep-view-scene.v4', status='Ready', role='MaterialSolidCollection',
                units='mm', auxiliary_required=False, body_count=2, body_scenes=bodies)
    for key in ('faces', 'edges', 'vertices', 'triangles', 'points'):
        root[key] = sum(b[key] for b in bodies)
    return root


class CollectionTransportTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.directory = Path(self.folder.name)
        self.manifest = collection_transport(self.directory)

    def read(self, manifest=None):
        return read_collection(self.directory, self.manifest if manifest is None else manifest, scene_file)

    def test_global_and_local_identity_and_independent_native_measurements(self):
        mesh, boundary, topology = self.read()
        self.assertEqual((12, 24, 16), (len(topology.faces), len(topology.edges), len(topology.vertices)))
        self.assertEqual((1, 0, 200), (topology.faces[6].body, topology.faces[6].local_id, topology.faces[6].area_mm2))
        self.assertEqual((0, 1), tuple(b.id for b in topology.bodies))
        self.assertEqual((2, 112, -3, 17, 5, 35), tuple(mesh.bounds))
        self.assertEqual(set(range(12)), set(np.unique(mesh.cell_data['TopoFace'])))
        self.assertEqual(set(range(24)), set(np.unique(boundary.cell_data['TopoEdge'])))
        self.assertEqual((102, -3, 5), topology.vertices[8].point_mm)

    def test_real_rays_select_faces_edges_and_vertices_with_correct_body_owner(self):
        import pyvista as pv
        from tools.smlib_viewer.picking import TopologyPicker
        mesh, boundary, data = self.read()
        plot = pv.Plotter(off_screen=True, window_size=(800, 400))
        picker = None
        try:
            actor = plot.add_mesh(mesh, smooth_shading=True, split_sharp_edges=True)
            plot.camera_position = [(57, 7, 300), (57, 7, 20), (0, 1, 0)]
            plot.camera.parallel_projection = True
            plot.camera.parallel_scale = 35
            plot.reset_camera_clipping_range()
            plot.show(auto_close=False, interactive=False)
            picker = TopologyPicker(plot)
            picker.set_scene(mesh, boundary, data, actor)
            for point, kind, owner in (((7, 7, 35), 'face', 0), ((107, 7, 35), 'face', 1),
                                       ((107, -3, 35), 'edge', 1), ((102, -3, 35), 'vertex', 1)):
                plot.renderer.SetWorldPoint(*point, 1)
                plot.renderer.WorldToDisplay()
                x, y, _ = plot.renderer.GetDisplayPoint()
                hit = picker.pick(x, y)
                self.assertIsNotNone(hit)
                self.assertEqual(kind, hit.kind)
                self.assertEqual(owner, picker.info(hit).body)
        finally:
            if picker: picker.clear()
            plot.close()

    def test_coincident_bodies_are_not_welded(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            manifest = collection_transport(path, coincident=True)
            mesh, _, topology = read_collection(path, manifest, scene_file)
            self.assertEqual(manifest['points'], mesh.n_points)
            self.assertEqual(16, len(topology.vertices))
            self.assertEqual(topology.vertices[0].point_mm, topology.vertices[8].point_mm)
            self.assertNotEqual(topology.vertices[0].body, topology.vertices[8].body)

    def test_missing_last_body_duplicate_payload_and_mismatched_totals_refuse_whole_scene(self):
        invalid = copy.deepcopy(self.manifest)
        invalid['body_scenes'][1]['mesh'] = 'missing.vtk'
        with self.assertRaises(ValueError): self.read(invalid)
        invalid = copy.deepcopy(self.manifest)
        for key in ('mesh', 'boundary', 'topology'):
            invalid['body_scenes'][1][key] = invalid['body_scenes'][0][key]
        with self.assertRaisesRegex(ValueError, 'ReusedCollectionPayload'): self.read(invalid)
        invalid = copy.deepcopy(self.manifest)
        invalid['faces'] -= 1
        with self.assertRaisesRegex(ValueError, 'InvalidCollectionInventory'): self.read(invalid)

    def test_capacity_is_cumulative_and_checked_before_combination(self):
        with patch('tools.smlib_viewer.collection.MAX_FACES', 6):
            with self.assertRaisesRegex(ValueError, 'CollectionDisplayLimit'): self.read()
        one = (self.directory/'body-0/topology.json').stat().st_size
        with patch('tools.smlib_viewer.collection.MAX_TOPOLOGY_BYTES', one+1):
            with self.assertRaisesRegex(ValueError, 'CollectionDisplayLimit'): self.read()


@unittest.skipUnless(native_worker().is_file(), 'Requires native display worker')
class CollectionWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.loader = SceneLoader(ImportSettings())

    def tearDown(self):
        self.loader.close()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)

    def test_actual_collection_worker_preserves_body_ranges_and_hud_measurements(self):
        source = REPO/'source/STEP_IMPORT/tests/fixtures/two_material_boxes.step'
        _, mesh, boundary, metadata = self.load(source)
        self.assertEqual('brep-view-scene.v4', metadata['format'])
        self.assertEqual(2, metadata['body_count'])
        self.assertEqual((2, 112, -3, 17, 5, 35), tuple(mesh.bounds))
        data = metadata['_topology']
        self.assertEqual((12, 24, 16), (len(data.faces), len(data.edges), len(data.vertices)))
        from tools.smlib_viewer.app import ViewerWindow
        window = ViewerWindow(self.loader.settings)
        try:
            window._loaded(str(source), mesh, boundary, metadata)
            hit = window.picker.resolve('face', 6)
            self.assertIsNotNone(hit)
            self.assertEqual((1, 0), (window.picker.info(hit).body, window.picker.info(hit).local_id))
            window.selected = [('face', 6)]
            window._update_measurements()
            self.assertIn('实体集合 · 2 件', window.hud.text())
            self.assertIn('实体 2 · 面 0', window.hud.text())
        finally:
            window.close()


class CollectionAdoptionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.loader = SceneLoader(ImportSettings())
        self.addCleanup(self.loader.close)
        self.loaded, self.failed = [], []
        self.loader.loaded.connect(lambda *args: self.loaded.append(args))
        self.loader.failed.connect(lambda *args: self.failed.append(args))

    def request(self):
        folder = tempfile.TemporaryDirectory()
        path = Path(folder.name)
        manifest = collection_transport(path)
        (path/'scene.json').write_text(json.dumps(manifest), encoding='utf-8')
        process = Mock()
        process.state.return_value = QtCore.QProcess.NotRunning
        generation = self.loader.generation
        self.loader.jobs[generation] = dict(directory=folder, path=path/'public.step', stopped=False,
                                            guard=None, timer=Mock(), process=process)
        return generation, path

    def test_corrupt_last_body_never_replaces_complete_prior_scene(self):
        generation, path = self.request()
        self.loader._adopt(generation, 0)
        self.assertEqual(1, len(self.loaded))
        previous = self.loaded[0]
        self.assertFalse(path.exists())
        generation, path = self.request()
        (path/'body-1/topology.json').write_text('{}', encoding='utf-8')
        self.loader._adopt(generation, 0)
        self.assertEqual(1, len(self.loaded))
        self.assertIs(previous, self.loaded[0])
        self.assertEqual(1, len(self.failed))
        self.assertFalse(path.exists())

    def test_cancel_at_adoption_keeps_all_collection_payloads_private(self):
        generation, path = self.request()
        self.loader.progress.connect(lambda p: self.loader.cancel(notify=False) if p.phase == 'render' else None)
        self.loader._adopt(generation, 0)
        self.assertFalse(self.loaded)
        self.assertFalse(path.exists())
