# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Real isolated worker + Qt/VTK smoke with public synthetic geometry only."""
import hashlib
import os
from pathlib import Path
import tempfile
import unittest

os.environ.setdefault("QT_QPA_PLATFORM", "windows" if os.name == "nt" else "offscreen")
os.environ.setdefault("PYVISTA_OFF_SCREEN", "true")

from PySide6 import QtCore, QtWidgets
from tools.smlib_viewer.app import ViewerWindow
from tools.smlib_viewer.backend import native_worker

REPO = Path(__file__).resolve().parents[3]
BOX = REPO / "tools/step_import_validation/corpus/ap203e2-planar/v1/cases/S203-P04/input.step"


@unittest.skipUnless(native_worker().is_file(), "Set BREP_VIEW_WORKER to the built step_view_export")
class ViewerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.window = ViewerWindow()
        self.window.show()

    def tearDown(self):
        self.window.close()

    def load(self, path, before_wait=None):
        loop = QtCore.QEventLoop()
        results = []

        def finished(ok):
            results.append(ok)
            loop.quit()
        self.window.load_complete.connect(finished)
        self.window.load(path)
        if before_wait:
            before_wait()
        timer = QtCore.QTimer()
        timer.setSingleShot(True)
        timer.timeout.connect(loop.quit)
        timer.start(30000)
        if not results:
            loop.exec()
        self.window.load_complete.disconnect(finished)
        self.assertTrue(results, "No completion within smoke deadline")
        return results[-1]

    def test_box_progress_render_controls_and_failed_replacement(self):
        updates = []
        self.window.loader.progress.connect(lambda p: updates.append(p.phase))
        original = BOX.read_bytes()
        self.assertTrue(self.load(BOX))
        self.assertEqual(hashlib.sha256(original).hexdigest(), self.window.metadata["source_sha256"])
        self.assertEqual(original, BOX.read_bytes())
        for phase in ["capture", "check", "build", "mesh", "boundary", "display", "render"]:
            self.assertIn(phase, updates)
        self.assertEqual(100, self.window.progress_bar.value())
        self.assertFalse(self.window.cancel_button.isVisible())
        self.assertGreater(self.window.mesh.n_cells, 0)
        points = self.window.mesh.points.copy()
        for view in ["front", "top", "right", "isometric"]:
            self.window.named_view(view)
            self.assertGreater(self.window.plot.camera.clipping_range[0], 0)
            import numpy as np
            image = self.window.plot.screenshot(return_img=True, transparent_background=True)
            height, width = image.shape[:2]
            center_alpha = image[height//4:height*3//4, width//4:width*3//4, 3]
            self.assertGreater(np.count_nonzero(center_alpha > 240), 1000, "Empty native viewport")
        self.window.edges_button.toggle()
        self.window.wire_button.toggle()
        self.assertTrue((points == self.window.mesh.points).all())
        mesh = self.window.mesh
        self.assertFalse(self.load(BOX.with_name("absent.step")))
        self.assertIs(mesh, self.window.mesh)
        self.assertEqual(str(BOX), self.window.filename)
        self.assertEqual(0, self.window.progress_bar.value())

    def test_explicit_header_metadata_survives_worker_and_refused_replacement(self):
        from dataclasses import replace

        fixture = REPO / "source/STEP_IMPORT/tests/fixtures/mixed_ellipse_cylinder_solid.step"
        original = fixture.read_text(encoding="utf-8")
        header = original.index("FILE_NAME(")
        end = original.index(";", header)
        token = original.rindex("''", header, end)
        modified = original[:token] + "$" + original[token + 2:]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "public-unset-authorization.step"
            path.write_text(modified, encoding="utf-8")
            self.assertFalse(self.load(path))
            self.window.loader.settings = replace(
                self.window.loader.settings, preserve_unset_header_authorization=True)
            self.assertTrue(self.load(path))
            receipt = self.window.loader.worker_result['header_metadata_preservation']
            self.assertTrue(receipt['applied'])
            self.assertEqual([7], receipt['original_diagnostics'][0]['parameter_path'])
            mesh = self.window.mesh
            # A known later geometric refusal cannot replace the accepted scene;
            # the new request still owns its original header-error observation.
            off_surface = modified.replace("ELLIPSE('',#51,2.5,2.)", "ELLIPSE('',#51,2.6,2.)")
            self.assertNotEqual(off_surface, modified)
            path.write_text(off_surface, encoding="utf-8")
            self.assertFalse(self.load(path))
            self.assertIs(self.window.mesh, mesh)
            self.assertTrue(self.window.loader.worker_result['header_metadata_preservation']['applied'])
            self.assertTrue(receipt['original_diagnostics'])

    def test_cancel_and_stale_request_never_complete_at_100(self):
        self.assertFalse(self.load(BOX, self.window.loader.cancel))
        self.assertIsNone(self.window.mesh)
        self.assertEqual(0, self.window.progress_bar.value())
        # A new request can start while the old process is still stopping.
        self.assertTrue(self.load(BOX))
        self.assertEqual(str(BOX), self.window.filename)
        self.assertEqual(100, self.window.progress_bar.value())

    def test_cancel_at_last_progress_boundary_prevents_publication(self):
        def cancel_at_render(update):
            if update.phase == "render":
                self.window.loader.cancel()
        self.window.loader.progress.connect(cancel_at_render)
        self.assertFalse(self.load(BOX))
        self.assertIsNone(self.window.mesh)
        self.assertEqual(0, self.window.progress_bar.value())

    def test_mesh_file_and_bad_step_leave_previous_scene(self):
        import pyvista as pv
        with tempfile.TemporaryDirectory(prefix="brep-view-smoke-") as directory:
            mesh_file = Path(directory) / "公开网格.stl"
            pv.Cube().triangulate().save(mesh_file)
            self.assertTrue(self.load(mesh_file))
            self.assertEqual("unknown", self.window.metadata["units"])
            previous = self.window.mesh
            bad = Path(directory) / "invalid.step"
            bad.write_bytes(b"not a STEP file")
            self.assertFalse(self.load(bad))
            self.assertIs(previous, self.window.mesh)
            self.assertEqual(str(mesh_file), self.window.filename)
            self.assertEqual(0, self.window.progress_bar.value())
