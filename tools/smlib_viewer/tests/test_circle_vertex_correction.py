# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""A displaced-vertex repair has an explicit request and distinct picking/HUD."""
import copy
from dataclasses import replace
from pathlib import Path
import tempfile
import unittest

from PySide6 import QtWidgets
from tools.smlib_viewer.backend import SceneLoader, native_worker
from tools.smlib_viewer.repair import arguments, validate_vertex_manifest
from tools.step_import_native_validation.tests.test_circle_vertex_correction import displaced_source
from . import test_band_repair, test_topology


def settings(data):
    return replace(test_band_repair.settings(data), maximum_band_circle_vertices=1,
                   maximum_band_vertex_move_mm='0.001', maximum_band_total_vertex_move_mm='0.001',
                   band_circle_vertices=('7:21:120:110',))


class CircleVertexIntentTests(unittest.TestCase):
    def test_no_implicit_budget_or_repair_scope(self):
        good = settings(displaced_source())
        self.assertIn('7:21:120:110', arguments(good))
        for fields in (dict(maximum_band_circle_vertices=0), dict(maximum_band_vertex_move_mm=''),
                       dict(maximum_band_total_vertex_move_mm='nan'), dict(maximum_band_vertex_move_mm=0.001),
                       dict(maximum_band_vertex_move_mm='1e999999999'), dict(band_circle_vertices=()),
                       dict(band_circle_vertices=good.band_circle_vertices * 2),
                       dict(band_repair_source_sha256='', cylindrical_band_repairs=(), maximum_band_repair_faces=0,
                            maximum_band_bound_edits=0, maximum_band_proof_operations=0)):
            with self.subTest(fields=fields), self.assertRaises(ValueError):
                replace(good, **fields)

    def test_receipt_binds_original_identity_scope_and_exact_budgets(self):
        good = settings(displaced_source())
        manifest = dict(repair_kind='CylindricalBandWithCircleVertices', native_import_status='AcceptedWithRepairs',
                        repair_applied=True, repaired_vertex_count=1, circle_vertex_repair=dict(
                            source_sha256=good.band_repair_source_sha256, maximum_vertices=1,
                            maximum_displacement_mm='1/1000', maximum_total_displacement_mm='1/1000',
                            request_complete=True, vertices=['7:21:120:110']))
        validate_vertex_manifest(manifest, good)
        for key, value in (('source_sha256', '0' * 64), ('maximum_vertices', 2), ('maximum_displacement_mm', '1/10'),
                           ('request_complete', False), ('vertices', ['7:21:121:111'])):
            changed = copy.deepcopy(manifest)
            changed['circle_vertex_repair'][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_vertex_manifest(changed, good)
        with self.assertRaises(ValueError):
            validate_vertex_manifest({}, good)
        with self.assertRaises(ValueError):
            validate_vertex_manifest(manifest, test_band_repair.settings(displaced_source()))


@unittest.skipUnless(native_worker().is_file(), 'Requires matching native worker')
class CircleVertexWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load
    close_loader = test_topology.NativeTopologyTests.close_loader

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def test_complete_scene_has_distinct_repair_label_and_native_picking(self):
        from tools.smlib_viewer.app import ViewerWindow
        data = displaced_source()
        self.loader = SceneLoader(settings(data))
        self.addCleanup(self.close_loader)
        with tempfile.TemporaryDirectory(prefix='public-circle-vertex-') as folder:
            source = Path(folder) / 'source.step'
            source.write_bytes(data)
            _, mesh, boundary, metadata = self.load(source)
            self.assertEqual(data, source.read_bytes())
        self.assertEqual('CylindricalBandWithCircleVertices', metadata['repair_kind'])
        self.assertEqual(1, metadata['repaired_vertex_count'])
        window = ViewerWindow(self.loader.settings)
        try:
            window._loaded('public.step', mesh, boundary, metadata)
            self.assertIn('1 个圆边顶点', window.hud.text())
            self.assertIn('1 处周期边界角色', window.hud.text())
            for kind in ('face', 'edge', 'vertex'):
                self.assertIsNotNone(window.picker.resolve(kind, 0))
        finally:
            window.close()
