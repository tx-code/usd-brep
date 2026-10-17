# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Actual STEP CLI contracts for shared faces with independently specified holes."""
from copy import deepcopy
from fractions import Fraction as F
import itertools
import re
import unittest

from tools.step_import_native_validation.tests import test_sheet as sheets
from tools.step_import_validation.oracle import decode, exact, sqrt_interval
from tools.step_import_validation.records import aggregate, by_id, inventory, ref, vector as source_vector

MULTI_HOLE_SHEET = sheets.SHEET.with_name("multi_hole_shared_sheet.step")
_length, _bounds = sheets.radical_length(25, 5, 3)
# Subtract areas 1, 1/4 and 1/2 from the area-17 concave sheet.
# Their moments are (3/2,1,0), (11/16,9/16,0) and (2,1/2,1/4).
MULTI_HOLE_TRUTH = {"role": "SurfaceModel", "source_topology": {"faces": 2, "edges": 20, "vertices": 19},
                    "measures": {"area": exact(F(61, 4)), "perimeter": _length},
                    "perimeter_bounds": _bounds,
                    "centroid": [exact(F(637, 244)), exact(F(1165, 732)), exact(F(15, 61))],
                    "bounds": deepcopy(sheets.CONCAVE_TRUTH["bounds"])}
FIXTURES = ((sheets.PERFORATED_SHEET, sheets.PERFORATED_TRUTH), (MULTI_HOLE_SHEET, MULTI_HOLE_TRUTH))


class PerforatedSheetTests(unittest.TestCase):
    def setUp(self):
        self.cli = sheets.SheetTests()

    def hole_points(self, data, bound):
        index = by_id(inventory(data))
        points = set()
        for token in aggregate(index[ref(index[bound].args[1])].args[1]):
            edge = index[ref(index[ref(token)].args[3])]
            points.update(ref(index[ref(v)].args[1]) for v in edge.args[1:3])
            points.add(ref(index[ref(edge.args[3])].args[1]))
        return points

    def move_hole(self, data, bound, transform):
        points = self.hole_points(data, bound)
        for record in inventory(data):
            if record.entity_id in points:
                value = transform(source_vector(record.args[1]))
                body = "CARTESIAN_POINT('',(" + ",".join(format(float(v), ".12f") for v in value) + "))"
                data = self.cli.replace(data, f"#{record.entity_id}={record.raw};", f"#{record.entity_id}={body};")
        return data

    def without_hole(self, data, bound):
        index = by_id(inventory(data))
        removed, pending = set(), [bound]
        while pending:
            record = pending.pop()
            if record not in removed:
                removed.add(record)
                pending.extend(int(r) for r in re.findall(r"#(\d+)", index[record].raw))
        for record in index.values():
            if record.entity_id in removed:
                data = self.cli.replace(data, f"#{record.entity_id}={record.raw};\n", "")
            elif record.kind == "ADVANCED_FACE" and f"#{bound}" in aggregate(record.args[1]):
                args = list(record.args)
                args[1] = "(" + ",".join(v for v in aggregate(args[1]) if v != f"#{bound}") + ")"
                data = self.cli.replace(data, f"#{record.entity_id}={record.raw};",
                                        f"#{record.entity_id}=ADVANCED_FACE({','.join(args)});")
        return data

    def unfolded(self, data, side=1):
        index = by_id(inventory(data))
        directions, points = {710, 711}, set()
        for bound in aggregate(index[714].args[1]):
            for token in aggregate(index[ref(index[ref(bound)].args[1])].args[1]):
                edge = index[ref(index[ref(token)].args[3])]
                line = index[ref(edge.args[3])]
                directions.add(ref(index[ref(line.args[2])].args[1]))
                points.add(ref(line.args[1]))
                points.update(ref(index[ref(v)].args[1]) for v in edge.args[1:3])
        for record in index.values():
            if record.entity_id in directions | points:
                x, y, z = source_vector(record.args[1])
                value = (side*z, y, -side*x) if record.kind == "DIRECTION" else (4+side*z, y, -side*(x-4))
                body = record.kind + "('',(" + ",".join(str(float(v)) for v in value) + "))"
                data = self.cli.replace(data, f"#{record.entity_id}={record.raw};", f"#{record.entity_id}={body};")
        return data

    def query_checked(self, data, truth, query, closest, squared):
        actual, observed = self.cli.invoke(data, "--query", *(str(float(v)) for v in query))
        self.assertEqual(0, actual.returncode, observed.get("diagnostics"))
        self.cli.polygon_checked(observed["import"], data, truth)
        for a, b in zip(observed["query"]["closest_point_mm"], closest):
            self.assertAlmostEqual(a, float(b), delta=1e-7)
        bounds = sqrt_interval(F(squared))
        distance = F(observed["query"]["distance_mm"])
        self.assertLessEqual(max(abs(distance-decode(bounds[k])) for k in ("lower", "upper")), F(1, 10**7))
        self.assertEqual("NotApplicable", observed["query"]["material_location"])

    def test_concave_outer_boundary_and_multiple_holes_keep_every_loop(self):
        data = MULTI_HOLE_SHEET.read_bytes()
        cli = sheets.SheetTests()
        actual, observed = cli.invoke(data)
        self.assertEqual(0, actual.returncode, observed.get("diagnostics"))
        cli.polygon_checked(observed, data, MULTI_HOLE_TRUTH)
        self.assertEqual((5, 3, 21), (len(observed["loop_mappings"]),
                                    sum(not r["outer"] for r in observed["loop_mappings"]),
                                    sum(len(r["uses"]) for r in observed["loop_mappings"])))
        self.assertEqual((19, 210), (observed["native_measurements"]["free_boundary_edges"],
                                    len(observed["tolerances"])))

    def test_holes_remain_excluded_in_closest_queries_on_both_faces(self):
        for fixture, truth in FIXTURES:
            data = fixture.read_bytes()
            with self.subTest(fixture=fixture.name, face="A"):
                self.query_checked(data, truth, (F(5, 4), F(9, 10), 1), (1, F(9, 10), 0), F(17, 16))
            if fixture == sheets.PERFORATED_SHEET:
                query, closest, squared = (5, F(3, 4), 1), (4, F(1, 2), 1), F(17, 16)
            else:
                query, closest, squared = (5, F(3, 5), F(1, 2)), (4, F(1, 2), F(1, 2)), F(101, 100)
            with self.subTest(fixture=fixture.name, face="B"):
                self.query_checked(data, truth, query, closest, squared)

    def test_only_one_face_needs_to_have_a_hole(self):
        for removed, center in ((726, (F(44, 17), F(26, 17), F(5, 17))),
                                (736, (F(93, 34), F(26, 17), F(6, 17)))):
            data = self.without_hole(sheets.PERFORATED_SHEET.read_bytes(), removed)
            truth = deepcopy(sheets.PERFORATED_TRUTH)
            truth["source_topology"] = {"faces": 2, "edges": 11, "vertices": 10}
            truth["measures"] = {"area": exact(17), "perimeter": exact(25)}
            truth["centroid"] = list(map(exact, center))
            with self.subTest(removed=removed):
                actual, observed = self.cli.invoke(data)
                self.assertEqual(0, actual.returncode, observed.get("diagnostics"))
                self.cli.polygon_checked(observed, data, truth)
                self.assertEqual(3, len(observed["loop_mappings"]))

    def test_coplanar_holes_keep_the_two_source_faces_separate(self):
        for (fixture, base), xcenter, ymax in zip(FIXTURES, (F(95, 32), F(697, 244)), (3, 4)):
            data = self.unfolded(fixture.read_bytes())
            truth = deepcopy(base)
            truth["centroid"] = [exact(xcenter), base["centroid"][1], exact(0)]
            truth["bounds"][1] = [exact(6), exact(ymax), exact(0)]
            with self.subTest(fixture=fixture.name):
                self.query_checked(data, truth, (F(5, 4), F(9, 10), 1), (1, F(9, 10), 0), F(17, 16))
                actual, observed = self.cli.invoke(self.unfolded(fixture.read_bytes(), -1))
                self.assertEqual(2, actual.returncode)
                self.assertEqual(("Checked", "Unsupported", 0),
                                 (observed["source"]["planar"], observed["status"], observed["native_calls"]))
                self.assertEqual("UnsupportedNativeOpenSheetScope", observed["diagnostics"][0]["key"])

    def test_bad_hole_regions_are_rejected_by_source_rules_before_native(self):
        base = sheets.PERFORATED_SHEET.read_bytes()
        defects = (("touch_shared", self.move_hole(base, 726, lambda p: (p[0]+2, p[1], p[2])),
                    "PlanarBoundaryIntersection"),
                   ("outside", self.move_hole(base, 726, lambda p: (p[0]+4, p[1], p[2])),
                    "InnerBoundaryOutsideOuter"),
                   ("wrong_orientation", self.cli.replace(base, "#726=FACE_BOUND('',#725,.T.)",
                                                           "#726=FACE_BOUND('',#725,.F.)"),
                    "InnerBoundaryOrientationMismatch"))
        multi = MULTI_HOLE_SHEET.read_bytes()
        defects += (("overlap", self.move_hole(multi, 746, lambda p: (p[0]-1, p[1]-1, p[2])),
                     "PlanarBoundaryIntersection"),
                    ("nested", self.move_hole(multi, 746, lambda p: (p[0]-F(5, 4), p[1]-F(5, 4), p[2])),
                     "AmbiguousNestedInnerBoundary"))
        for name, data, key in defects:
            with self.subTest(defect=name):
                actual, observed = self.cli.invoke(data)
                self.assertEqual(1, actual.returncode, observed.get("diagnostics"))
                self.assertEqual((key, "D4"), tuple(observed["diagnostics"][0][k] for k in ("key", "stage")))
                self.assertEqual((0, "None"), (observed["native_calls"], observed["repair"]))
                self.assertIsNone(observed["model"])

    def test_hole_vertices_cannot_bypass_exact_native_plane_support(self):
        data = self.move_hole(sheets.PERFORATED_SHEET.read_bytes(), 726,
                              lambda p: (p[0], p[1], p[2]+F(1, 10**8)))
        actual, observed = self.cli.invoke(data)
        self.assertEqual(2, actual.returncode, observed.get("diagnostics"))
        self.assertEqual(("Checked", "Unsupported", 0),
                         (observed["source"]["planar"], observed["status"], observed["native_calls"]))
        self.assertEqual("UnsupportedNativeOpenSheetScope", observed["diagnostics"][0]["key"])
        self.assertIsNone(observed["model"])

    def test_a_source_shared_hole_edge_does_not_acquire_outer_edge_qualification(self):
        data = self.without_hole(sheets.PERFORATED_SHEET.read_bytes(), 736)
        # Fold FB out of FA's inner left edge instead of the outer x=4 edge.
        for old, new in (("#104=CARTESIAN_POINT('',(4.,0.,2.))", "#104=CARTESIAN_POINT('',(1.,0.5,2.))"),
                         ("#105=CARTESIAN_POINT('',(4.,3.,2.))", "#105=CARTESIAN_POINT('',(1.,1.5,2.))"),
                         ("#504=LINE('',#101,#404)", "#504=LINE('',#110,#404)"),
                         ("#604=EDGE_CURVE('',#201,#204,#504,.T.)", "#604=EDGE_CURVE('',#210,#204,#504,.T.)"),
                         ("#606=EDGE_CURVE('',#205,#202,#506,.T.)", "#606=EDGE_CURVE('',#205,#211,#506,.T.)"),
                         ("#813=ORIENTED_EDGE('',*,*,#601,.F.)", "#813=ORIENTED_EDGE('',*,*,#610,.F.)"),
                         ("#712=AXIS2_PLACEMENT_3D('',#101,#710,#711)",
                          "#712=AXIS2_PLACEMENT_3D('',#110,#710,#711)")):
            data = self.cli.replace(data, old, new)
        actual, observed = self.cli.invoke(data)
        self.assertEqual(2, actual.returncode, observed.get("diagnostics"))
        self.assertEqual(("Checked", "Unsupported", 0),
                         (observed["source"]["planar"], observed["status"], observed["native_calls"]))
        self.assertEqual(("UnsupportedNativeOpenSheetScope", "D5", "TargetUnsupported"),
                         tuple(observed["diagnostics"][0][k] for k in ("key", "stage", "classification")))
        self.assertIsNone(observed["model"])

    def test_independent_checker_rejects_lost_holes_uses_uv_or_surface_role(self):
        for fixture, truth in FIXTURES:
            data = fixture.read_bytes()
            actual, observed = self.cli.invoke(data)
            self.assertEqual(0, actual.returncode)
            for defect in ("missing_loop", "filled_area", "missing_use", "wrong_uv", "wrong_loop_role", "material"):
                bad = deepcopy(observed)
                inner = next(row for row in bad["loop_mappings"] if not row["outer"])
                if defect == "missing_loop":
                    bad["loop_mappings"].remove(inner)
                elif defect == "filled_area":
                    bad["native_measurements"]["area_mm2"] = 18
                elif defect == "missing_use":
                    inner["uses"].pop()
                elif defect == "wrong_uv":
                    inner["uses"][0]["uv_start_mm"] = [99, 99]
                elif defect == "wrong_loop_role":
                    inner["outer"] = True
                else:
                    bad["model"]["role"] = "MaterialSolid"
                with self.subTest(fixture=fixture.name, defect=defect), self.assertRaises(AssertionError):
                    self.cli.polygon_checked(bad, data, truth)

    def variants(self, original):
        index = by_id(inventory(original))
        loop_bits, use_bits, bound_bits = {}, {}, {}
        for face in (704, 714):
            for token in aggregate(index[face].args[1]):
                bound = ref(token)
                loop = ref(index[bound].args[1])
                bit = 1 << len(loop_bits)
                loop_bits[loop], bound_bits[bound] = bit, bit
                for edge in aggregate(index[loop].args[1]):
                    use_bits[ref(edge)] = bit
        for mask, planes in itertools.product(range(1 << len(loop_bits)), range(4)):
            data = original
            reverse_curve, swap_edges = bool(mask & 1), bool(planes & 1)
            for record in index.values():
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
                    if swap_edges != bool(mask & use_bits[identity]):
                        args[4] = toggle(args[4])
                elif identity in bound_bits and mask & bound_bits[identity]:
                    args[2] = toggle(args[2])
                elif identity in loop_bits and mask & loop_bits[identity]:
                    args[1] = "(" + ",".join(reversed(aggregate(args[1]))) + ")"
                elif record.kind == "DIRECTION" and identity in {700, 710}:
                    if planes & (1 << (identity == 710)):
                        args[1] = "(" + ",".join(str(-float(v)) for v in source_vector(args[1])) + ")"
                elif record.kind == "ADVANCED_FACE":
                    args[1] = "(" + ",".join(reversed(aggregate(args[1]))) + ")"
                    if planes & (1 << (identity == 714)):
                        args[3] = toggle(args[3])
                elif record.kind == "OPEN_SHELL":
                    args[1] = "(#714,#704)"
                else:
                    continue
                data = self.cli.replace(data, f"#{identity}={record.raw};",
                                        f"#{identity}={record.kind}({','.join(args)});")
            data = re.sub(rb"#(\d+)", lambda m: b"#" + str(int(m[1])*23+2_000_001).encode(), data)
            yield (mask, planes), data

    def test_each_inner_outer_sense_and_identity_keeps_the_trimmed_sheet(self):
        for fixture, base in FIXTURES:
            for coplanar in (False, True):
                # Both configurations exercise every independent inner/outer bound bit.
                # Split plane-sense combinations across folded and coplanar frames.
                original = self.unfolded(fixture.read_bytes()) if coplanar else fixture.read_bytes()
                truth = deepcopy(base)
                if coplanar:
                    xcenter = F(95, 32) if fixture == sheets.PERFORATED_SHEET else F(697, 244)
                    truth["centroid"] = [exact(xcenter), base["centroid"][1], exact(0)]
                    truth["bounds"][1] = [exact(6), base["bounds"][1][1], exact(0)]
                for flags, data in self.variants(original):
                    if bool(flags[1] & 2) != coplanar:
                        continue
                    with self.subTest(fixture=fixture.name, coplanar=coplanar, flags=flags):
                        closest = (5, 1.5, 0) if coplanar else (4, 1.5, 1)
                        self.query_checked(data, truth, (5, 1.5, 1), closest, 1)

    def test_hole_units_are_normalized_without_changing_line_parameters(self):
        for fixture, truth in FIXTURES:
            data = self.cli.replace(fixture.read_bytes(), "SI_UNIT(.MILLI.,.METRE.)", "SI_UNIT(.CENTI.,.METRE.)")
            for record in inventory(data):
                args = list(record.args)
                if record.kind == "CARTESIAN_POINT":
                    args[1] = "(" + ",".join(str(float(v/10)) for v in source_vector(args[1])) + ")"
                elif record.kind == "VECTOR":
                    args[2] = str(float(F(args[2])/10))
                else:
                    continue
                data = self.cli.replace(data, f"#{record.entity_id}={record.raw};",
                                        f"#{record.entity_id}={record.kind}({','.join(args)});")
            with self.subTest(fixture=fixture.name):
                actual, observed = self.cli.invoke(data)
                self.assertEqual(0, actual.returncode, observed.get("diagnostics"))
                self.cli.polygon_checked(observed, data, truth)
                self.assertEqual(exact(10), observed["unit_normalization"]["length_to_mm"])
                self.assertEqual("DimensionlessUnchanged", observed["unit_normalization"]["line_parameter"])

    def test_hole_uvs_follow_shifted_rotated_source_frames(self):
        for fixture, truth in FIXTURES:
            data = fixture.read_bytes()
            for old, new in (("#711=DIRECTION('',(0.,0.,1.))", "#711=DIRECTION('',(0.,1.,0.))"),
                             ("#712=AXIS2_PLACEMENT_3D('',#101,#710,#711)",
                              "#712=AXIS2_PLACEMENT_3D('',#139,#710,#711)"),
                             ("ENDSEC;\nEND-ISO-10303-21;",
                              "#139=CARTESIAN_POINT('',(4.,2.,1.));\nENDSEC;\nEND-ISO-10303-21;")):
                data = self.cli.replace(data, old, new)
            with self.subTest(fixture=fixture.name):
                actual, observed = self.cli.invoke(data)
                self.assertEqual(0, actual.returncode, observed.get("diagnostics"))
                self.cli.polygon_checked(observed, data, truth)
                inner = next(r for r in observed["loop_mappings"] if r["source_bound"] == "736")
                self.assertTrue(any(v < 0 for row in inner["uses"] for v in row["uv_start_mm"]))

    def test_holes_are_preserved_in_an_oblique_world_frame(self):
        for fixture, base in FIXTURES:
            data = fixture.read_bytes()
            for record in inventory(data):
                if record.kind not in {"CARTESIAN_POINT", "DIRECTION"}:
                    continue
                x, y, z = source_vector(record.args[1])
                value = (F(3, 5)*x+F(4, 5)*z, y, -F(4, 5)*x+F(3, 5)*z)
                if record.kind == "CARTESIAN_POINT":
                    value = tuple(a+b for a, b in zip(value, (7, -2, 5)))
                body = record.kind + "('',(" + ",".join(str(float(v)) for v in value) + "))"
                data = self.cli.replace(data, f"#{record.entity_id}={record.raw};", f"#{record.entity_id}={body};")
            truth = deepcopy(base)
            x, y, z = map(decode, base["centroid"])
            truth["centroid"] = [exact(7+F(3, 5)*x+F(4, 5)*z), exact(y-2), exact(5-F(4, 5)*x+F(3, 5)*z)]
            truth["bounds"] = [[exact(7), exact(-2), exact(F(9, 5))],
                               [exact(11), exact(1 if fixture == sheets.PERFORATED_SHEET else 2), exact(5)]]
            with self.subTest(fixture=fixture.name):
                self.query_checked(data, truth, (8.55, -1.1, 4.6), (7.6, -1.1, 4.2), F(17, 16))
