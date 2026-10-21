# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Whole mixed requests through the real worker, with independent analytic truth."""
import copy
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
from PySide6 import QtWidgets

from tools.smlib_viewer.app import ViewerWindow
from tools.smlib_viewer.auxiliary import AuxiliaryScene, read_auxiliary
from tools.smlib_viewer.backend import native_worker
from tools.smlib_viewer.tests import test_viewer as viewer_tests

REPO = Path(__file__).resolve().parents[3]
SUPPORT = REPO / "source/STEP_IMPORT/tests/fixtures/supporting_geometry.step"
WIRE = REPO / "source/STEP_IMPORT/tests/fixtures/linked_wireframe.step"
BOX = REPO / "tools/step_import_validation/corpus/ap203e2-planar/v1/cases/S203-P04/input.step"


@unittest.skipUnless(native_worker().is_file(), "Set BREP_VIEW_WORKER")
class AuxiliaryViewerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.window = ViewerWindow()
        self.window.show()

    def tearDown(self):
        self.window.close()

    load = viewer_tests.ViewerTests.load

    def test_complete_wireframe_keeps_identity_direction_and_body(self):
        original = WIRE.read_bytes()
        self.assertTrue(self.load(WIRE), self.window.status.toolTip())
        self.assertEqual("brep-view-scene.v3", self.window.metadata["format"])
        auxiliary = self.window.metadata["_auxiliary"]
        self.assertEqual(dict(wireframe_groups=1, supporting_groups=0, curves=2, points=1, frames=0, planes=0),
                         auxiliary.counts)
        group = auxiliary.source["groups"][0]
        self.assertEqual(("920", "931", "910"),
                         (group["representation_id"], group["relationship_id"], group["set_id"]))
        self.assertFalse(group["material_boundary"])
        curves = {curve["source_id"]: curve for curve in group["curves"]}
        line = np.array(curves["912"]["polyline_mm"])
        np.testing.assert_allclose(line[[0, -1]], [[0, 0, 0], [5, 0, 0]], atol=1e-9, rtol=0)
        np.testing.assert_allclose(line[:, 1:], 0, atol=1e-9, rtol=0)
        arc = np.array(curves["940"]["polyline_mm"])
        np.testing.assert_allclose(arc[[0, -1]], [[2, 0, 0], [0, 2, 0]], atol=1e-9, rtol=0)
        np.testing.assert_allclose(np.linalg.norm(arc[:, :2], axis=1), 2, atol=1e-9, rtol=0)
        np.testing.assert_allclose(arc[:, 2], 0, atol=1e-9, rtol=0)
        self.assertEqual("911", group["points"][0]["source_id"])
        np.testing.assert_array_equal(group["points"][0]["point_mm"], [7, 8, 9])
        self.assertEqual((6, 12, 8), tuple(self.window.metadata[key] for key in ("faces", "edges", "vertices")))
        np.testing.assert_allclose(self.window.mesh.bounds, [2, 12, -3, 17, 5, 35], atol=1e-9, rtol=0)
        before = copy.deepcopy(auxiliary.source)
        self.window.edges_button.toggle()
        self.window.wire_button.toggle()
        self.assertTrue(all(actor.GetVisibility() for actor in self.window.auxiliary_actors))
        self.assertEqual(before, auxiliary.source)
        self.assertEqual(original, WIRE.read_bytes())
        self.assertEqual(hashlib.sha256(original).hexdigest(), self.window.metadata["source_sha256"])

    def test_unbounded_plane_and_frames_are_marked_display_proxies(self):
        original = SUPPORT.read_bytes()
        self.assertTrue(self.load(SUPPORT), self.window.status.toolTip())
        auxiliary = self.window.metadata["_auxiliary"]
        self.assertEqual(dict(wireframe_groups=0, supporting_groups=1, curves=2, points=1, frames=2, planes=1),
                         auxiliary.counts)
        group = auxiliary.source["groups"][0]
        self.assertEqual("ConstructiveSupportingGeometry", group["role"])
        frame = next(frame for frame in group["frames"] if frame["source_id"] == "970")["frame"]
        self.assertEqual(dict(origin_mm=[30, 40, 50], x=[0, 1, 0], y=[0, 0, 1], z=[1, 0, 0]), frame)
        plane = group["planes"][0]
        self.assertEqual(("972", "970", False), (plane["source_id"], plane["position_id"], plane["source_bounded"]))
        u0, v0, u1, v1 = plane["preview_uv_mm"]
        truth = [[30, 40+u0, 50+v0], [30, 40+u1, 50+v0], [30, 40+u1, 50+v1], [30, 40+u0, 50+v1]]
        np.testing.assert_allclose(plane["preview_corners_mm"], truth, atol=1e-9, rtol=0)
        self.assertIn("无限平面", self.window.hud.text())
        self.assertFalse(auxiliary.source["continuous_error_bound_established"])
        self.assertEqual((6, 12, 8), tuple(self.window.metadata[key] for key in ("faces", "edges", "vertices")))
        self.assertGreater(self.window._scene_bounds[5], self.window.mesh.bounds[5])
        self.assertEqual(original, SUPPORT.read_bytes())
        # A transport declaration cannot convert this unbounded support plane
        # into a bounded material face, nor alter native parameter coverage.
        bad = copy.deepcopy(auxiliary.source)
        bad["groups"][0]["planes"][0]["source_bounded"] = True
        with self.assertRaisesRegex(ValueError, "CannotBecomeBoundedMaterialFace"):
            AuxiliaryScene(bad)
        bad = copy.deepcopy(auxiliary.source)
        bad["groups"][0]["curves"][0]["parameters"][0] += 1
        with self.assertRaisesRegex(ValueError, "ParameterCoverage"):
            AuxiliaryScene(bad)

    def test_bad_required_auxiliary_never_publishes_body_only_replacement(self):
        self.assertTrue(self.load(BOX))
        previous = self.window.mesh
        filename = self.window.filename
        def corrupt_owned_payload(path):
            # This is the worker's disposable transport payload, not source
            # geometry. Simulate truncation after native whole-model acceptance.
            path.write_bytes(b"{}")
            return read_auxiliary(path)
        with patch("tools.smlib_viewer.auxiliary.read_auxiliary", side_effect=corrupt_owned_payload):
            self.assertFalse(self.load(SUPPORT))
        self.assertEqual("brep-view-scene.v3", self.window.loader.worker_result["format"])
        self.assertIn("InvalidAuxiliaryFormat", self.window.status.toolTip())
        self.assertIs(previous, self.window.mesh)
        self.assertEqual(filename, self.window.filename)

    def test_reverse_source_curve_retains_native_parameters_and_use_direction(self):
        original = WIRE.read_text(encoding="utf-8")
        before = "#912=TRIMMED_CURVE('',#913,(#914),(#915),.T.,.CARTESIAN.);"
        self.assertIn(before, original)
        reversed_source = original.replace(before, "#912=TRIMMED_CURVE('',#913,(#915),(#914),.F.,.CARTESIAN.);")
        with tempfile.TemporaryDirectory(prefix="viewer-public-reverse-") as name:
            filename = Path(name) / "reverse.step"
            filename.write_text(reversed_source, encoding="utf-8")
            self.assertTrue(self.load(filename))
            curves = self.window.metadata["_auxiliary"].source["groups"][0]["curves"]
            line = next(curve for curve in curves if curve["source_id"] == "912")
            self.assertFalse(line["source_forward_in_native"])
            self.assertEqual("Native", line["parameter_space"])
            np.testing.assert_allclose(np.array(line["polyline_mm"])[[0, -1]], [[5, 0, 0], [0, 0, 0]],
                                       atol=1e-9, rtol=0)
            self.assertTrue(np.all(np.diff(line["parameters"]) < 0))

    def test_rational_curve_keeps_nonunit_knot_domain(self):
        source = SUPPORT.read_text(encoding="utf-8")
        source = source.replace("(#904,#970,#972,#911,#912,#940)", "(#904,#970,#972,#911,#912,#940,#950)")
        records = (
            "#950=(BOUNDED_CURVE() B_SPLINE_CURVE(2,(#914,#951,#952),.UNSPECIFIED.,.F.,.F.) "
            "B_SPLINE_CURVE_WITH_KNOTS((3,3),(2.,5.),.UNSPECIFIED.) CURVE() GEOMETRIC_REPRESENTATION_ITEM() "
            "RATIONAL_B_SPLINE_CURVE((1.,0.5,1.)) REPRESENTATION_ITEM(''));\n"
            "#951=CARTESIAN_POINT('',(1.,2.,0.));\n#952=CARTESIAN_POINT('',(2.,0.,0.));\n")
        at = source.rindex("ENDSEC;")
        source = source[:at] + records + source[at:]
        with tempfile.TemporaryDirectory(prefix="viewer-public-rational-") as name:
            filename = Path(name) / "rational.step"
            filename.write_text(source, encoding="utf-8")
            self.assertTrue(self.load(filename), self.window.status.toolTip())
            curves = self.window.metadata["_auxiliary"].source["groups"][0]["curves"]
            curve = next(curve for curve in curves if curve["source_id"] == "950")
            np.testing.assert_array_equal(curve["native_domain"], [2, 5])
            t = (np.array(curve["parameters"]) - 2) / 3
            denominator = 1-t+t*t
            truth = np.column_stack(((t+t*t)/denominator, 2*t*(1-t)/denominator, np.zeros_like(t)))
            np.testing.assert_allclose(curve["polyline_mm"], truth, atol=1e-9, rtol=0)
