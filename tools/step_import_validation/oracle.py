# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Independent rational mathematics and narrow fixture facts; no CAD dependency."""

from __future__ import annotations

from fractions import Fraction as F
from math import isqrt

from .common import InvalidEvidence
from .records import aggregate, by_id, inventory, ref, vector


def exact(value: int | F) -> dict:
    value = F(value)
    return {"kind": "rational", "num": str(value.numerator), "den": str(value.denominator)}


def decode(value: dict) -> F:
    if not isinstance(value, dict) or set(value) != {"kind", "num", "den"} or value["kind"] != "rational":
        raise InvalidEvidence("Contract.ExactValueEncodingInvalid", value)
    if not isinstance(value["num"], str) or not isinstance(value["den"], str):
        raise InvalidEvidence("Contract.ExactValueEncodingInvalid", value)
    try:
        result = F(int(value["num"]), int(value["den"]))
    except (ValueError, ZeroDivisionError) as exc:
        raise InvalidEvidence("Contract.ExactValueEncodingInvalid", value) from exc
    if exact(result) != value:
        raise InvalidEvidence("Contract.ExactValueEncodingInvalid", value)
    return result


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def dot(a, b):
    return sum((x * y for x, y in zip(a, b)), F(0))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def signed_area(points) -> F:
    return sum((a[0] * b[1] - a[1] * b[0] for a, b in zip(points, points[1:] + points[:1])), F(0)) / 2


def sqrt_interval(value: F, places: int = 12) -> dict:
    if value < 0 or not 0 <= places <= 100:
        raise InvalidEvidence("Oracle.SqrtDomain", str(value))
    scale = 10 ** places
    k = isqrt(value.numerator * scale * scale // value.denominator)
    lo = F(k, scale)
    hi = lo if lo * lo == value else F(k + 1, scale)
    if not (lo * lo <= value <= hi * hi):
        raise InvalidEvidence("Oracle.IntervalContainment", str(value))
    return {"radicand": exact(value), "lower": exact(lo), "upper": exact(hi),
            "proof": "lower^2 <= radicand <= upper^2; nonnegative bounds"}


def segment_closest(q, a, b):
    direction = sub(b, a)
    t = max(F(0), min(F(1), dot(sub(q, a), direction) / dot(direction, direction)))
    return add(a, tuple(t * v for v in direction))


def polygon_location(q, polygon):
    """Exact ray crossings for reviewed finite polygons; no epsilon or CAD calls."""
    inside = False
    for a, b in zip(polygon, polygon[1:] + polygon[:1]):
        if segment_closest(q, a, b) == q:
            return "Boundary"
        if (a[1] > q[1]) != (b[1] > q[1]):
            crossing = a[0] + (q[1] - a[1]) * (b[0] - a[0]) / (b[1] - a[1])
            if q[0] < crossing:
                inside = not inside
    return "Inside" if inside else "Outside"


def query_truth(anchor, points):
    def distance(q, closest):
        return {"point_mm": [exact(v) for v in q], "closest_point_mm": [exact(v) for v in closest],
                "distance_mm": sqrt_interval(dot(sub(q, closest), sub(q, closest)))}

    if anchor["closed"]:
        low = tuple(min(p[i] for p in points) for i in range(3))
        high = tuple(max(p[i] for p in points) for i in range(3))
        mid = tuple((a + b) / 2 for a, b in zip(low, high))
        queries = {}
        for name, q in (("off_face", (high[0] + 3, mid[1], mid[2])),
                        ("corner", (low[0] - 1, low[1] - 2, low[2] - 4))):
            closest = tuple(max(a, min(b, v)) for a, b, v in zip(low, high, q))
            queries[name] = distance(q, closest)
        classifications = []
        samples = [mid, (low[0] - 1, mid[1], mid[2]), (low[0], mid[1], mid[2]),
                   (low[0], low[1], mid[2]), low]
        for q in samples:
            outside = any(v < a or v > b for a, b, v in zip(low, high, q))
            boundary_axes = sum(v in (a, b) for a, b, v in zip(low, high, q))
            location = "Outside" if outside else ["Inside", "OnFace", "OnEdge", "OnVertex"][boundary_axes]
            classifications.append({"point_mm": [exact(v) for v in q], "boundary_location": location,
                                    "material_location": location if anchor["role"] == "MaterialSolid" else None})
        queries["classifications"] = classifications
        queries["method"] = "exact axis intervals and clamp; surface role grants no InsideSolid qualification"
        return queries
    xaxis, yaxis = (F(3, 5), F(4, 5), F(0)), (F(-4, 5), F(3, 5), F(0))
    origin = points[0]
    uv = [(dot(sub(p, origin), xaxis), dot(sub(p, origin), yaxis)) for p in points]
    polygons = [[uv[i] for i in loop] for loop in anchor["loops"]]

    def world(q):
        return add(origin, add(tuple(q[0] * v for v in xaxis), tuple(q[1] * v for v in yaxis)))

    def location(q):
        states = [polygon_location(q, poly) for poly in polygons]
        if "Boundary" in states:
            return "OnBoundary"
        return "InsideFace" if states[0] == "Inside" and "Inside" not in states[1:] else "OutsideFace"

    # Queries are defined in reviewed world-frame UV, separately from STEP's parameter origin.
    scale = F(anchor["scale"])
    projection = (10 * scale, 6 * scale)
    q = add(world(projection), (F(0), F(0), F(3)))
    queries = {"off_face": distance(q, world(projection))}
    if len(polygons) == 2:
        projection = (F(3), F(5, 2))
        q = add(world(projection), (F(0), F(0), F(3)))
        candidates = [segment_closest(projection, a, b) for poly in polygons
                      for a, b in zip(poly, poly[1:] + poly[:1])]
        closest = min(candidates, key=lambda p: dot(sub(p, projection), sub(p, projection)))
        queries["hole"] = distance(q, world(closest))
        queries["classifications"] = [{"frame_uv": [exact(F(v)) for v in point],
                                       "point_mm": [exact(v) for v in world(point)], "location": location(point)}
                                      for point in [(10, 6), (4, 3), (2, 3), (13, 1)]]
    queries["method"] = "exact plane projection, ray crossings and minimum finite segment distance"
    return queries


def reference_truth(case_id: str, anchor: dict) -> dict:
    if anchor["kind"] == "Negative":
        return {"case_id": case_id, "kind": "NegativeFacts", "facts": anchor["facts"],
                "canonical_units": {"length": "mm", "squared_length": "mm^2"},
                "whole_shape": {"applicability": "NotEstablished", "value": None,
                                "reason": "Only specified input facts are proved; no imported model exists."}}
    points = [tuple(F(v) for v in point) for point in anchor["world_points"]]
    if anchor["closed"]:
        lengths = tuple(max(p[i] for p in points) - min(p[i] for p in points) for i in range(3))
        x, y, z = lengths
        area = 2 * (x * y + y * z + x * z)
        edge_length = 4 * (x + y + z)
        volume = x * y * z
        determinant_volume = F(0)
        for face in anchor["faces"]:
            a = points[face[0]]
            for j in range(1, len(face) - 1):
                determinant_volume += dot(a, cross(points[face[j]], points[face[j+1]])) / 6
        if volume != determinant_volume:
            raise InvalidEvidence("Oracle.IndependentVolumeDisagreement", str(determinant_volume))
        centroid = tuple((min(p[i] for p in points) + max(p[i] for p in points)) / 2 for i in range(3))
        methods = ["axis interval product", "signed tetrahedral determinants"]
    else:
        # World-plane projection, independent of the source STEP LINE/PLANE factories.
        xaxis, yaxis = (F(3, 5), F(4, 5), F(0)), (F(-4, 5), F(3, 5), F(0))
        origin = points[0]
        uv = [(dot(sub(p, origin), xaxis), dot(sub(p, origin), yaxis)) for p in points]
        polygons = [[uv[i] for i in loop] for loop in anchor["loops"]]
        areas = [signed_area(poly) for poly in polygons]
        area = sum(areas, F(0))
        edge_length = F(0)
        moment_u = moment_v = F(0)
        for poly in polygons:
            for a, b in zip(poly, poly[1:] + poly[:1]):
                squared = dot(sub(a, b), sub(a, b))
                edge = F(isqrt(squared.numerator), isqrt(squared.denominator))
                if edge * edge != squared:
                    raise InvalidEvidence("Oracle.NonRationalFixtureEdge", str(squared))
                edge_length += edge
                determinant = a[0] * b[1] - a[1] * b[0]
                moment_u += (a[0] + b[0]) * determinant
                moment_v += (a[1] + b[1]) * determinant
        centroid = add(origin, add(tuple(moment_u / (6 * area) * v for v in xaxis),
                                   tuple(moment_v / (6 * area) * v for v in yaxis)))
        # Different formula and reviewed dimensions, not the same polygon loop.
        scale = F(anchor["scale"])
        product_area = F(12 * 8 - (8 if len(polygons) == 2 else 0)) * scale * scale
        if area != product_area:
            raise InvalidEvidence("Oracle.IndependentAreaDisagreement", str(area))
        methods = ["exact plane projection and shoelace", "reviewed rectangle minus hole dimensions"]
        volume = None
    measures = {"area": exact(area), "perimeter": exact(edge_length)}
    if anchor["role"] == "MaterialSolid":
        measures["volume"] = exact(volume)
    if measures != anchor["measures"]:
        raise InvalidEvidence("Oracle.ReviewedValueConflict", {"calculated": measures, "reviewed": anchor["measures"]})
    for metric in measures.values():
        decode(metric)
    bounds = [[exact(min(p[i] for p in points)) for i in range(3)],
              [exact(max(p[i] for p in points)) for i in range(3)]]
    queries = query_truth(anchor, points)
    definition = {"world_points_mm": [[exact(v) for v in p] for p in points],
                  "vertex_point_ids": [110+i for i in range(len(points))],
                  "edge_curve_ids": [203+10*i for i in range(len(anchor["edge_ends"]))],
                  "edge_vertex_indices": anchor["edge_ends"], "face_paths": anchor["faces"],
                  "loop_paths": anchor["loops"], "edge_same_sense": True,
                  "face_same_sense": True, "bound_orientation": True,
                  "source_length_scale_mm": exact(F(anchor["source_length_scale_mm"])),
                  "line_endpoint_parameters": [exact(0), exact(1)]}
    if not anchor["closed"]:
        origin = tuple(F(v) for v in anchor["plane_origin"])
        source_scale = F(anchor["source_length_scale_mm"])
        source_uv = [(dot(sub(p, origin), xaxis) / source_scale,
                      dot(sub(p, origin), yaxis) / source_scale) for p in points]
        definition.update(plane_origin_mm=[exact(v) for v in origin],
                          source_uv_points=[[exact(v) for v in p] for p in source_uv],
                          source_uv_window=[[exact(min(p[i] for p in source_uv)) for i in range(2)],
                                            [exact(max(p[i] for p in source_uv)) for i in range(2)]])
    return {"case_id": case_id, "kind": "ExactShape", "role": anchor["role"], "measures": measures,
            "canonical_units": {"length": "mm", "area": "mm^2", "volume": "mm^3",
                                "parameter": "dimensionless"},
            "material_volume": {"applicability": "Applicable" if volume is not None and
                                anchor["role"] == "MaterialSolid" else "NotApplicable",
                                "value": exact(volume) if anchor["role"] == "MaterialSolid" else None,
                                "reason": "material solid" if anchor["role"] == "MaterialSolid" else "surface model"},
            "centroid": [exact(v) for v in centroid], "bounds": bounds, "queries": queries,
            "source_definition": definition,
            "source_topology": {"vertices": len(points), "edges": len(anchor["edge_ends"]),
                                "faces": len(anchor["faces"])}, "methods": methods}


RI_TYPES = {"CARTESIAN_POINT", "DIRECTION", "VECTOR", "LINE", "PLANE", "AXIS2_PLACEMENT_3D",
            "VERTEX_POINT", "EDGE_CURVE", "ORIENTED_EDGE", "EDGE_LOOP", "FACE_BOUND", "FACE_OUTER_BOUND",
            "ADVANCED_FACE", "FACE_SURFACE", "OPEN_SHELL", "CLOSED_SHELL", "MANIFOLD_SOLID_BREP",
            "SHELL_BASED_SURFACE_MODEL"}


def orphan_ids(records) -> list[int]:
    index = by_id(records)
    reached, pending = set(), [20]
    while pending:
        entity = pending.pop()
        if entity in reached:
            continue
        reached.add(entity)
        if entity in index:
            pending.extend(index[entity].references)
    return sorted(record.entity_id for record in records if record.kind in RI_TYPES and record.entity_id not in reached)


def _path(index, loop_id: int) -> list[tuple[int, int]]:
    path = []
    for oe_token in aggregate(index[loop_id].args[1]):
        oe = index[ref(oe_token)]
        edge = index[ref(oe.args[3])]
        a, b = ref(edge.args[1]), ref(edge.args[2])
        path.append((a, b) if oe.args[4] == ".T." else (b, a))
    return path


def input_facts(case_id: str, data: bytes) -> dict:
    records = inventory(data)
    key = case_id[-3:]
    ids = [r.entity_id for r in records]
    if key == "N02":
        return {"occurrences": len(records), "unique_ids": len(set(ids)),
                "duplicate_id": next(i for i in ids if ids.count(i) > 1)}
    index = by_id(records)

    def point(entity: int):
        return vector(index[entity].args[1])

    def distance_squared(q, line_id):
        line = index[line_id]
        p = point(ref(line.args[1]))
        v = index[ref(line.args[2])]
        direction = vector(index[ref(v.args[1])].args[1])
        delta = sub(q, p)
        return dot(delta, delta) - dot(delta, direction) ** 2 / dot(direction, direction)

    if key == "N01":
        return {"oid_tail": int(data.split(b"{1 0 10303 403 2 1 ")[1].split(b"}")[0])}
    if key == "N03":
        return {"dangling": sorted({r for record in records for r in record.references if r not in index})}
    if key == "N04":
        return {"required_name_token": index[3].args[0]}
    if key in {"N05", "N06", "N07"}:
        return ({"derived_start_token": index[204].args[1]} if key == "N05" else
                {"boolean_token": index[204].args[4]} if key == "N06" else
                {"edge_start_kind": index[ref(index[203].args[1])].kind})
    if key == "N08":
        return {"orphans": orphan_ids(records)}
    if key == "N09":
        faces = [ref(v) for v in aggregate(index[22].args[1])]
        return {"duplicate_face": next(v for v in faces if faces.count(v) > 1)}
    if key == "N10":
        return {"apd_suffix": "_mim_lf" if index[2].args[1].endswith("_mim_lf'") else ""}
    if key == "N11":
        d = vector(index[200].args[1])
        return {"direction_norm_squared": exact(dot(d, d))}
    if key in {"N12", "N19"}:
        return {"magnitude": exact(F(index[201].args[2]))}
    if key == "N13":
        c = cross(vector(index[131].args[1]), vector(index[132].args[1]))
        return {"axis_cross_norm_squared": exact(dot(c, c))}
    if key == "N14":
        return {"si_dimensions_token": index[10].raw.split("NAMED_UNIT(")[1].split(")")[0]}
    if key == "N15":
        return {"prefix": index[11].raw.split("SI_UNIT(")[1].split(",")[0]}
    if key == "N16":
        return {"context_role": index[6].args[0]}
    if key == "N17":
        return {"explicit_outer_bounds": sum(r.kind == "FACE_OUTER_BOUND" for r in records)}
    if key == "N18":
        return {"face_kind": index[302].kind}
    if key == "N20":
        return {"endpoint_line_distance_squared": exact(distance_squared(point(100), 202))}
    if key == "N21":
        return {"edge_plane_distance": exact(abs(point(130)[2] - point(100)[2]))}
    if key == "N22":
        return {"same_sense": index[203].args[4]}
    if key in {"N23", "N24"}:
        path = _path(index, 300)
        facts = {"path_connected": all(a[1] == b[0] for a, b in zip(path, path[1:])),
                 "loop_closed": path[0][0] == path[-1][1]}
        if key == "N24":
            facts["end_distance_squared"] = exact(distance_squared(point(101), 232))
        return facts
    if key == "N25":
        uv = [(F(2), F(2)), (F(2), F(4)), (F(6), F(4)), (F(6), F(2))]
        return {"effective_hole_area": exact(signed_area(uv) * (1 if index[311].args[2] == ".T." else -1))}
    if key == "N26":
        return {"free_edges": len(_path(index, 300)), "free_length": exact(sum(
            (F(index[201+10*i].args[2]) for i in range(4)), F(0)))}
    if key == "N27":
        sense = 1 if index[507].args[3] == ".T." else -1
        return {"selected_bottom_normal": [int(v * sense) for v in vector(index[501].args[1])]}
    if key == "N28":
        # Remaining axis-aligned face areas/perimeter are independent fixture facts.
        active = [ref(v) for v in aggregate(index[22].args[1])]
        face_areas = {507: 200, 517: 200, 527: 300, 537: 300, 547: 600, 557: 600}
        return {"orphans": orphan_ids(records), "remaining_area": exact(sum(face_areas[v] for v in active)),
                "free_length": exact(2 * (F(20) + F(10)))}
    if key == "N29":
        return {"names_unique": index[13].args[2] != index[14].args[2]}
    if key in {"N30", "N32", "N33"}:
        record = index[13 if key == "N30" else 18]
        number = F(record.args[0].split("(", 1)[1][:-1])
        return {"uncertainty" if key == "N30" else "factor": exact(number)}
    if key == "N31":
        return {"declared_length_exponent": exact(F(index[17].args[0]))}
    if key == "N34":
        target = ref(index[18].args[1])
        cycle = [19, 18, target]
        return {"unit_cycle": cycle, "mm_anchor_present": 13 in index and ref(index[13].args[1]) == 10}
    if key == "N35":
        return {"category": index[15].args[0]}
    if key == "N36":
        return {"discipline": index[3].args[2]}
    raise InvalidEvidence("Oracle.UnknownCase", case_id)


def verify_input_definition(case_id: str, data: bytes, anchor: dict) -> dict:
    records = inventory(data)
    if len(records) != anchor["record_count"]:
        raise InvalidEvidence("Preparation.OccurrenceInventoryMismatch", [len(records), anchor["record_count"]])
    if anchor["kind"] == "Negative":
        observed = input_facts(case_id, data)
        expected = anchor["facts"]
        if "orphans" in expected:
            observed["orphans"] = sorted(observed["orphans"])
            expected = {**expected, "orphans": sorted(expected["orphans"])}
        if observed != expected:
            raise InvalidEvidence("Reference.NegativeFactConflict", {"observed": observed, "expected": expected})
        return {"method": "frozen fixture token/identity and rational input facts", "facts": observed}
    index = by_id(records)
    if "header_prefix" in anchor and not data.startswith(anchor["header_prefix"].encode("ascii")):
        raise InvalidEvidence("Reference.InputDefinitionMismatch", "authored HEADER")
    for entity_id, raw in anchor.get("literal_records", {}).items():
        if int(entity_id) not in index or index[int(entity_id)].raw != raw:
            raise InvalidEvidence("Reference.InputDefinitionMismatch", {"source_id": int(entity_id),
                                                                        "reason": "authored metadata/role/units"})
    scale = F(anchor["source_length_scale_mm"])
    # Read the unit declaration, don't assume the caller's anchor scale is the input scale.
    context_raw = index[12].raw
    if "((#19,#11))" in context_raw:
        factor = index[18].args[0]
        actual_scale = F(factor.split("(")[1][:-1])
        if ref(index[18].args[1]) != 10:
            raise InvalidEvidence("Reference.InputDefinitionMismatch", "factor must terminate in mm")
    else:
        actual_scale = F(10) if ".CENTI." in index[10].raw else F(1)
    if actual_scale != scale:
        raise InvalidEvidence("Reference.InputDefinitionMismatch", "source unit scale")
    for i, expected in enumerate(anchor["world_points"]):
        actual = tuple(v * actual_scale for v in vector(index[100+i].args[1]))
        if actual != tuple(F(v) for v in expected):
            raise InvalidEvidence("Reference.InputDefinitionMismatch",
                                  {"source_id": 100+i, "actual": list(map(str, actual))})
        if ref(index[110+i].args[1]) != 100 + i:
            raise InvalidEvidence("Reference.InputDefinitionMismatch", "vertex identity")
    for i, (a, b) in enumerate(anchor["edge_ends"]):
        edge = index[203+10*i]
        if (edge.kind != "EDGE_CURVE" or edge.args[1:3] != (f"#{110+a}", f"#{110+b}") or
                edge.args[4] != ".T." or edge.args[3] != f"#{202+10*i}"):
            raise InvalidEvidence("Reference.InputDefinitionMismatch", "edge identity/sense")
        line = index[ref(edge.args[3])]
        if line.kind != "LINE" or line.args[2] != f"#{201+10*i}":
            raise InvalidEvidence("Reference.InputDefinitionMismatch", "line identity")
        p = tuple(v * scale for v in vector(index[ref(line.args[1])].args[1]))
        vect = index[ref(line.args[2])]
        direction = vector(index[ref(vect.args[1])].args[1])
        magnitude = F(vect.args[2]) * scale
        if dot(direction, direction) != 1:
            raise InvalidEvidence("Reference.InputDefinitionMismatch", "fixture unit direction")
        start = tuple(F(v) for v in anchor["world_points"][a])
        end = tuple(F(v) for v in anchor["world_points"][b])
        if p != start or add(p, tuple(v * magnitude for v in direction)) != end:
            raise InvalidEvidence("Reference.InputDefinitionMismatch", "line endpoints/parameter 0..1")
    if orphan_ids(records):
        raise InvalidEvidence("Reference.InputDefinitionMismatch", "orphan representation items")
    if anchor["closed"]:
        face_ids = [ref(v) for v in aggregate(index[22].args[1])]
        if face_ids != [507+10*i for i in range(6)]:
            raise InvalidEvidence("Reference.InputDefinitionMismatch", "face set")
        for i, expected_face in enumerate(anchor["faces"]):
            face, loop = index[507+10*i], 505+10*i
            path = _path(index, loop)
            if [a-110 for a, _ in path] != expected_face or face.args[3] != ".T.":
                raise InvalidEvidence("Reference.InputDefinitionMismatch", "face path/sense")
            origin = vector(index[ref(index[503+10*i].args[1])].args[1])
            normal, xaxis = vector(index[501+10*i].args[1]), vector(index[502+10*i].args[1])
            if (index[504+10*i].args != ("''", f"#{503+10*i}") or
                    index[503+10*i].args[2:] != (f"#{501+10*i}", f"#{502+10*i}") or
                    index[506+10*i].args != ("''", f"#{loop}", ".T.")):
                raise InvalidEvidence("Reference.InputDefinitionMismatch", "box frame/bound references")
            pa, pb, pc = [vector(index[100+j].args[1]) for j in expected_face[:3]]
            if any(dot(sub(vector(index[100+j].args[1]), origin), normal) != 0 for j in expected_face):
                raise InvalidEvidence("Reference.InputDefinitionMismatch", "box edge/plane containment")
            outward = cross(sub(pb, pa), sub(pc, pa))
            if dot(normal, outward) <= 0 or dot(normal, normal) != 1 or dot(xaxis, xaxis) != 1:
                raise InvalidEvidence("Reference.InputDefinitionMismatch", "outward plane frame")
            if face.kind != "ADVANCED_FACE" or face.args[1:3] != (f"(#{506+10*i})", f"#{504+10*i}"):
                raise InvalidEvidence("Reference.InputDefinitionMismatch", "face geometry/bound owner")
            projected = [(dot(sub(vector(index[100+j].args[1]), origin), xaxis),
                          dot(sub(vector(index[100+j].args[1]), origin), cross(normal, xaxis)))
                         for j in expected_face]
            if signed_area(projected) <= 0 or dot(normal, xaxis) != 0:
                raise InvalidEvidence("Reference.InputDefinitionMismatch", "face frame")
        solid = index[21].kind == "MANIFOLD_SOLID_BREP"
        if solid != (anchor["role"] == "MaterialSolid"):
            raise InvalidEvidence("Contract.ApplicabilityConflict", "solid/surface role")
    else:
        origin = tuple(v * scale for v in vector(index[130].args[1]))
        if origin != tuple(F(v) for v in anchor["plane_origin"]):
            raise InvalidEvidence("Reference.InputDefinitionMismatch", "plane parameter origin")
        for loop_id, expected_loop in zip([300, 310], anchor["loops"]):
            if [a-110 for a, _ in _path(index, loop_id)] != expected_loop:
                raise InvalidEvidence("Reference.InputDefinitionMismatch", "plane loop path")
        if (vector(index[131].args[1]) != (0, 0, 1) or
                vector(index[132].args[1]) != (F(3, 5), F(4, 5), 0) or
                index[133].args != ("''", "#130", "#131", "#132") or index[134].args != ("''", "#133")):
            raise InvalidEvidence("Reference.InputDefinitionMismatch", "plane frame/parameter basis")
        bounds = "(#301,#311)" if len(anchor["loops"]) == 2 else "(#301)"
        if (index[302].kind != "ADVANCED_FACE" or index[302].args != ("''", bounds, "#134", ".T.") or
                index[301].kind != "FACE_OUTER_BOUND" or index[301].args != ("''", "#300", ".T.")):
            raise InvalidEvidence("Reference.InputDefinitionMismatch", "plane region/selected side")
        if len(anchor["loops"]) == 2 and index[311].args != ("''", "#310", ".T."):
            raise InvalidEvidence("Reference.InputDefinitionMismatch", "hole side")
    return {"method": "independent reviewed world/frame/identity anchors and exact affine equations",
            "record_occurrences": len(records), "unique_entity_ids": len(index)}
