# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Whole-document transport and definition-local display, using public boxes."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

from . import test_topology
from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker, scene_file
from tools.smlib_viewer.collection import read_document
from PySide6 import QtCore, QtWidgets


def document_transport(directory, counts=(1, 1), *, shared=False):
    bodies = []
    for definition, count in enumerate(counts):
        for local in range(count):
            index = len(bodies)
            folder = directory / f'body-{index}'
            folder.mkdir()
            mesh, boundary, topology, body = test_topology.box_transport()
            offset = 0 if shared else 100 * index
            mesh.points[:, 0] += offset
            boundary.points[:, 0] += offset
            for point in topology['vertices']:
                point['point_mm'][0] += offset
            mesh.save(folder / 'mesh.vtk')
            boundary.save(folder / 'edges.vtk')
            (folder / 'topology.json').write_text(json.dumps(topology), encoding='utf-8')
            body.update(ordinal=index, definition=definition, source_definition_id=str(7 + 1000 * definition),
                        source_shape_id=str(21 + (local if shared else index) * 10000), role='MaterialSolid',
                        mesh=f'body-{index}/mesh.vtk', boundary=f'body-{index}/edges.vtk',
                        topology=f'body-{index}/topology.json')
            bodies.append(body)
    root = dict(format='brep-view-scene.v5', status='Ready', role='IndependentProductDefinitions',
                units='mm', coordinate_scope='DefinitionLocal', definition_count=len(counts),
                auxiliary_required=False, body_count=len(bodies), body_scenes=bodies)
    for key in ('faces', 'edges', 'vertices', 'triangles', 'points'):
        root[key] = sum(b[key] for b in bodies)
    return root


class DocumentTransportTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.directory = Path(self.folder.name)
        self.manifest = document_transport(self.directory)

    def read(self, manifest=None):
        return read_document(self.directory, self.manifest if manifest is None else manifest, scene_file)

    def test_separate_frames_and_complete_inventory(self):
        scenes = self.read()
        self.assertEqual(2, len(scenes))
        self.assertEqual((2, 12), tuple(scenes[0].mesh.bounds[:2]))
        self.assertEqual((102, 112), tuple(scenes[1].mesh.bounds[:2]))
        for index, scene in enumerate(scenes):
            self.assertEqual(index, scene.metadata['definition_index'])
            self.assertEqual((1, 2), (scene.metadata['body_count'], scene.metadata['document_body_count']))
            self.assertEqual((6, 12, 8), tuple(len(getattr(scene.metadata['_topology'], key))
                                              for key in ('faces', 'edges', 'vertices')))
            self.assertEqual(200, scene.metadata['_topology'].faces[0].area_mm2)

    def test_shared_source_shape_is_not_a_duplicate_definition(self):
        self.manifest['body_scenes'][1]['source_shape_id'] = self.manifest['body_scenes'][0]['source_shape_id']
        self.assertEqual(2, len(self.read()))

    def test_assembly_scope_missing_definition_and_duplicate_owner_are_rejected(self):
        for field, value in (('coordinate_scope', 'Assembly'), ('definition_count', 3), ('body_count', True)):
            invalid = copy.deepcopy(self.manifest)
            invalid[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.read(invalid)
        invalid = copy.deepcopy(self.manifest)
        invalid['body_scenes'][1]['source_definition_id'] = invalid['body_scenes'][0]['source_definition_id']
        with self.assertRaisesRegex(ValueError, 'InvalidDocumentInventory'):
            self.read(invalid)

    def test_cross_definition_payload_reuse_and_global_limits_are_rejected(self):
        invalid = copy.deepcopy(self.manifest)
        for key in ('mesh', 'boundary', 'topology'):
            invalid['body_scenes'][1][key] = invalid['body_scenes'][0][key]
        with self.assertRaisesRegex(ValueError, 'ReusedDocumentPayload'):
            self.read(invalid)
        with patch('tools.smlib_viewer.collection.MAX_FACES', 6):
            with self.assertRaisesRegex(ValueError, 'DocumentDisplayLimit'):
                self.read()
        largest = max((self.directory / f'body-{i}/topology.json').stat().st_size for i in range(2))
        with patch('tools.smlib_viewer.collection.MAX_TOPOLOGY_BYTES', largest + 1):
            with self.assertRaisesRegex(ValueError, 'DocumentDisplayLimit'):
                self.read()

    def test_material_collection_stays_inside_its_definition(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            manifest = document_transport(path, (2, 1))
            scenes = read_document(path, manifest, scene_file)
            self.assertEqual(('MaterialSolidCollection', 'MaterialSolid'), tuple(s.metadata['role'] for s in scenes))
            self.assertEqual((2, 1), tuple(len(s.metadata['_topology'].bodies) for s in scenes))
            manifest['body_scenes'][1]['source_shape_id'] = manifest['body_scenes'][0]['source_shape_id']
            with self.assertRaisesRegex(ValueError, 'InvalidCollectionBody'):
                read_document(path, manifest, scene_file)


class DocumentAdoptionTests(unittest.TestCase):
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
        manifest = document_transport(path)
        (path / 'scene.json').write_text(json.dumps(manifest), encoding='utf-8')
        generation = self.loader.generation
        process = Mock()
        process.state.return_value = QtCore.QProcess.NotRunning
        self.loader.jobs[generation] = dict(directory=folder, path=path / 'public.step', stopped=False,
                                            guard=None, timer=Mock(), process=process)
        return generation, path

    def test_corrupt_later_definition_never_replaces_accepted_request(self):
        generation, path = self.request()
        self.loader._adopt(generation, 0)
        self.assertEqual(1, len(self.loaded))
        previous = self.loaded[0]
        self.assertEqual(2, len(previous[-1]['_definitions']))
        self.assertFalse(path.exists())
        generation, path = self.request()
        (path / 'body-1/topology.json').write_text('{}', encoding='utf-8')
        self.loader._adopt(generation, 0)
        self.assertEqual(1, len(self.loaded))
        self.assertIs(previous, self.loaded[0])
        self.assertEqual(1, len(self.failed))
        self.assertFalse(path.exists())

    def test_cancel_after_all_payload_checks_never_adopts_any_definition(self):
        generation, path = self.request()
        self.loader.progress.connect(lambda p: self.loader.cancel(notify=False) if p.phase == 'render' else None)
        self.loader._adopt(generation, 0)
        self.assertFalse(self.loaded)
        self.assertFalse(path.exists())

    def test_switch_resets_selection_and_camera_without_a_new_import(self):
        from tools.smlib_viewer.app import ViewerWindow
        generation, _ = self.request()
        self.loader._adopt(generation, 0)
        window = ViewerWindow()
        window.show()
        completed = []
        window.load_complete.connect(completed.append)
        try:
            window._loaded(*self.loaded[0])
            self.application.processEvents()
            self.assertTrue(window.definition_selector.isVisible())
            old_hit = window.picker.resolve('vertex', 0)
            window.selected = [('vertex', 0), ('vertex', 1)]
            window._restore_selection()
            self.assertIn('两点距离：10 mm', window.hud.text())
            window.definition_selector.setCurrentIndex(1)
            self.assertEqual(1, window.metadata['definition_index'])
            self.assertEqual((102, 112), tuple(window.mesh.bounds[:2]))
            self.assertEqual([], window.selected)
            self.assertFalse(window.picker.is_current(old_hit))
            self.assertNotIn('两点距离', window.hud.text())
            self.assertIn('定义 2/2 · 本地坐标', window.hud.text())
            self.assertEqual([True], completed)
            with patch.object(window, '_redraw', side_effect=[ValueError('SyntheticDisplayFailure'), None]):
                window.definition_selector.setCurrentIndex(0)
            self.assertEqual(1, window.definition_selector.currentIndex())
            self.assertEqual(1, window.metadata['definition_index'])
            self.assertEqual([True], completed)
        finally:
            window.close()


@unittest.skipUnless(native_worker().is_file(), 'Requires native display worker')
class DocumentWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load
    close_loader = test_topology.NativeTopologyTests.close_loader

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.loader = SceneLoader(ImportSettings())
        # Match the real-worker fixture's QObject disposal boundary. Deferred
        # QProcess deletion must finish before the next test starts its event loop.
        self.addCleanup(self.close_loader)

    def test_actual_worker_retains_shared_source_shape_with_different_units(self):
        source = test_topology.REPO / 'source/STEP_IMPORT/tests/fixtures/independent_two_parts.step'
        original = source.read_bytes()
        _, mesh, _, metadata = self.load(source)
        self.assertEqual('brep-view-scene.v5', metadata['format'])
        self.assertEqual(2, len(metadata['_definitions']))
        self.assertEqual((2, 12), tuple(mesh.bounds[:2]))
        second = metadata['_definitions'][1]
        self.assertEqual((20, 120), tuple(second.mesh.bounds[:2]))
        self.assertAlmostEqual(20000, second.metadata['_topology'].faces[0].area_mm2, places=4)
        receipt = self.loader.worker_result
        self.assertEqual((2, 12, 24, 16), tuple(receipt['topology_inventory'][k]
                                              for k in ('definitions', 'faces', 'edges', 'vertices')))
        self.assertEqual('DefinitionLocal', receipt['display_scope'])
        self.assertEqual(original, source.read_bytes())
