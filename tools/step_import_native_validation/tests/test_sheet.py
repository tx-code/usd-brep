# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""P0 S1: real CLI contracts for a folded two-face sheet, independently specified."""
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
from tools.step_import_validation.oracle import exact
from tools.step_import_validation.records import aggregate, inventory, vector as source_vector

SHEET = Path(__file__).resolve().parents[3] / "source/STEP_IMPORT/tests/fixtures/open_shared_sheet.step"
# P0 S1 analytic truth: face areas 12 and 6; unique edge lengths total 21.
# Area moments: (48, 27, 6); no CAD-generated expectations.
TRUTH = {"role": "SurfaceModel", "source_topology": {"faces": 2, "edges": 7, "vertices": 6},
         "measures": {"area": exact(18), "perimeter": exact(21)},
         "centroid": [exact(F(8, 3)), exact(F(3, 2)), exact(F(1, 3))],
         "bounds": [[exact(0), exact(0), exact(0)], [exact(4), exact(3), exact(2)]]}


class SheetTests(unittest.TestCase):
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

    def test_source_valid_trapezoid_is_not_guessed_into_a_rectangle(self):
        data = SHEET.read_bytes()
        for old, new in (("#105=CARTESIAN_POINT('',(4.,3.,2.))", "#105=CARTESIAN_POINT('',(4.,4.,2.))"),
                         ("#405=VECTOR('',#305,3.)", "#405=VECTOR('',#305,4.)"),
                         ("#306=DIRECTION('',(0.,0.,-1.))", "#306=DIRECTION('',(0.,-1.,-2.))"),
                         ("#406=VECTOR('',#306,2.)", "#406=VECTOR('',#306,1.)")):
            data = self.replace(data, old, new)
        actual, observed = self.invoke(data)
        self.assertEqual(2, actual.returncode)
        self.assertEqual("Checked", observed["source"]["planar"])
        self.assertEqual("UnsupportedNativeOpenSheetScope", observed["diagnostics"][0]["key"])
        self.assertEqual(0, observed["native_calls"])
        self.assertIsNone(observed["model"])

    def test_si_restatement_preserves_sheet_and_dimensionless_parameters(self):
        data = SHEET.read_bytes().replace(b"SI_UNIT(.MILLI.,.METRE.)", b"SI_UNIT(.CENTI.,.METRE.)")
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
        actual, observed = self.invoke(data)
        self.assertEqual(0, actual.returncode)
        accepted(observed, data, TRUTH)
        self.assertEqual(exact(10), observed["unit_normalization"]["length_to_mm"])
        self.assertEqual("DimensionlessUnchanged", observed["unit_normalization"]["line_parameter"])

    def test_equivalent_senses_face_order_and_sparse_identity_preserve_sheet(self):
        for reverse_curve, swap_edges, reverse_bounds, flip_planes in itertools.product((False, True), (False, True),
                                                                                      range(4), range(4)):
            data = SHEET.read_bytes()
            for record in inventory(data):
                args = list(record.args)
                identity = record.entity_id
                toggle = lambda token: ".F." if token == ".T." else ".T."
                if record.kind == "DIRECTION" and reverse_curve and 300 <= identity <= 306:
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
            with self.subTest(flags=(reverse_curve, swap_edges, reverse_bounds, flip_planes)):
                actual, observed = self.invoke(data, "--query", "5", "1.5", "1")
                self.assertEqual(0, actual.returncode, observed.get("diagnostics"))
                accepted(observed["import"], data, TRUTH)
                self.assertEqual("NotApplicable", observed["query"]["material_location"])
                for a, b in zip(observed["query"]["closest_point_mm"], (4, 1.5, 1)):
                    self.assertAlmostEqual(a, b, delta=1e-7)
                self.assertAlmostEqual(1, observed["query"]["distance_mm"], delta=1e-7)

    def test_source_valid_coplanar_sheet_is_outside_bounded_target_scope(self):
        data = SHEET.read_bytes()
        for old, new in ((b"(4.,0.,2.)", b"(6.,0.,0.)"), (b"(4.,3.,2.)", b"(6.,3.,0.)"),
                         (b"#304=DIRECTION('',(0.,0.,1.))", b"#304=DIRECTION('',(1.,0.,0.))"),
                         (b"#306=DIRECTION('',(0.,0.,-1.))", b"#306=DIRECTION('',(-1.,0.,0.))"),
                         (b"#710=DIRECTION('',(-1.,0.,0.))", b"#710=DIRECTION('',(0.,0.,1.))"),
                         (b"#711=DIRECTION('',(0.,0.,1.))", b"#711=DIRECTION('',(1.,0.,0.))")):
            self.assertEqual(1, data.count(old))
            data = data.replace(old, new)
        actual, observed = self.invoke(data)
        self.assertEqual(2, actual.returncode)
        self.assertEqual("Checked", observed["source"]["planar"])
        self.assertEqual("Unsupported", observed["status"])
        self.assertEqual("UnsupportedNativeOpenSheetScope", observed["diagnostics"][0]["key"])
        self.assertIsNone(observed["model"])
        self.assertEqual(0, observed["native_calls"])

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
