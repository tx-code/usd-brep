# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Complete part-associated transport, native worker and refusal controls."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock

from . import test_collection, test_document, test_topology
from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker, scene_file
from tools.smlib_viewer.collection import read_collection, read_collection_scene, read_document
from PySide6 import QtCore, QtWidgets


def associated(directory, definition='7', name='associated.json'):
    value = test_topology.point_auxiliary()
    value['source_definition_id'] = definition
    (directory / name).write_text(json.dumps(value), encoding='utf-8')
    return name


class AssociatedTransportTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix='viewer-public-part-associated-')
        self.addCleanup(self.folder.cleanup)
        self.directory = Path(self.folder.name)

    def test_collection_requires_its_own_part_payload_and_owner_identity(self):
        manifest = test_collection.collection_transport(self.directory)
        manifest.update(format='brep-view-scene.v9', auxiliary_required=True, associated_definition_id='7',
                        auxiliary=associated(self.directory))
        mesh, boundary, topology, auxiliary = read_collection_scene(self.directory, manifest, scene_file)
        self.assertEqual(12, len(topology.faces))
        self.assertEqual(1, auxiliary.counts['points'])
        self.assertEqual('7', auxiliary.source_definition_id)
        with self.assertRaisesRegex(ValueError, 'AssociatedGeometryReaderRequired'):
            read_collection(self.directory, manifest, scene_file)
        wrong = copy.deepcopy(manifest)
        wrong['associated_definition_id'] = '1007'
        with self.assertRaisesRegex(ValueError, 'AssociatedDefinitionIdentityMismatch'):
            read_collection_scene(self.directory, wrong, scene_file)
        for value in (False, None, 1):
            wrong = dict(manifest, auxiliary_required=value)
            with self.assertRaises(ValueError):
                read_collection_scene(self.directory, wrong, scene_file)
        (self.directory / 'associated.json').unlink()
        with self.assertRaises((OSError, ValueError)):
            read_collection_scene(self.directory, manifest, scene_file)
        self.assertEqual(1, auxiliary.counts['points'])  # Owning projection survives payload cleanup.

    def test_document_validates_every_definition_before_returning_any_scene(self):
        manifest = test_document.document_transport(self.directory)
        manifest.update(format='brep-view-scene.v10', definition_auxiliary=[
            dict(definition=0, source_definition_id='7', required=True, auxiliary=associated(self.directory)),
            dict(definition=1, source_definition_id='1007', required=False)])
        scenes = read_document(self.directory, manifest, scene_file)
        self.assertEqual(1, scenes[0].metadata['_auxiliary'].counts['points'])
        self.assertNotIn('_auxiliary', scenes[1].metadata)
        for field, value in [('source_definition_id', '999'), ('definition', True), ('required', 1)]:
            wrong = copy.deepcopy(manifest)
            wrong['definition_auxiliary'][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                read_document(self.directory, wrong, scene_file)
        wrong = copy.deepcopy(manifest)
        wrong['definition_auxiliary'] = wrong['definition_auxiliary'][:1]
        with self.assertRaises(ValueError):
            read_document(self.directory, wrong, scene_file)
        wrong = copy.deepcopy(manifest)
        wrong['definition_auxiliary'][1].update(required=True, auxiliary='associated.json')
        with self.assertRaises(ValueError):
            read_document(self.directory, wrong, scene_file)


class AssociatedAdoptionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])

    def test_missing_required_payload_keeps_previous_complete_scene(self):
        loader = SceneLoader(ImportSettings())
        self.addCleanup(loader.close)
        loaded, failed = [], []
        loader.loaded.connect(lambda *args: loaded.append(args))
        loader.failed.connect(lambda *args: failed.append(args))

        def request(missing):
            folder = tempfile.TemporaryDirectory(prefix='viewer-public-associated-adopt-')
            path = Path(folder.name)
            manifest = test_collection.collection_transport(path)
            manifest.update(format='brep-view-scene.v9', auxiliary_required=True,
                            associated_definition_id='7', auxiliary=associated(path))
            if missing:
                (path / 'associated.json').unlink()
            (path / 'scene.json').write_text(json.dumps(manifest), encoding='utf-8')
            process = Mock()
            process.state.return_value = QtCore.QProcess.NotRunning
            generation = loader.generation
            loader.jobs[generation] = dict(directory=folder, path=path / 'public.step', stopped=False,
                                           guard=None, timer=Mock(), process=process)
            loader._adopt(generation, 0)
            self.assertFalse(path.exists())

        request(False)
        self.assertEqual(1, len(loaded))
        previous = loaded[0]
        request(True)
        self.assertEqual(1, len(loaded))
        self.assertIs(previous, loaded[0])
        self.assertEqual(1, len(failed))


@unittest.skipUnless(native_worker().is_file(), 'Requires matching native display worker')
class AssociatedWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load
    close_loader = test_topology.NativeTopologyTests.close_loader

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.loader = SceneLoader(ImportSettings())
        self.addCleanup(self.close_loader)

    def test_actual_collection_retains_roles_and_native_body_picking(self):
        from tools.smlib_viewer.app import ViewerWindow
        source = test_topology.REPO / 'source/STEP_IMPORT/tests/fixtures/collection_associated_geometry.step'
        _, mesh, boundary, metadata = self.load(source)
        self.assertEqual('brep-view-scene.v9', metadata['format'])
        self.assertEqual('Accepted', metadata['native_import_status'])
        self.assertEqual(2, metadata['body_count'])
        inventory = metadata['_auxiliary'].counts
        self.assertEqual((1, 1, 4, 2, 2, 1), tuple(inventory[k] for k in
                         ('wireframe_groups', 'supporting_groups', 'curves', 'points', 'frames', 'planes')))
        window = ViewerWindow(self.loader.settings)
        try:
            window._loaded(str(source), mesh, boundary, metadata)
            for kind in ('face', 'edge', 'vertex'):
                self.assertIsNotNone(window.picker.resolve(kind, 0))
            self.assertIn('附属', window.hud.text())
        finally:
            window.close()

    def test_actual_document_keeps_associated_geometry_in_its_definition(self):
        source = test_topology.REPO / 'source/STEP_IMPORT/tests/fixtures/document_associated_geometry.step'
        _, mesh, boundary, metadata = self.load(source)
        definitions = metadata['_definitions']
        self.assertEqual(2, len(definitions))
        self.assertEqual('7', definitions[0].metadata['source_definition_id'])
        self.assertEqual('7', definitions[0].metadata['_auxiliary'].source_definition_id)
        self.assertEqual('20802', definitions[1].metadata['source_definition_id'])
        self.assertNotIn('_auxiliary', definitions[1].metadata)
        self.assertEqual(3, self.loader.worker_result['topology_inventory']['bodies'])


if __name__ == '__main__':
    unittest.main()
