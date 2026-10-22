# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit complex-facet binding transport and native qualification visibility."""
from dataclasses import replace
from pathlib import Path
import tempfile
import unittest

from . import test_topology
from tools.step_import_native_validation.tests.test_complex_mapping import reordered_unit
from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker
from tools.smlib_viewer.interpretation import complex_mapping_arguments, validate_complex_mapping_receipt
from PySide6 import QtWidgets


class ComplexMappingPolicyTests(unittest.TestCase):
    def test_closed_policy_and_request_qualification_receipt(self):
        self.assertEqual([], complex_mapping_arguments(ImportSettings()))
        good = ImportSettings(complex_mapping_interpretation='RegisteredFacetNames')
        self.assertEqual(['--complex-mapping-interpretation', 'RegisteredFacetNames'], complex_mapping_arguments(good))
        for fields in (dict(strict_source=True), dict(complex_mapping_interpretation='Guess')):
            with self.subTest(fields=fields), self.assertRaises(ValueError):
                replace(good, **fields)
        scene = dict(native_import_status='AcceptedUnderInterpretation', complex_mapping_interpretation=dict(
            requested='RegisteredFacetNames', applied='RegisteredFacetNames', records_interpreted=1))
        validate_complex_mapping_receipt(scene, good)
        with self.assertRaises(ValueError):
            validate_complex_mapping_receipt(scene, ImportSettings())
        with self.assertRaises(ValueError):
            validate_complex_mapping_receipt({}, good)
        for change in (dict(applied='DeclaredOnly'), dict(records_interpreted=True), dict(records_interpreted=-1)):
            with self.subTest(change=change), self.assertRaises(ValueError):
                validate_complex_mapping_receipt(dict(scene, complex_mapping_interpretation=dict(
                    scene['complex_mapping_interpretation'], **change)), good)
        with self.assertRaises(ValueError):
            validate_complex_mapping_receipt(dict(scene, native_import_status='Accepted'), good)


@unittest.skipUnless(native_worker().is_file(), 'Requires matching native display worker')
class ComplexMappingWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load
    close_loader = test_topology.NativeTopologyTests.close_loader

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.loader = SceneLoader(ImportSettings(complex_mapping_interpretation='RegisteredFacetNames'))
        self.addCleanup(self.close_loader)

    def test_real_worker_preserves_source_interpretation_after_complete_scene_adoption(self):
        from tools.smlib_viewer.app import ViewerWindow
        data = reordered_unit()
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'public.step'
            path.write_bytes(data)
            _, mesh, boundary, metadata = self.load(path)
            self.assertEqual(data, path.read_bytes())
        self.assertEqual('AcceptedUnderInterpretation', metadata['native_import_status'])
        self.assertEqual(1, metadata['complex_mapping_interpretation']['records_interpreted'])
        window = ViewerWindow(self.loader.settings)
        try:
            window._loaded('public.step', mesh, boundary, metadata)
            self.assertEqual(1, window.hud.text().count('源解释模式'))
            self.assertIsNotNone(window.picker.resolve('face', 0))
        finally:
            window.close()
