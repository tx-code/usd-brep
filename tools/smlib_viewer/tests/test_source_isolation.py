# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Selected source qualification must remain bound through worker adoption and HUD."""
from copy import deepcopy
from dataclasses import replace
import hashlib
from pathlib import Path
import tempfile
import unittest

from PySide6 import QtWidgets
from . import test_topology
from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker
from tools.smlib_viewer.definition_selection import arguments, validate_manifest
from tools.step_import_native_validation.tests.test_source_isolation import invalid_second


def settings(data):
    return ImportSettings(selection_source_sha256=hashlib.sha256(data).hexdigest(), selected_definitions=('7',),
                          source_error_isolation='FaceBoundsClause', maximum_isolated_source_failures=4,
                          maximum_source_influence_visits=100000)


class SourceIsolationIntentTests(unittest.TestCase):
    def test_policy_is_explicit_bounded_and_requires_selected_original(self):
        value = settings(invalid_second())
        self.assertIn('FaceBoundsClause', arguments(value))
        for fields in (dict(source_error_isolation=''), dict(source_error_isolation='guess'),
                       dict(maximum_isolated_source_failures=True), dict(maximum_isolated_source_failures=0),
                       dict(maximum_source_influence_visits=0), dict(selection_source_sha256=''),
                       dict(selected_definitions=())):
            with self.subTest(fields=fields), self.assertRaises(ValueError):
                replace(value, **fields)

    def test_isolated_receipt_requires_accepted_status_and_an_excluded_definition(self):
        value = settings(invalid_second())
        manifest = {
            'format': 'brep-view-scene.v5', 'definition_count': 1,
            'source_sha256': value.selection_source_sha256, 'native_import_status': 'AcceptedWithSourceIsolation',
            'body_scenes': [{'definition': 0, 'source_definition_id': '7'}],
            'definition_selection': {
                'configuration': 'SourceFaceClauseSelection-0.1', 'native_scope': 'ExplicitSelectedDefinitions',
                'source_sha256': value.selection_source_sha256, 'selected_definitions': ['7'],
                'source_definition_count': 2, 'isolated_source_failure_count': 1,
                'maximum_isolated_failures': 4, 'maximum_influence_visits': 100000,
                'influence_visits': 50, 'source_scope': 'SelectedDefinitionsOnly',
            },
        }
        validate_manifest(manifest, value)
        inconsistent = deepcopy(manifest)
        inconsistent['definition_selection']['source_definition_count'] = 1
        with self.assertRaises(ValueError):
            validate_manifest(inconsistent, value)
        no_op = deepcopy(manifest)
        no_op['definition_selection'].update(isolated_source_failure_count=0, source_scope='AllDefinitionsChecked')
        for status in ('Accepted', 'AcceptedUnderInterpretation'):
            no_op['native_import_status'] = status
            validate_manifest(no_op, value)
        for status in ('Rejected', 'UnknownStatus', 'AcceptedWithRepairs', 'AcceptedWithSourceIsolation', None):
            no_op['native_import_status'] = status
            with self.subTest(status=status), self.assertRaises(ValueError):
                validate_manifest(no_op, value)


@unittest.skipUnless(native_worker().is_file(), 'Requires matching native worker')
class SourceIsolationWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load
    close_loader = test_topology.NativeTopologyTests.close_loader

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.input = invalid_second()
        self.loader = SceneLoader(settings(self.input))
        self.addCleanup(self.close_loader)

    def test_complete_scene_retains_scope_original_identity_and_native_picking(self):
        from tools.smlib_viewer.app import ViewerWindow
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'public.step'
            source.write_bytes(self.input)
            _, mesh, boundary, metadata = self.load(source)
            self.assertEqual(self.input, source.read_bytes())
        self.assertEqual('AcceptedWithSourceIsolation', metadata['native_import_status'])
        selection = metadata['definition_selection']
        self.assertEqual('SelectedDefinitionsOnly', selection['source_scope'])
        self.assertEqual(2, selection['source_definition_count'])
        self.assertEqual(1, selection['isolated_source_failure_count'])
        self.assertEqual(['7'], selection['selected_definitions'])
        validate_manifest(metadata, self.loader.settings)
        for changed in (dict(isolated_source_failure_count=True), dict(influence_visits=100001),
                        dict(source_scope='AllDefinitionsChecked'), dict(maximum_isolated_failures=5),
                        dict(configuration='SourceCompleteNativeSelection-0.1')):
            tampered = deepcopy(metadata)
            tampered['definition_selection'].update(changed)
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                validate_manifest(tampered, self.loader.settings)
        window = ViewerWindow(self.loader.settings)
        try:
            window._loaded('public.step', mesh, boundary, metadata)
            self.assertEqual(1, window.hud.text().count('选入 1/2'))
            self.assertEqual(1, window.hud.text().count('隔离 1 处未选定义的源错误'))
            self.assertNotIn('已修复', window.hud.text())
            for kind in ('face', 'edge', 'vertex'):
                self.assertIsNotNone(window.picker.resolve(kind, 0))
        finally:
            window.close()
