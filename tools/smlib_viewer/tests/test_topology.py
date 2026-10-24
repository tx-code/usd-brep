# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Native box truth, labelled VTK transport and atomic scene-adoption controls."""
import copy
from dataclasses import FrozenInstanceError
import hashlib
import itertools
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

os.environ.setdefault("QT_QPA_PLATFORM", "windows" if os.name == "nt" else "offscreen")
os.environ.setdefault("PYVISTA_OFF_SCREEN", "true")

import numpy as np
import pyvista as pv
from PySide6 import QtCore, QtWidgets

from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker
from tools.smlib_viewer.topology import MAX_TOPOLOGY_BYTES, TopologyData, read_topology

REPO = Path(__file__).resolve().parents[3]
BOX = REPO / "tools/step_import_validation/corpus/ap203e2-planar/v1/cases/S203-P04/input.step"
WIRE = REPO / "source/STEP_IMPORT/tests/fixtures/linked_wireframe.step"


def box_transport():
    """Author a 10 x 20 x 30 mm box independently of the native exporter.

    Subdivision deliberately adds display vertices. Its native topology still
    has eight corners, so a mesh-point substitution cannot satisfy the control.
    The real-worker test below checks the producer against the same analytic
    dimensions; this helper tests only transport and adoption, not import.
    """
    points = np.array([[2, -3, 5], [12, -3, 5], [12, 17, 5], [2, 17, 5],
                       [2, -3, 35], [12, -3, 35], [12, 17, 35], [2, 17, 35]], dtype=float)
    faces = [[0, 3, 2, 1], [4, 5, 6, 7], [0, 1, 5, 4],
             [1, 2, 6, 5], [2, 3, 7, 6], [3, 0, 4, 7]]
    mesh = pv.PolyData(points, np.array([[4, *face] for face in faces]).ravel())
    mesh.cell_data["TopoFace"] = np.arange(6, dtype=np.int32)
    mesh = mesh.triangulate().subdivide(1, subfilter="linear")
    pairs = [(0, 1), (1, 2), (2, 3), (3, 0), (4, 5), (5, 6), (6, 7), (7, 4),
             (0, 4), (1, 5), (2, 6), (3, 7)]
    boundary = pv.PolyData(points, lines=np.array([[2, a, b] for a, b in pairs]).ravel())
    boundary.cell_data["TopoEdge"] = np.arange(12, dtype=np.int32)
    topology = dict(
        format="brep-view-topology.v1", units="mm", identity_scope="NativeDisplayCopy",
        face_labels_complete=True, face_area_relative_accuracy=1e-4,
        edge_length_absolute_accuracy_mm=1e-5, continuous_error_bound_established=False,
        faces=[dict(id=index, area_mm2=area, measurement_status="Available")
               for index, area in enumerate([200, 200, 300, 600, 300, 600])],
        edges=[dict(id=index, length_mm=length, measurement_status="Available")
               for index, length in enumerate([10, 20, 10, 20, 10, 20, 10, 20, 30, 30, 30, 30])],
        vertices=[dict(id=index, point_mm=point.tolist()) for index, point in enumerate(points)])
    manifest = dict(format="brep-view-scene.v3", status="Ready", units="mm", mesh="mesh.vtk",
                    boundary="edges.vtk", topology="topology.json", auxiliary_required=False,
                    faces=6, edges=12, vertices=8, triangles=mesh.n_cells, points=mesh.n_points)
    return mesh, boundary, topology, manifest


def point_auxiliary():
    # An explicitly separate supporting point is outside the material box.
    return dict(format="brep-view-auxiliary.v1", units="mm", continuous_error_bound_established=False,
                preview_scale_mm=5, groups=[dict(
                    role="ConstructiveSupportingGeometry", material_boundary=False,
                    representation_id="920", relationship_id="931", curves=[], frames=[], planes=[],
                    points=[dict(source_id="911", point_mm=[70, 80, 90])])])


class TopologyTransportTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="viewer-public-topology-")
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "topology.json"
        self.mesh, self.boundary, self.topology, self.manifest = box_transport()

    def read(self):
        self.path.write_text(json.dumps(self.topology), encoding="utf-8")
        return read_topology(self.path, self.mesh, self.boundary, self.manifest)

    def test_owned_native_tables_do_not_become_tessellation_vertices(self):
        data = self.read()
        self.assertIsInstance(data, TopologyData)
        self.assertEqual((6, 12, 8), (len(data.faces), len(data.edges), len(data.vertices)))
        self.assertGreater(self.mesh.n_points, len(data.vertices))
        self.assertEqual([200, 200, 300, 300, 600, 600], sorted(face.area_mm2 for face in data.faces))
        self.assertEqual([10] * 4 + [20] * 4 + [30] * 4, sorted(edge.length_mm for edge in data.edges))
        self.assertEqual(set(itertools.product((2., 12.), (-3., 17.), (5., 35.))),
                         {vertex.point_mm for vertex in data.vertices})
        self.assertEqual("NativeDisplayCopy", data.identity_scope)
        self.assertFalse(data.continuous_error_bound_established)
        with self.assertRaises(FrozenInstanceError):
            data.vertices[0].point_mm = (0, 0, 0)
        self.path.unlink()
        np.testing.assert_array_equal(data.vertices[0].point_mm, [2, -3, 5])

    def test_unavailable_measurements_retain_distinct_reasons(self):
        self.topology["faces"][0].update(area_mm2=None, measurement_status="NativeQueryFailed")
        self.topology["edges"][0].update(length_mm=None, measurement_status="MeasurementBudget")
        data = self.read()
        self.assertIsNone(data.faces[0].area_mm2)
        self.assertEqual("NativeQueryFailed", data.faces[0].measurement_status)
        self.assertIsNone(data.edges[0].length_mm)
        self.assertEqual("MeasurementBudget", data.edges[0].measurement_status)
        self.topology["edges"][0]["length_mm"] = 0
        with self.assertRaisesRegex(ValueError, "InvalidTopologyMeasurement"):
            self.read()

    def test_dense_identity_and_counts_are_required(self):
        for value in (True, -1, 1, 0.5, "0"):
            with self.subTest(id=value):
                self.topology["vertices"][0]["id"] = value
                with self.assertRaisesRegex(ValueError, "InvalidTopologyIdentity"):
                    self.read()
        self.topology["vertices"][0]["id"] = 0
        self.manifest["vertices"] = 9
        with self.assertRaisesRegex(ValueError, "InvalidTopologyInventory"):
            self.read()

    def test_numbers_and_measurement_status_cannot_invent_evidence(self):
        original = copy.deepcopy(self.topology)
        for field, value in (("area_mm2", None), ("area_mm2", -1), ("area_mm2", float("inf")),
                             ("area_mm2", True), ("measurement_status", "MeshEstimate")):
            with self.subTest(field=field, value=value):
                self.topology = copy.deepcopy(original)
                self.topology["faces"][0][field] = value
                with self.assertRaises(ValueError):
                    self.read()
        self.topology = original
        self.topology["vertices"][0]["point_mm"] = [2, float("nan"), 5]
        with self.assertRaisesRegex(ValueError, "InvalidTopologyNumber"):
            self.read()

    def test_format_units_scope_and_bound_claim_are_explicit(self):
        original = copy.deepcopy(self.topology)
        for key, value in (("format", "brep-view-topology.v2"), ("units", "unknown"),
                           ("identity_scope", "STEP"), ("continuous_error_bound_established", True),
                           ("face_labels_complete", 1), ("face_area_relative_accuracy", 0),
                           ("edge_length_absolute_accuracy_mm", float("nan"))):
            with self.subTest(key=key):
                self.topology = {**original, key: value}
                with self.assertRaises(ValueError):
                    self.read()

    def test_only_integer_cell_labels_reference_existing_native_entities(self):
        for name, dataset, bad in (("TopoFace", self.mesh, 6), ("TopoFace", self.mesh, -2),
                                   ("TopoEdge", self.boundary, 12), ("TopoEdge", self.boundary, -1)):
            with self.subTest(name=name, value=bad):
                saved = dataset.cell_data[name].copy()
                dataset.cell_data[name][0] = bad
                with self.assertRaisesRegex(ValueError, "InvalidTopologyLabelRange"):
                    self.read()
                dataset.cell_data[name] = saved
        saved = self.mesh.cell_data["TopoFace"].copy()
        self.mesh.cell_data["TopoFace"] = saved.astype(float)
        with self.assertRaisesRegex(ValueError, "InvalidTopologyLabels"):
            self.read()
        del self.mesh.cell_data["TopoFace"]
        self.mesh.point_data["TopoFace"] = np.zeros(self.mesh.n_points, dtype=np.int32)
        with self.assertRaisesRegex(ValueError, "MissingTopologyLabels"):
            self.read()

    def test_unknown_faces_stay_unselectable_without_false_completeness(self):
        self.mesh.cell_data["TopoFace"][0] = -1
        with self.assertRaisesRegex(ValueError, "InvalidTopologyFaceCompleteness"):
            self.read()
        self.topology["face_labels_complete"] = False
        self.assertFalse(self.read().face_labels_complete)

    def test_multiple_boundary_cells_may_share_one_native_edge(self):
        repeated = self.boundary.copy(deep=True)
        self.boundary = pv.merge([self.boundary, repeated], merge_points=False)
        data = self.read()
        self.assertEqual(12, len(data.edges))
        self.assertEqual(24, self.boundary.n_cells)

    def test_missing_polyline_for_a_degenerate_native_edge_is_not_dropped_from_table(self):
        self.topology["edges"].append(dict(id=12, length_mm=0, measurement_status="Available"))
        self.manifest["edges"] = 13
        data = self.read()
        self.assertEqual(13, len(data.edges))
        self.assertEqual(0, data.edges[12].length_mm)
        self.assertNotIn(12, self.boundary.cell_data["TopoEdge"])

    def test_duplicate_json_fields_and_oversize_report_are_rejected(self):
        self.path.write_text('{"format":"brep-view-topology.v1","format":"other"}', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "DuplicateTopologyField"):
            read_topology(self.path, self.mesh, self.boundary, self.manifest)
        with self.path.open("wb") as stream:
            stream.seek(MAX_TOPOLOGY_BYTES)
            stream.write(b" ")
        with self.assertRaisesRegex(ValueError, "TopologyReportLimit"):
            read_topology(self.path, self.mesh, self.boundary, self.manifest)


class TopologyAdoptionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.loader = SceneLoader(ImportSettings())
        self.addCleanup(self.loader.close)
        self.loaded = []
        self.failed = []
        self.loader.loaded.connect(lambda *args: self.loaded.append(args))
        self.loader.failed.connect(lambda *args: self.failed.append(args))

    def request(self, *, version="v3", auxiliary=False):
        directory = tempfile.TemporaryDirectory(prefix="viewer-public-adoption-")
        self.addCleanup(directory.cleanup)
        path = Path(directory.name)
        mesh, boundary, topology, manifest = box_transport()
        mesh.save(path / "mesh.vtk")
        boundary.save(path / "edges.vtk")
        (path / "topology.json").write_text(json.dumps(topology), encoding="utf-8")
        manifest["format"] = "brep-view-scene." + version
        if version != "v3":
            del manifest["topology"]
            del manifest["auxiliary_required"]
        if auxiliary:
            manifest["auxiliary"] = "auxiliary.json"
            if version == "v3":
                manifest["auxiliary_required"] = True
            (path / "auxiliary.json").write_text(json.dumps(point_auxiliary()), encoding="utf-8")
        generation = self.loader.generation
        process = Mock()
        process.state.return_value = QtCore.QProcess.NotRunning
        self.loader.jobs[generation] = dict(directory=directory, path=BOX, stopped=False,
                                            guard=None, timer=Mock(), process=process)
        self.write_manifest(path, manifest)
        return generation, path, manifest

    @staticmethod
    def write_manifest(path, manifest):
        (path / "scene.json").write_text(json.dumps(manifest), encoding="utf-8")

    def test_complete_v3_adopts_topology_and_auxiliary_together(self):
        generation, path, _ = self.request(auxiliary=True)
        self.loader._adopt(generation, 0)
        self.assertFalse(self.failed)
        self.assertEqual(1, len(self.loaded))
        metadata = self.loaded[0][3]
        self.assertIsInstance(metadata["_topology"], TopologyData)
        self.assertEqual(8, len(metadata["_topology"].vertices))
        self.assertEqual(1, metadata["_auxiliary"].counts["points"])
        self.assertEqual([70, 80, 90], metadata["_auxiliary"].source["groups"][0]["points"][0]["point_mm"])
        self.assertEqual(dict(faces=6, edges=12, vertices=8, face_labels_complete=True),
                         self.loader.worker_result["topology_inventory"])
        self.assertFalse(path.exists(), "The complete scene must own its data after worker cleanup")

    def test_known_face_orientation_refusal_has_a_precise_caption_and_retains_prior_scene(self):
        generation, _, _ = self.request()
        self.loader._adopt(generation, 0)
        previous = self.loaded[-1]
        for classification, title in (('NativeFaceLoopOrientationFailure', '面环方向检查未通过'),
                                      ('NativePeriodicSourceVertexNearMiss', '源顶点周期连接检查未通过'),
                                      (None, '模型未能载入')):
            generation, path, manifest = self.request()
            manifest.update(status='PostcheckFailure', key='ImportFailed', diagnostics=[dict(
                key='GeneralPointerValidation', message='Synthetic native refusal', classification=classification)])
            self.write_manifest(path, manifest)
            self.loader._adopt(generation, 1)
            self.assertEqual(title, self.failed[-1][0])
            self.assertIn('GeneralPointerValidation', self.failed[-1][1])
            self.assertIs(previous, self.loaded[-1])
            self.assertEqual(1, len(self.loaded))
            self.assertFalse(path.exists())

        generation, path, manifest = self.request()
        manifest.update(status='PostcheckFailure', key='ImportFailed', diagnostics=[
            dict(key='GeneralRegionSeamUnestablished', message='Synthetic near miss',
                 classification='NativePeriodicSourceVertexNearMiss'),
            dict(key='GeneralDerivedUvGap', message='Synthetic additional failure')])
        self.write_manifest(path, manifest)
        self.loader._adopt(generation, 1)
        self.assertEqual('模型未能载入', self.failed[-1][0], 'mixed findings must keep the generic caption')
        self.assertIs(previous, self.loaded[-1])

    def test_legacy_v1_and_required_auxiliary_v2_remain_compatible(self):
        for version, auxiliary in (("v1", False), ("v2", True)):
            with self.subTest(version=version):
                self.loader.generation += 1
                generation, _, _ = self.request(version=version, auxiliary=auxiliary)
                self.loader._adopt(generation, 0)
                self.assertFalse(self.failed)
                metadata = self.loaded[-1][3]
                self.assertNotIn("_topology", metadata)
                self.assertEqual(auxiliary, "_auxiliary" in metadata)

    def test_required_payload_failures_never_replace_the_published_scene(self):
        generation, _, _ = self.request()
        self.loader._adopt(generation, 0)
        previous = self.loaded[-1]
        for payload in ("topology.json", "auxiliary.json"):
            for corrupt in (False, True):
                with self.subTest(payload=payload, corrupt=corrupt):
                    self.loader.generation += 1
                    generation, path, _ = self.request(auxiliary=True)
                    if corrupt:
                        (path / payload).write_text("{}", encoding="utf-8")
                    else:
                        (path / payload).unlink()
                    self.loader._adopt(generation, 0)
                    self.assertIs(previous, self.loaded[-1])
                    self.assertEqual(1, len(self.loaded))
                    self.assertFalse(path.exists())
        self.assertEqual(4, len(self.failed))

    def test_v3_auxiliary_requirement_is_boolean_and_matches_descriptor(self):
        for required, descriptor in ((None, False), (1, False), (False, True), (True, False)):
            with self.subTest(required=required, descriptor=descriptor):
                self.loader.generation += 1
                generation, path, manifest = self.request()
                manifest["auxiliary_required"] = required
                if descriptor:
                    manifest["auxiliary"] = "auxiliary.json"
                self.write_manifest(path, manifest)
                self.loader._adopt(generation, 0)
        self.assertFalse(self.loaded)
        self.assertEqual(4, len(self.failed))

    def test_stale_or_cancelled_request_never_publishes_any_metadata(self):
        generation, path, _ = self.request()
        self.loader.generation += 1
        self.loader._adopt(generation, 0)
        self.assertFalse(self.loaded)
        self.assertFalse(path.exists())
        generation, path, _ = self.request(auxiliary=True)

        def cancel_at_render(update):
            if update.phase == "render":
                self.loader.cancel(notify=False)

        self.loader.progress.connect(cancel_at_render)
        self.loader._adopt(generation, 0)
        self.assertFalse(self.loaded)
        self.assertFalse(self.failed)
        self.assertFalse(path.exists())

    def test_topology_path_cannot_escape_owned_request(self):
        generation, path, manifest = self.request()
        manifest["topology"] = "../topology.json"
        self.write_manifest(path, manifest)
        self.loader._adopt(generation, 0)
        self.assertFalse(self.loaded)
        self.assertIn("InvalidScenePath", self.failed[-1][1])


@unittest.skipUnless(native_worker().is_file(), "Set BREP_VIEW_WORKER to the built step_view_export")
class NativeTopologyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.loader = SceneLoader(ImportSettings())
        self.addCleanup(self.close_loader)

    def close_loader(self):
        self.loader.close()
        # Deliver QObject deferred destruction before this test releases Python
        # wrappers; the next event loop must not inherit a prior QProcess tree.
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)

    def load(self, source):
        loop = QtCore.QEventLoop()
        results, errors = [], []

        def loaded(*args):
            results.append(args)
            loop.quit()

        def failed(*args):
            errors.append(args)
            loop.quit()

        self.loader.loaded.connect(loaded)
        self.loader.failed.connect(failed)
        timer = QtCore.QTimer()
        timer.setSingleShot(True)
        timer.timeout.connect(loop.quit)
        timer.start(30000)
        try:
            self.loader.load(str(source))
            if not results and not errors:
                loop.exec()
        finally:
            self.loader.loaded.disconnect(loaded)
            self.loader.failed.disconnect(failed)
            timer.stop()
        self.assertFalse(errors, str(errors))
        self.assertTrue(results, "No complete native scene before the public smoke deadline")
        return results[-1]

    def test_real_box_uses_native_area_length_corners_and_valid_cell_provenance(self):
        original = BOX.read_bytes()
        _, mesh, boundary, metadata = self.load(BOX)
        self.assertEqual("brep-view-scene.v3", metadata["format"])
        topology = metadata["_topology"]
        self.assertTrue(topology.face_labels_complete)
        self.assertEqual(set(range(6)), set(mesh.cell_data["TopoFace"]))
        self.assertEqual(set(range(12)), set(boundary.cell_data["TopoEdge"]))
        self.assertEqual((6, 12, 8), (len(topology.faces), len(topology.edges), len(topology.vertices)))
        self.assertTrue(all(face.measurement_status == "Available" for face in topology.faces))
        self.assertTrue(all(edge.measurement_status == "Available" for edge in topology.edges))
        np.testing.assert_allclose(sorted(face.area_mm2 for face in topology.faces),
                                   [200, 200, 300, 300, 600, 600], rtol=1e-4, atol=0)
        np.testing.assert_allclose(sorted(edge.length_mm for edge in topology.edges),
                                   [10] * 4 + [20] * 4 + [30] * 4, rtol=0, atol=1e-5)
        # Check the labels' association too: each selected face's display
        # triangles lie on one complete analytic box plane, whose independent
        # rectangular area must match that same native face's measurement.
        triangles = mesh.faces.reshape(-1, 4)[:, 1:]
        for face in topology.faces:
            coordinates = mesh.points[np.unique(triangles[mesh.cell_data["TopoFace"] == face.id])]
            extent = np.ptp(coordinates, axis=0)
            self.assertEqual(1, np.count_nonzero(np.isclose(extent, 0, atol=1e-9, rtol=0)))
            positive = extent[extent > 1e-9]
            self.assertAlmostEqual(float(np.prod(positive)), face.area_mm2, delta=face.area_mm2 * 1e-4)
        for cell_index, edge_id in enumerate(boundary.cell_data["TopoEdge"]):
            coordinates = boundary.get_cell(cell_index).points
            extent = np.ptp(coordinates, axis=0)
            self.assertEqual(2, np.count_nonzero(np.isclose(extent, 0, atol=1e-9, rtol=0)))
            self.assertAlmostEqual(float(np.linalg.norm(coordinates[-1] - coordinates[0])),
                                   topology.edges[int(edge_id)].length_mm, delta=1e-5)
        self.assertEqual(set(itertools.product((2., 12.), (-3., 17.), (5., 35.))),
                         {vertex.point_mm for vertex in topology.vertices})
        self.assertEqual(hashlib.sha256(original).hexdigest(), metadata["source_sha256"])
        self.assertEqual(original, BOX.read_bytes())

    def test_real_mixed_v3_cannot_publish_without_its_required_auxiliary(self):
        _, _, _, metadata = self.load(WIRE)
        self.assertTrue(metadata["auxiliary_required"])
        self.assertEqual(8, len(metadata["_topology"].vertices))
        self.assertEqual(2, metadata["_auxiliary"].counts["curves"])
        from tools.smlib_viewer.auxiliary import read_auxiliary

        def corrupt_payload(path):
            path.write_bytes(b"{}")
            return read_auxiliary(path)

        loop = QtCore.QEventLoop()
        failed, loaded = [], []
        self.loader.failed.connect(lambda *args: (failed.append(args), loop.quit()))
        self.loader.loaded.connect(lambda *args: (loaded.append(args), loop.quit()))
        timer = QtCore.QTimer()
        timer.setSingleShot(True)
        timer.timeout.connect(loop.quit)
        timer.start(30000)
        with patch("tools.smlib_viewer.auxiliary.read_auxiliary", side_effect=corrupt_payload):
            self.loader.load(str(WIRE))
            if not failed and not loaded:
                loop.exec()
        self.assertTrue(failed)
        self.assertFalse(loaded)
        self.assertIn("InvalidAuxiliaryFormat", failed[-1][1])

    def test_source_pcurve_pole_model_keeps_native_measurement_and_pick_tables(self):
        path = REPO / 'source/STEP_IMPORT/tests/fixtures/source_pcurve_revolution_pole_driven_patch.step'
        original = path.read_bytes()
        _, mesh, boundary, metadata = self.load(path)
        self.assertEqual('Accepted', metadata['native_import_status'])
        self.assertEqual((1, 3, 3), tuple(metadata[key] for key in ('faces', 'edges', 'vertices')))
        topology = metadata['_topology']
        self.assertTrue(topology.face_labels_complete)
        self.assertEqual({0}, set(mesh.cell_data['TopoFace']))
        self.assertEqual({0, 1, 2}, set(boundary.cell_data['TopoEdge']))
        self.assertAlmostEqual(8.1, topology.faces[0].area_mm2, delta=.001)
        self.assertEqual(original, path.read_bytes())
