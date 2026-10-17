# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Real CLI contracts for folded and coplanar sheets, independently specified."""
from fractions import Fraction as F
from copy import deepcopy
import json
import itertools
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tools.step_import_native_validation.oracle import accepted
from tools.step_import_validation.oracle import decode, exact, sqrt_interval
from tools.step_import_validation.records import aggregate, by_id, inventory, ref, vector as source_vector

SHEET = Path(__file__).resolve().parents[3] / "source/STEP_IMPORT/tests/fixtures/open_shared_sheet.step"
COPLANAR_SHEET = SHEET.with_name("coplanar_shared_sheet.step")
CONVEX_SHEET = SHEET.with_name("convex_shared_sheet.step")
TRIANGULAR_SHEET = SHEET.with_name("triangular_shared_sheet.step")
PENTAGONAL_SHEET = SHEET.with_name("pentagonal_shared_sheet.step")
# P0 S1 analytic truth: face areas 12 and 6; unique edge lengths total 21.
# Area moments: (48, 27, 6); no CAD-generated expectations.
TRUTH = {"role": "SurfaceModel", "source_topology": {"faces": 2, "edges": 7, "vertices": 6},
         "measures": {"area": exact(18), "perimeter": exact(21)},
         "centroid": [exact(F(8, 3)), exact(F(3, 2)), exact(F(1, 3))],
         "bounds": [[exact(0), exact(0), exact(0)], [exact(4), exact(3), exact(2)]]}
COPLANAR_TRUTH = {"role": "SurfaceModel", "source_topology": {"faces": 2, "edges": 7, "vertices": 6},
                  "measures": {"area": exact(18), "perimeter": exact(21)},
                  "centroid": [exact(3), exact(F(3, 2)), exact(0)],
                  "bounds": [[exact(0), exact(0), exact(0)], [exact(6), exact(3), exact(0)]]}


def radical_length(base, squared, multiplier=1):
    bounds = sqrt_interval(F(squared))
    low, high = base + multiplier * decode(bounds["lower"]), base + multiplier * decode(bounds["upper"])
    return exact((low + high) / 2), [exact(low), exact(high)]


_trapezoid_length, _trapezoid_bounds = radical_length(20, 5)
CONVEX_TRUTH = {"role": "SurfaceModel", "source_topology": {"faces": 2, "edges": 7, "vertices": 6},
                "measures": {"area": exact(19), "perimeter": _trapezoid_length},
                "perimeter_bounds": _trapezoid_bounds,
                "centroid": [exact(F(52, 19)), exact(F(91, 57)), exact(F(22, 57))],
                "bounds": [[exact(0), exact(0), exact(0)], [exact(4), exact(4), exact(2)]]}
_triangle_length, _triangle_bounds = radical_length(14, 13)
TRIANGULAR_TRUTH = {"role": "SurfaceModel", "source_topology": {"faces": 2, "edges": 5, "vertices": 4},
                    "measures": {"area": exact(9), "perimeter": _triangle_length},
                    "perimeter_bounds": _triangle_bounds,
                    "centroid": [exact(F(28, 9)), exact(1), exact(F(2, 9))],
                    "bounds": [[exact(0), exact(0), exact(0)], [exact(4), exact(3), exact(2)]]}
_pentagon_length, _pentagon_bounds = radical_length(16, 5, 3)
PENTAGONAL_TRUTH = {"role": "SurfaceModel", "source_topology": {"faces": 2, "edges": 8, "vertices": 7},
                    "measures": {"area": exact(21), "perimeter": _pentagon_length},
                    "perimeter_bounds": _pentagon_bounds,
                    "centroid": [exact(F(20, 7)), exact(F(103, 63)), exact(F(4, 7))],
                    "bounds": [[exact(0), exact(0), exact(0)], [exact(4), exact(4), exact(3)]]}


class SheetTests(unittest.TestCase):
    def polygon_checked(self, observed, data, truth):
        accepted(observed, data, truth)
        if "perimeter_bounds" in truth:
            actual = F(observed["native_measurements"]["boundary_mm"])
            self.assertLessEqual(max(abs(actual - decode(v)) for v in truth["perimeter_bounds"]), F(1, 10**7))

    def unfolded(self, data, side=1):
        index = by_id(inventory(data))
        directions, points = {710, 711}, set()
        for token in aggregate(index[715].args[1]):
            edge = index[ref(index[ref(token)].args[3])]
            vector = index[ref(index[ref(edge.args[3])].args[2])]
            directions.add(ref(vector.args[1]))
            points.update(ref(index[ref(v)].args[1]) for v in edge.args[1:3])
        for record in index.values():
            if record.entity_id not in directions | points:
                continue
            x, y, z = source_vector(record.args[1])
            value = (side*z, y, -side*x) if record.kind == "DIRECTION" else (4+side*z, y, -side*(x-4))
            body = record.kind + "('',(" + ",".join(str(float(v)) for v in value) + "))"
            data = self.replace(data, f"#{record.entity_id}={record.raw};", f"#{record.entity_id}={body};")
        return data

    def test_coplanar_convex_faces_remain_separate_without_overlap(self):
        for fixture, base_truth, center_x, xmax in (
                (CONVEX_SHEET, CONVEX_TRUTH, F(178, 57), 6),
                (TRIANGULAR_SHEET, TRIANGULAR_TRUTH, F(10, 3), 6),
                (PENTAGONAL_SHEET, PENTAGONAL_TRUTH, F(24, 7), 7)):
            data = self.unfolded(fixture.read_bytes())
            truth = deepcopy(base_truth)
            truth["centroid"] = [exact(center_x), base_truth["centroid"][1], exact(0)]
            truth["bounds"][1] = [exact(xmax), base_truth["bounds"][1][1], exact(0)]
            with self.subTest(fixture=fixture.name):
                actual, observed = self.invoke(data, "--query", "4.5", "1", "-1")
                self.assertEqual(0, actual.returncode, observed.get("diagnostics"))
                self.polygon_checked(observed["import"], data, truth)
                self.assertEqual([4.5, 1, 0], observed["query"]["closest_point_mm"])
                self.assertAlmostEqual(1, observed["query"]["distance_mm"], delta=1e-7)

    def test_same_side_coplanar_convex_faces_do_not_enter_native(self):
        for fixture in (CONVEX_SHEET, TRIANGULAR_SHEET, PENTAGONAL_SHEET):
            data = self.unfolded(fixture.read_bytes(), -1)
            with self.subTest(fixture=fixture.name):
                actual, observed = self.invoke(data)
                self.assertEqual(2, actual.returncode)
                self.assertEqual("Checked", observed["source"]["planar"])
                self.assertEqual("UnsupportedNativeOpenSheetScope", observed["diagnostics"][0]["key"])
                self.assertEqual(("Unsupported", 0), (observed["status"], observed["native_calls"]))
                self.assertIsNone(observed["model"])

    def test_convex_pentagon_keeps_every_vertex_and_use(self):
        data = PENTAGONAL_SHEET.read_bytes()
        actual, observed = self.invoke(data, "--query", "5", "2", "2")
        self.assertEqual(0, actual.returncode, observed.get("diagnostics"))
        self.polygon_checked(observed["import"], data, PENTAGONAL_TRUTH)
        self.assertEqual((7, 90), (observed["import"]["native_measurements"]["free_boundary_edges"],
                                  len(observed["import"]["tolerances"])))
        self.assertEqual(9, sum(len(row["uses"]) for row in observed["import"]["loop_mappings"]))
        self.assertEqual([4, 2, 2], observed["query"]["closest_point_mm"])
        self.assertAlmostEqual(1, observed["query"]["distance_mm"], delta=1e-7)

    def test_valid_concave_or_collinear_boundaries_do_not_get_simplified(self):
        for y, z in ((2, 1), (2, 2)):
            data = PENTAGONAL_SHEET.read_bytes()
            changes = [("#105=CARTESIAN_POINT('',(4.,2.,3.))", f"#105=CARTESIAN_POINT('',(4.,{float(y)},{float(z)}))"),
                       ("#305=DIRECTION('',(0.,2.,1.))", f"#305=DIRECTION('',(0.,{float(y)},{float(z-2)}))"),
                       ("#306=DIRECTION('',(0.,2.,-1.))", f"#306=DIRECTION('',(0.,{float(4-y)},{float(2-z)}))")]
            for old, new in changes:
                data = self.replace(data, old, new)
            with self.subTest(shape="concave" if z == 1 else "collinear"):
                actual, observed = self.invoke(data)
                self.assertEqual(2, actual.returncode)
                self.assertEqual("Checked", observed["source"]["planar"])
                self.assertEqual(("Unsupported", 0, "None"),
                                 (observed["status"], observed["native_calls"], observed["repair"]))
                self.assertEqual("UnsupportedNativeOpenSheetScope", observed["diagnostics"][0]["key"])
                self.assertIsNone(observed["model"])

    def test_convex_faces_preserve_shape_in_an_oblique_world_frame(self):
        for fixture, base_truth, xmax, ymax in ((CONVEX_SHEET, CONVEX_TRUTH, F(11), 2),
                                               (TRIANGULAR_SHEET, TRIANGULAR_TRUTH, F(11), 1),
                                               (PENTAGONAL_SHEET, PENTAGONAL_TRUTH, F(59, 5), 2)):
            data = fixture.read_bytes()
            for record in inventory(data):
                if record.kind not in {"CARTESIAN_POINT", "DIRECTION"}:
                    continue
                x, y, z = source_vector(record.args[1])
                value = (F(3, 5)*x + F(4, 5)*z, y, -F(4, 5)*x + F(3, 5)*z)
                if record.kind == "CARTESIAN_POINT":
                    value = tuple(a+b for a, b in zip(value, (7, -2, 5)))
                body = record.kind + "('',(" + ",".join(str(float(v)) for v in value) + "))"
                data = self.replace(data, f"#{record.entity_id}={record.raw};", f"#{record.entity_id}={body};")
            truth = deepcopy(base_truth)
            x, y, z = map(decode, base_truth["centroid"])
            truth["centroid"] = [exact(7 + F(3, 5)*x + F(4, 5)*z), exact(y-2),
                                 exact(5 - F(4, 5)*x + F(3, 5)*z)]
            truth["bounds"] = [[exact(7), exact(-2), exact(F(9, 5))],
                               [exact(xmax), exact(ymax), exact(5)]]
            with self.subTest(fixture=fixture.name):
                actual, observed = self.invoke(data, "--query", "10.4", "-1", "1.3")
                self.assertEqual(0, actual.returncode, observed.get("diagnostics"))
                self.polygon_checked(observed["import"], data, truth)
                for a, b in zip(observed["query"]["closest_point_mm"], (9.8, -1, 2.1)):
                    self.assertAlmostEqual(a, b, delta=1e-7)
                self.assertAlmostEqual(1, observed["query"]["distance_mm"], delta=1e-7)

    def test_independent_checker_rejects_lost_convex_geometry(self):
        for fixture, truth in ((CONVEX_SHEET, CONVEX_TRUTH), (TRIANGULAR_SHEET, TRIANGULAR_TRUTH),
                                (PENTAGONAL_SHEET, PENTAGONAL_TRUTH)):
            data = fixture.read_bytes()
            actual, observed = self.invoke(data)
            self.assertEqual(0, actual.returncode)
            self.polygon_checked(observed, data, truth)
            for defect in ("vertex", "use", "area", "centroid", "uv", "material"):
                bad = deepcopy(observed)
                if defect == "vertex":
                    bad["native_measurements"]["vertices"] -= 1
                elif defect == "use":
                    bad["loop_mappings"][1]["uses"].pop()
                elif defect == "area":
                    bad["native_measurements"]["area_mm2"] = 18
                elif defect == "centroid":
                    bad["native_measurements"]["centroid_mm"] = [2, 1.5, 0]
                elif defect == "uv":
                    bad["loop_mappings"][1]["uses"][0]["uv_end_mm"] = [99, 99]
                else:
                    bad["model"]["role"] = "MaterialSolid"
                with self.subTest(fixture=fixture.name, defect=defect), self.assertRaises(AssertionError):
                    self.polygon_checked(bad, data, truth)

    def replace(self, data, old, new):
        old, new = old.encode("ascii"), new.encode("ascii")
        self.assertEqual(1, data.count(old), old)
        return data.replace(old, new)

    def invoke(self, data=None, *options):
        binary = Path(os.environ["STEP_NATIVE_BINARY"]).resolve(strict=True)
        with tempfile.TemporaryDirectory(prefix="step-open-sheet-") as root:
            path = Path(root) / "input.step"
            path.write_bytes(data if data is not None else SHEET.read_bytes())
            actual = subprocess.run([str(binary), "--fixture-limits", "--quality", "Q-P0-mm-01", str(path), *options],
                                    capture_output=True, timeout=30)
            return actual, json.loads(actual.stdout)

    def test_coplanar_rectangles_on_the_same_side_are_not_accepted(self):
        for width in (F(1, 2), F(1), F(2), F(4), F(6)):
            data = COPLANAR_SHEET.read_bytes()
            for old, new in (("#104=CARTESIAN_POINT('',(6.,0.,0.))",
                             f"#104=CARTESIAN_POINT('',({float(4-width)},0.,0.))"),
                            ("#105=CARTESIAN_POINT('',(6.,3.,0.))",
                             f"#105=CARTESIAN_POINT('',({float(4-width)},3.,0.))"),
                            ("#304=DIRECTION('',(1.,0.,0.))", "#304=DIRECTION('',(-1.,0.,0.))"),
                            ("#306=DIRECTION('',(-1.,0.,0.))", "#306=DIRECTION('',(1.,0.,0.))"),
                            ("#710=DIRECTION('',(0.,0.,1.))", "#710=DIRECTION('',(0.,0.,-1.))"),
                            ("#711=DIRECTION('',(1.,0.,0.))", "#711=DIRECTION('',(-1.,0.,0.))"),
                            ("#404=VECTOR('',#304,2.)", f"#404=VECTOR('',#304,{float(width)})"),
                            ("#406=VECTOR('',#306,2.)", f"#406=VECTOR('',#306,{float(width)})")):
                data = self.replace(data, old, new)
            with self.subTest(width=width):
                actual, observed = self.invoke(data)
                self.assertEqual(2, actual.returncode)
                self.assertEqual("Checked", observed["source"]["planar"])
                self.assertEqual("Unsupported", observed["status"])
                self.assertEqual("UnsupportedNativeOpenSheetScope", observed["diagnostics"][0]["key"])
                self.assertIsNone(observed["model"])
                self.assertEqual((0, False, "None"),
                                 (observed["native_calls"], observed["candidate_discarded"], observed["repair"]))

    def test_coplanar_second_face_retains_a_rotated_shifted_uv_frame(self):
        data = COPLANAR_SHEET.read_bytes()
        data = self.replace(data, "#712=AXIS2_PLACEMENT_3D('',#101,#710,#711)",
                            "#712=AXIS2_PLACEMENT_3D('',#105,#710,#711)")
        data = self.replace(data, "#711=DIRECTION('',(1.,0.,0.))", "#711=DIRECTION('',(0.,-1.,0.))")
        actual, observed = self.invoke(data, "--query", "5", "1.5", "1")
        self.assertEqual(0, actual.returncode)
        accepted(observed["import"], data, COPLANAR_TRUTH)
        face, = [f for f in observed["import"]["face_mappings"] if f["source_face"] == "714"]
        for a, b in zip(face["measurements"]["window_mm"], (0, -2, 3, 0)):
            self.assertAlmostEqual(a, b, delta=1e-9)
        use, = [u for row in observed["import"]["loop_mappings"] for u in row["uses"]
                if u["source_oriented_edge"] == "813"]
        self.assertEqual(([0, -2], [3, -2]), (use["uv_start_mm"], use["uv_end_mm"]))
        self.assertEqual([5, 1.5, 0], observed["query"]["closest_point_mm"])

    def test_coplanar_sheet_in_an_oblique_world_plane(self):
        data = COPLANAR_SHEET.read_bytes()
        for record in inventory(data):
            if record.kind not in {"CARTESIAN_POINT", "DIRECTION"}:
                continue
            x, y, z = source_vector(record.args[1])
            point = (F(3, 5)*x + F(4, 5)*z, y, -F(4, 5)*x + F(3, 5)*z)
            if record.kind == "CARTESIAN_POINT":
                point = tuple(a+b for a, b in zip(point, (7, -2, 5)))
            value = "(" + ",".join(str(float(v)) for v in point) + ")"
            data = self.replace(data, f"#{record.entity_id}={record.raw};",
                                f"#{record.entity_id}={record.kind}('',{value});")
        truth = deepcopy(COPLANAR_TRUTH)
        truth["centroid"] = [exact(F(44, 5)), exact(F(-1, 2)), exact(F(13, 5))]
        truth["bounds"] = [[exact(7), exact(-2), exact(F(1, 5))],
                           [exact(F(53, 5)), exact(1), exact(5)]]
        actual, observed = self.invoke(data, "--query", "10.8", "-0.5", "1.6")
        self.assertEqual(0, actual.returncode, observed.get("diagnostics"))
        accepted(observed["import"], data, truth)
        for a, b in zip(observed["query"]["closest_point_mm"], (10, -0.5, 1)):
            self.assertAlmostEqual(a, b, delta=1e-7)
        self.assertAlmostEqual(1, observed["query"]["distance_mm"], delta=1e-7)

    def test_source_counterexamples_never_enter_native_or_get_stitched(self):
        data = SHEET.read_bytes()
        duplicate = self.replace(data, "#813=ORIENTED_EDGE('',*,*,#601,.F.);",
                                 "#813=ORIENTED_EDGE('',*,*,#607,.F.);")
        duplicate = self.replace(duplicate, "ENDSEC;\nEND-ISO-10303-21;",
                                 "#607=EDGE_CURVE('',#201,#202,#501,.T.);\nENDSEC;\nEND-ISO-10303-21;")
        for candidate, key in ((duplicate, "DisconnectedSourceShell"),
                               (data.replace(b"#22=OPEN_SHELL", b"#22=CLOSED_SHELL"), "ClaimedClosedShellHasBoundary"),
                               (data.replace(b"#716=FACE_OUTER_BOUND('',#715,.T.)",
                                             b"#716=FACE_OUTER_BOUND('',#715,.F.)"),
                                "FaceBoundaryOrShellOrientationMismatch")):
            with self.subTest(key=key):
                actual, observed = self.invoke(candidate)
                self.assertEqual(1, actual.returncode)
                self.assertEqual("Rejected", observed["status"])
                self.assertEqual((key, "D4"), (observed["diagnostics"][0]["key"], observed["diagnostics"][0]["stage"]))
                self.assertIsNone(observed["model"])
                self.assertEqual((0, "None"), (observed["native_calls"], observed["repair"]))

    def test_convex_trapezoid_is_preserved_without_fitting_a_rectangle(self):
        data = CONVEX_SHEET.read_bytes()
        actual, observed = self.invoke(data)
        self.assertEqual(0, actual.returncode, observed["diagnostics"])
        self.polygon_checked(observed, data, CONVEX_TRUTH)

    def test_two_triangles_preserve_variable_census_and_complete_boundaries(self):
        data = TRIANGULAR_SHEET.read_bytes()
        actual, observed = self.invoke(data, "--query", "5", "1", "0.5")
        self.assertEqual(0, actual.returncode, observed.get("diagnostics"))
        self.polygon_checked(observed["import"], data, TRIANGULAR_TRUTH)
        self.assertEqual((4, 60), (observed["import"]["native_measurements"]["free_boundary_edges"],
                                  len(observed["import"]["tolerances"])))
        self.assertEqual(6, sum(len(row["uses"]) for row in observed["import"]["loop_mappings"]))
        self.assertEqual([4, 1, 0.5], observed["query"]["closest_point_mm"])
        self.assertAlmostEqual(1, observed["query"]["distance_mm"], delta=1e-7)
        self.assertEqual("NotApplicable", observed["query"]["material_location"])

    def test_si_restatement_preserves_sheet_and_dimensionless_parameters(self):
        for fixture, truth in ((SHEET, TRUTH), (COPLANAR_SHEET, COPLANAR_TRUTH), (CONVEX_SHEET, CONVEX_TRUTH),
                                (TRIANGULAR_SHEET, TRIANGULAR_TRUTH), (PENTAGONAL_SHEET, PENTAGONAL_TRUTH)):
            data = fixture.read_bytes().replace(b"SI_UNIT(.MILLI.,.METRE.)", b"SI_UNIT(.CENTI.,.METRE.)")
            for record in inventory(data):
                args = list(record.args)
                if record.kind == "CARTESIAN_POINT":
                    args[1] = "(" + ",".join(str(float(v / 10)) for v in source_vector(args[1])) + ")"
                elif record.kind == "VECTOR":
                    args[2] = str(float(F(args[2]) / 10))
                else:
                    continue
                data = self.replace(data, f"#{record.entity_id}={record.raw};",
                                    f"#{record.entity_id}={record.kind}({','.join(args)});")
            with self.subTest(fixture=fixture.name):
                actual, observed = self.invoke(data)
                self.assertEqual(0, actual.returncode)
                self.polygon_checked(observed, data, truth)
                self.assertEqual(exact(10), observed["unit_normalization"]["length_to_mm"])
                self.assertEqual("DimensionlessUnchanged", observed["unit_normalization"]["line_parameter"])

    def variants(self, original):
        for reverse_curve, swap_edges, reverse_bounds, flip_planes in itertools.product((False, True), (False, True),
                                                                                      range(4), range(4)):
            data = original
            for record in inventory(data):
                args = list(record.args)
                identity = record.entity_id
                toggle = lambda token: ".F." if token == ".T." else ".T."
                if record.kind == "DIRECTION" and reverse_curve and 300 <= identity < 400:
                    args[1] = "(" + ",".join(str(-float(v)) for v in source_vector(args[1])) + ")"
                elif record.kind == "EDGE_CURVE":
                    if swap_edges:
                        args[1], args[2] = args[2], args[1]
                    if reverse_curve != swap_edges:
                        args[4] = toggle(args[4])
                elif record.kind == "ORIENTED_EDGE":
                    face = 0 if identity < 810 else 1
                    if swap_edges != bool(reverse_bounds & (1 << face)):
                        args[4] = toggle(args[4])
                elif record.kind in {"FACE_OUTER_BOUND", "EDGE_LOOP"}:
                    face = 0 if identity < 710 else 1
                    if reverse_bounds & (1 << face):
                        args[-1] = toggle(args[-1]) if record.kind == "FACE_OUTER_BOUND" else \
                            "(" + ",".join(reversed(aggregate(args[1]))) + ")"
                elif record.kind == "DIRECTION" and identity in {700, 710}:
                    if flip_planes & (1 << (identity == 710)):
                        args[1] = "(" + ",".join(str(-float(v)) for v in source_vector(args[1])) + ")"
                elif record.kind == "ADVANCED_FACE":
                    if flip_planes & (1 << (identity == 714)):
                        args[3] = toggle(args[3])
                elif record.kind == "OPEN_SHELL":
                    args[1] = "(#714,#704)"
                else:
                    continue
                data = self.replace(data, f"#{identity}={record.raw};", f"#{identity}={record.kind}({','.join(args)});")
            # IDs carry source identity, not fixture dispatch or array positions.
            data = re.sub(rb"#(\d+)", lambda m: b"#" + str(int(m[1]) * 19 + 1_000_001).encode(), data)
            yield (reverse_curve, swap_edges, reverse_bounds, flip_planes), data

    def test_equivalent_senses_face_order_and_sparse_identity_preserve_sheet(self):
        for fixture, truth, closest in ((SHEET, TRUTH, (4, 1.5, 1)),
                                        (COPLANAR_SHEET, COPLANAR_TRUTH, (5, 1.5, 0)),
                                        (CONVEX_SHEET, CONVEX_TRUTH, (4, 1.5, 1)),
                                        (TRIANGULAR_SHEET, TRIANGULAR_TRUTH, (4, 1.5, 1)),
                                        (PENTAGONAL_SHEET, PENTAGONAL_TRUTH, (4, 1.5, 1))):
            for flags, data in self.variants(fixture.read_bytes()):
                with self.subTest(fixture=fixture.name, flags=flags):
                    actual, observed = self.invoke(data, "--query", "5", "1.5", "1")
                    self.assertEqual(0, actual.returncode, observed.get("diagnostics"))
                    self.polygon_checked(observed["import"], data, truth)
                    self.assertEqual("NotApplicable", observed["query"]["material_location"])
                    for a, b in zip(observed["query"]["closest_point_mm"], closest):
                        self.assertAlmostEqual(a, b, delta=1e-7)
                    self.assertAlmostEqual(1, observed["query"]["distance_mm"], delta=1e-7)

    def test_coplanar_shared_sheet_preserves_both_faces_and_frames(self):
        data = COPLANAR_SHEET.read_bytes()
        actual, observed = self.invoke(data)
        self.assertEqual(0, actual.returncode, observed["diagnostics"])
        accepted(observed, data, COPLANAR_TRUTH)
        self.assertEqual({"kind": "OwningReadOnlySnapshot", "role": "OpenSurfaceModel"}, observed["model"])
        self.assertEqual({"704", "714"}, {f["source_face"] for f in observed["face_mappings"]})
        shared, = [r for r in observed["edge_adjacency"] if len(r["source_faces"]) == 2]
        self.assertEqual(("601", 4), (shared["source_edge"], shared["native_edgeuses"]))

    def test_independent_checker_rejects_false_sheet_properties(self):
        actual, observed = self.invoke()
        self.assertEqual(0, actual.returncode)
        accepted(observed, SHEET.read_bytes(), TRUTH)
        for defect in ("area", "edge_length", "centroid", "normal", "free_edges", "material", "shared_uses", "uv"):
            bad = deepcopy(observed)
            native = bad["native_measurements"]
            if defect == "area":
                native["area_mm2"] = 12
            elif defect == "edge_length":
                native["boundary_mm"] = 24
            elif defect == "centroid":
                native["centroid_mm"] = [2, 1.5, 0]
            elif defect == "normal":
                native["normal"] = [0, 0, 1]
            elif defect == "free_edges":
                native["free_boundary_edges"] = 7
            elif defect == "material":
                bad["model"]["role"] = "MaterialSolid"
            elif defect == "shared_uses":
                next(r for r in bad["edge_adjacency"] if r["source_edge"] == "601")["native_edgeuses"] = 2
            else:
                bad["loop_mappings"][1]["uses"][0]["uv_end_mm"] = [99, 99]
            with self.subTest(defect=defect), self.assertRaises(AssertionError):
                accepted(bad, SHEET.read_bytes(), TRUTH)

    def test_closest_queries_cover_both_faces_shared_edge_and_corner(self):
        for query, point, distance in ((("2", "1.5", "1"), (2, 1.5, 0), 1),
                                       (("5", "1.5", "1"), (4, 1.5, 1), 1),
                                       (("5", "1.5", "-1"), (4, 1.5, 0), math.sqrt(2)),
                                       (("5", "4", "3"), (4, 3, 2), math.sqrt(3))):
            with self.subTest(query=query):
                actual, observed = self.invoke(None, "--query", *query)
                self.assertEqual(0, actual.returncode)
                accepted(observed["import"], SHEET.read_bytes(), TRUTH)
                for a, b in zip(observed["query"]["closest_point_mm"], point):
                    self.assertAlmostEqual(a, b, delta=1e-7)
                self.assertAlmostEqual(distance, observed["query"]["distance_mm"], delta=1e-7)
                self.assertEqual("NotApplicable", observed["query"]["material_location"])

    def test_sheet_geometry_and_each_use_match_independent_truth(self):
        binary = Path(os.environ["STEP_NATIVE_BINARY"]).resolve(strict=True)
        actual = subprocess.run([str(binary), "--fixture-limits", "--quality", "Q-P0-mm-01", str(SHEET)],
                                capture_output=True, timeout=30)
        self.assertEqual(0, actual.returncode)
        accepted(json.loads(actual.stdout), SHEET.read_bytes(), TRUTH)

    def test_open_shared_sheet_preserves_both_faces_and_one_shared_edge(self):
        binary = Path(os.environ["STEP_NATIVE_BINARY"]).resolve(strict=True)
        actual = subprocess.run([str(binary), "--fixture-limits", "--quality", "Q-P0-mm-01", str(SHEET)],
                                capture_output=True, timeout=30)
        observed = json.loads(actual.stdout)
        self.assertEqual(0, actual.returncode, actual.stderr or observed["diagnostics"])
        self.assertEqual({"kind": "OwningReadOnlySnapshot", "role": "OpenSurfaceModel"}, observed["model"])
        native = observed["native_measurements"]
        self.assertEqual((2, 7, 6, 6), tuple(native[k] for k in ("faces", "edges", "vertices", "free_boundary_edges")))
        self.assertAlmostEqual(18, native["area_mm2"], delta=1e-6)
        self.assertAlmostEqual(21, native["boundary_mm"], delta=1e-7)
        self.assertIsNone(native["normal"])
        self.assertIsNone(native["window_mm"])
        self.assertEqual("NotApplicable", native["material_volume"])
        shared, = [r for r in observed["edge_adjacency"] if len(r["source_faces"]) == 2]
        self.assertEqual(("601", 4), (shared["source_edge"], shared["native_edgeuses"]))


if __name__ == "__main__":
    unittest.main()
