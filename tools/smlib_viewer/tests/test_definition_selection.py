# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit selection does not turn a subset into whole-file qualification."""
import copy
from dataclasses import replace
import hashlib
from pathlib import Path
import tempfile
import unittest

from . import test_topology
from .test_document import document_transport
from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker, scene_file
from tools.smlib_viewer.collection import read_document
from tools.smlib_viewer.definition_selection import arguments, validate_manifest
from tools.smlib_viewer.receipt import RequestReceipt
from PySide6 import QtWidgets


class SelectionContractTests(unittest.TestCase):
    def test_explicit_identity_and_unique_canonical_ids(self):
        self.assertEqual([], arguments(ImportSettings()))
        good = ImportSettings(selection_source_sha256='a' * 64, selected_definitions=('902', '7'))
        self.assertEqual(['--selection-source-sha256', 'a' * 64, '--select-definition', '902',
                          '--select-definition', '7'], arguments(good))
        for fields in (dict(selection_source_sha256=''), dict(selection_source_sha256='A' * 64),
                       dict(selected_definitions=()), dict(selected_definitions=('7', '7')),
                       dict(selected_definitions=('07',)), dict(selected_definitions=('7:21',))):
            with self.subTest(fields=fields), self.assertRaises(ValueError):
                replace(good, **fields)

    def test_complete_selection_receipt_and_body_identity_are_required(self):
        with tempfile.TemporaryDirectory() as folder:
            root = document_transport(Path(folder), counts=(1,))
            root['source_sha256'] = 'a' * 64
            settings = ImportSettings(selection_source_sha256='a' * 64, selected_definitions=('7',))
            with self.assertRaises(ValueError):
                validate_manifest(root, settings)
            root['definition_selection'] = dict(configuration='SourceCompleteNativeSelection-0.1',
                                                source_sha256='a' * 64, source_definition_count=2,
                                                selected_definitions=['7'], native_scope='ExplicitSelectedDefinitions')
            validate_manifest(root, settings)
            scenes = read_document(Path(folder), root, scene_file)
            self.assertEqual(1, len(scenes))
            with self.assertRaises(ValueError):
                validate_manifest(root, ImportSettings())
            for field, value in (('selected_definitions', ['902']), ('selected_definitions', ['7', '7']),
                                 ('source_definition_count', True), ('source_definition_count', 0),
                                 ('native_scope', 'WholeFile'), ('source_sha256', 'b' * 64)):
                invalid = copy.deepcopy(root)
                invalid['definition_selection'][field] = value
                with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                    validate_manifest(invalid, settings)
            with self.assertRaises(ValueError):
                validate_manifest(root, replace(settings, selected_definitions=('902',)))
            receipt = RequestReceipt('public.step').finish('Rendered', dict(root, native_import_status='Accepted'))
            self.assertEqual('ExplicitSelectedDefinitions', receipt['import_scope'])


@unittest.skipUnless(native_worker().is_file(), 'Requires matching native display worker')
class SelectionWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load
    close_loader = test_topology.NativeTopologyTests.close_loader

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.loader = SceneLoader(ImportSettings())
        self.addCleanup(self.close_loader)

    def test_selected_second_definition_keeps_its_own_units_identity_and_visible_scope(self):
        from tools.smlib_viewer.app import ViewerWindow
        source = test_topology.REPO / 'source/STEP_IMPORT/tests/fixtures/independent_two_parts.step'
        original = source.read_bytes()
        self.loader.settings = ImportSettings(selection_source_sha256=hashlib.sha256(original).hexdigest(),
                                              selected_definitions=('902',))
        _, mesh, boundary, metadata = self.load(source)
        self.assertEqual(1, len(metadata['_definitions']))
        self.assertEqual('902', metadata['source_definition_id'])
        self.assertEqual((20, 120), tuple(mesh.bounds[:2]))
        self.assertAlmostEqual(20000, metadata['_topology'].faces[0].area_mm2, places=4)
        self.assertEqual(original, source.read_bytes())
        window = ViewerWindow(self.loader.settings)
        try:
            window._loaded('public.step', mesh, boundary, metadata)
            self.assertIn('选入 1/2 个源定义', window.hud.text())
            self.assertIsNotNone(window.picker.resolve('face', 0))
        finally:
            window.close()

    def test_native_failure_in_unrequested_definition_does_not_block_selected_definition(self):
        source = test_topology.REPO / 'source/STEP_IMPORT/tests/fixtures/independent_two_parts.step'
        data = source.read_bytes().replace(b'SI_UNIT(.CENTI.,.METRE.)', b'SI_UNIT(.ATTO.,.METRE.)')
        self.loader.settings = ImportSettings(selection_source_sha256=hashlib.sha256(data).hexdigest(),
                                              selected_definitions=('7',))
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'public.step'
            path.write_bytes(data)
            _, mesh, _, metadata = self.load(path)
            self.assertEqual((2, 12), tuple(mesh.bounds[:2]))
            self.assertEqual(['7'], metadata['definition_selection']['selected_definitions'])
            self.assertEqual(data, path.read_bytes())

    def test_explicit_selection_and_explicit_repair_retain_both_qualifications(self):
        from tools.step_import_native_validation.tests.test_orientation_repair import broken_roof
        from .test_repair import settings
        data = broken_roof()
        self.loader.settings = replace(settings(data), selection_source_sha256=hashlib.sha256(data).hexdigest(),
                                       selected_definitions=('7',))
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'public.step'
            path.write_bytes(data)
            _, _, _, metadata = self.load(path)
            self.assertEqual('AcceptedWithRepairs', metadata['native_import_status'])
            self.assertEqual(1, metadata['repaired_face_count'])
            self.assertEqual(['7'], metadata['definition_selection']['selected_definitions'])
            self.assertEqual(1, metadata['definition_selection']['source_definition_count'])
            self.assertEqual(data, path.read_bytes())
