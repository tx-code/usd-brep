# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Independent fixed-corpus checks: pinned EXPRESS declarations + original bytes + ticket-01 math.

This is not a product loader or a second general EXPRESS interpreter. It accepts only the frozen
one-record-per-line fixture spelling. Physical area is recomputed by 3D vector areas and triangle
centroids, independently of the native projected-coordinate shoelace implementation.
"""
from __future__ import annotations

from collections import Counter
from fractions import Fraction as F
from math import isqrt
from pathlib import Path
import re

from tools.step_import_validation.common import InvalidEvidence, digest, load_json
from tools.step_import_validation.oracle import add, sub, dot, cross, exact, decode, input_facts
from tools.step_import_validation.records import inventory, by_id, ref, vector, aggregate
from tools.step_inspect_validation.oracle import position
from tools.step_semantic_validation.oracle import compare as compare_semantic, REJECTIONS, PREFIX
from tools.step_import_validation.records import SIMPLE, split_args

def facets(record):
    if record.kind != "COMPLEX":
        return {record.kind: record.args}
    text, offset, result = record.raw[1:-1], 0, {}
    while offset < len(text):
        while offset < len(text) and text[offset].isspace():
            offset += 1
        match = re.match(r"([A-Z][A-Z_0-9]*)\(", text[offset:])
        assert match, "test-unit constituent"
        start = offset + match.end()
        i, depth, quoted = start, 1, False
        while depth:
            ch = text[i]
            if ch == "'":
                if quoted and i+1 < len(text) and text[i+1] == "'":
                    i += 2
                    continue
                quoted = not quoted
            elif not quoted:
                depth += (ch == "(") - (ch == ")")
            i += 1
        result[match[1]] = tuple(split_args(text[start:i-1]))
        offset = i
    return result


def primary_length(index):
    contexts = [facets(r)["GLOBAL_UNIT_ASSIGNED_CONTEXT"] for r in index.values()
                if "GLOBAL_UNIT_ASSIGNED_CONTEXT" in facets(r)]
    context, = contexts
    unit, = [ref(v) for v in aggregate(context[0]) if "LENGTH_UNIT" in facets(index[ref(v)])]
    return unit


def dimensions(index, unit):
    parts = facets(index[unit])
    if "SI_UNIT" in parts:
        return (F(1), *([F(0)] * 6)) if parts["SI_UNIT"][1] == ".METRE." else (F(0),) * 7
    return tuple(F(v) for v in index[ref(parts["NAMED_UNIT"][0])].args)


def scale_for(index, start):
    # Expand a physical value all the way to a metre root. No native output or shared product memo is used.
    factor, unit, seen = F(1), start, set()
    while unit not in seen:
        seen.add(unit)
        parts = facets(index[unit])
        assert "LENGTH_UNIT" in parts and dimensions(index, unit) == (F(1), *([F(0)] * 6))
        if "SI_UNIT" in parts:
            prefix, name = parts["SI_UNIT"]
            assert name == ".METRE."
            return factor * F(10) ** (PREFIX[None if prefix == "$" else prefix.strip(".")] + 3)
        measure = index[ref(parts["CONVERSION_BASED_UNIT"][1])]
        value = SIMPLE.fullmatch(measure.args[0])
        assert value and value[1] in {"LENGTH_MEASURE", "POSITIVE_LENGTH_MEASURE", "NON_NEGATIVE_LENGTH_MEASURE"}
        numeric = F(value[2])
        assert numeric > 0
        factor *= numeric
        unit = ref(measure.args[1])
    raise AssertionError("cyclic test-unit definition")


def source_units(data):
    index = by_id(inventory(data))
    primary = primary_length(index)
    definitions = []
    for r in index.values():
        parts = facets(r)
        if "CONVERSION_BASED_UNIT" not in parts:
            continue
        factor = index[ref(parts["CONVERSION_BASED_UNIT"][1])]
        value = SIMPLE.fullmatch(factor.args[0])
        definitions.append(dict(source_unit=str(r.entity_id), source_dimensions=str(ref(parts["NAMED_UNIT"][0])),
            source_factor=str(factor.entity_id), source_base_unit=str(ref(factor.args[1])), value_type=value[1],
            factor_value=F(value[2]), scale_mm=scale_for(index, r.entity_id),
            dimensions=dimensions(index, r.entity_id)))
    return primary, scale_for(index, primary), definitions


REFERENCE = Path(__file__).parent / "contracts/n2635-rules-v1.json"
REFERENCE_SHA = "970d8ac65d56cad561004e72865fb3340000a6431e967b9a9bc9bd98cc6639b6"
NEW_REJECTIONS = {f"S203-N{i:02}" for i in [10, 11, 12, 13, 19, 20, 21, 22, 23, 24, 25, 26, 27, 29, 30, 31, 32, 33, 34]}
LATER = ("native_build", "full_import", "target_uv", "tolerance_setting",
         "shell_self_intersection", "material_volume")


def require(condition, detail):
    if not condition:
        raise InvalidEvidence("Ticket04.ObservationConflict", detail)


def reference():
    require(digest(REFERENCE.read_bytes()) == REFERENCE_SHA, "independent rule witness identity")
    return load_json(REFERENCE)


def root(value):
    result = F(isqrt(value.numerator), isqrt(value.denominator))
    require(result * result == value, "frozen corpus truth expects rational radicals")
    return result


def times(v, scale):
    return tuple(x * scale for x in v)


def clause_catalog(witness):
    result = {}
    for name, entity in witness["entities"].items():
        for kind in ("WHERE", "UNIQUE", "INVERSE"):
            for clause in entity["sections"].get(kind, {}).get("clauses", []):
                result[(kind, name + "." + clause["name"])] = (clause["line"], name)
    return result


def check_rules(observed, witness, index):
    populations = observed["source_semantics"]["snapshot"]["type_populations"]
    catalog = clause_catalog(witness)
    active = Counter()
    for rule in observed["rules"]:
        kind, name = rule["kind"], rule["name"]
        require(rule["evaluation"] == "Evaluated" and rule["logical_value"] in {"TRUE", "FALSE", "UNKNOWN"},
                "uncomputed expression disguised as source logical")
        require(rule["proof"], "missing clause proof")
        require(all(str(i) in {str(v) for v in index} for i in rule["witness_ids"]), "unbound witness")
        if kind in {"WHERE", "UNIQUE", "INVERSE"}:
            require((kind, name) in catalog, "unknown local clause")
            line, type_name = catalog[(kind, name)]
            require(rule["schema_line"] == line, "wrong schema clause line")
            require(rule["populations"] == {type_name: [rule["owner_id"]] if rule["owner_id"] else []},
                    "local owner population; complete populations retained once in semantic snapshot")
            require(rule["population_scope"] == ("OwnerInstance" if rule["owner_id"] else "FullFile"),
                    "population scope")
            require(rule["owner_id"] in populations[type_name] if populations[type_name] else
                    rule["owner_id"] is None, "local clause owner")
            active[(kind, name, rule["owner_id"])] += 1
        if kind == "TYPE_WHERE":
            require(name in {"DIMENSION_COUNT.WR1", "POSITIVE_LENGTH_MEASURE.WR1",
                             "NON_NEGATIVE_LENGTH_MEASURE.WR1"}, "named predicate")
        if kind == "GLOBAL":
            declarations = {r["name"]: r for r in witness["rules"]}
            require(name in declarations, "unknown global rule")
            meta = declarations[name]
            require(rule["schema_line"] == meta["line"], "global source line")
            expected = {t: populations.get(t, []) for t in meta["for"]}
            require(rule["populations"] == expected, "global FOR population/subtype coverage")
    if observed["checks"]["local_rules"] == "Passed":
        expected = Counter()
        for (kind, name), (_, type_name) in catalog.items():
            for owner in populations[type_name] or [None]:
                expected[(kind, name, owner)] += 1
        require(active == expected, "not all ancestor WHERE/UNIQUE/INVERSE clauses evaluated")
    if observed["checks"]["global_rules_47"] == "Passed":
        rules = [r for r in observed["rules"] if r["kind"] == "GLOBAL"]
        require([r["name"] for r in rules] == [r["name"] for r in witness["rules"]], "47 rule order/coverage")
        require(Counter(r["applicability"] for r in rules) ==
                Counter({"Mandatory": 5, "Conditional": 1, "Guarded": 41}), "5/1/41 classification")
        require(all(r["logical_value"] == "TRUE" for r in rules), "false/unknown rule granted a pass")
        g36, g47 = rules[35], rules[46]
        require(g36["facts"]["correlation_UNKNOWN_count"] == "1" and
                g36["facts"]["failure_QUERY_count"] == "0", "SDR/PDR or QUERY UNKNOWN semantics")
        require(g36["populations"]["PROPERTY_DEFINITION_REPRESENTATION"] == ["9"], "SDR omitted from PDR")
        require(g47["populations"]["LOOP"] == populations["EDGE_LOOP"] and
                len(g47["populations"]) == 59, "EDGE_LOOP omitted from LOOP / 59 FOR")
        for loop in populations["LOOP"]:
            require(any(rec.kind in {"FACE_BOUND", "FACE_OUTER_BOUND"} and ref(rec.args[1]) == int(loop)
                        for rec in index.values()), "missing independent dependency anchor")
        ri_rules = [r for r in observed["rules"] if r["name"] == "REPRESENTATION_ITEM.WR1"]
        require(len(ri_rules) == len(populations["REPRESENTATION_ITEM"]) and
                all(r["facts"]["using_representation_count"] == "1" for r in ri_rules), "indirect RI uses lost")


def check_units(observed, index):
    conversions = [r for r in index.values() if "CONVERSION_BASED_UNIT" in facets(r)]
    local_ok = observed["checks"]["local_rules"] == "Passed"
    global_ok = observed["checks"]["global_rules_47"] == "Passed"
    expected = ("NotRun" if not conversions or not local_ok or not global_ok else
                "Failed" if observed["diagnostics"] else "Passed")
    require(observed["checks"]["conversion_units"] == expected, "explicit conversion interpretation phase")
    if not local_ok:
        for rule in observed["rules"]:
            if rule["name"] == "CONVERSION_BASED_UNIT.WR1" and rule["logical_value"] == "FALSE":
                unit = int(rule["owner_id"])
                factor = index[ref(facets(index[unit])["CONVERSION_BASED_UNIT"][1])]
                base = ref(factor.args[1])
                require(dimensions(index, unit) != dimensions(index, base),
                        "independent conversion dimension counterexample")
        return
    for metric in observed["metrics"]:
        if metric["kind"] == "unit_scale":
            unit = int(metric["owner_id"])
            factor = index[ref(facets(index[unit])["CONVERSION_BASED_UNIT"][1])]
            value = F(SIMPLE.fullmatch(factor.args[0])[2])
            base = scale_for(index, ref(factor.args[1]))
            require({k: decode(v) for k, v in metric["exact"].items()} ==
                    dict(factor_value=value, base_scale_mm=base, scale_mm=value * base),
                    "independent exact scale evidence")
        if metric["kind"] == "unit_cycle":
            ids = list(map(int, metric["witness_ids"]))
            require(len(ids) >= 3 and len(ids) % 2 == 1 and ids[0] == ids[-1], "closed concrete unit dependency cycle")
            for unit, factor, target in zip(ids[::2], ids[1::2], ids[2::2]):
                require(ref(facets(index[unit])["CONVERSION_BASED_UNIT"][1]) == factor and
                        ref(index[factor].args[1]) == target, "cycle follows original factor and unit fields")
    if expected == "Passed":
        expected_rows = []
        for record in conversions:
            parts = facets(record)
            factor = index[ref(parts["CONVERSION_BASED_UNIT"][1])]
            value = SIMPLE.fullmatch(factor.args[0])
            expected_rows.append(dict(unit_id=str(record.entity_id), dimensions_id=str(ref(parts["NAMED_UNIT"][0])),
                factor_id=str(factor.entity_id), base_unit_id=str(ref(factor.args[1])), value_type=value[1],
                factor_value=exact(F(value[2])), scale_mm=exact(scale_for(index, record.entity_id)),
                dimensions=[exact(v) for v in dimensions(index, record.entity_id)]))
        require(sorted(observed["unit_conversions"], key=lambda v: v["unit_id"]) ==
                sorted(expected_rows, key=lambda v: v["unit_id"]),
                "complete independently expanded conversion definitions")


def check_geometry(observed, index, truth):
    if not observed["edges"] and not observed["faces"]:
        require(not observed["qualified_source_evidence"], "missing qualified geometry")
        return
    scale = scale_for(index, primary_length(index))

    def point(i):
        return times(vector(index[i].args[1]), scale)

    def vp(i):
        return point(ref(index[i].args[1]))

    def line(i):
        record = index[i]
        vect = index[ref(record.args[2])]
        return point(ref(record.args[1])), vector(index[ref(vect.args[1])].args[1]), F(vect.args[2]) * scale

    def edge(i):
        record = index[i]
        return vp(ref(record.args[1])), vp(ref(record.args[2])), ref(record.args[3])

    def frame(i):
        record = index[i] if index[i].kind == "PLANE" else index[ref(index[i].args[2])]
        placement = index[ref(record.args[1])]
        origin = point(ref(placement.args[1]))
        n = vector(index[ref(placement.args[2])].args[1])
        x = vector(index[ref(placement.args[3])].args[1])
        x = sub(x, times(n, dot(x, n) / dot(n, n)))
        y = cross(n, x)
        return origin, x, y, n

    def residual(p, plane):
        origin, _, _, n = frame(plane)
        return dot(sub(p, origin), n)**2 / dot(n, n)

    for actual in observed["edges"]:
        i = int(actual["id"])
        p, q, curve = edge(i)
        a, d, magnitude = line(curve)
        require(int(actual["line_id"]) == curve and int(actual["start_id"]) == ref(index[i].args[1]) and
                int(actual["end_id"]) == ref(index[i].args[2]), "source edge/curve/vertex identity")
        require(actual["same_sense"] == (index[i].args[4] == ".T."), "source EC sense lost")
        require(decode(actual["t_start_coefficient"]) == dot(sub(p, a), d) / magnitude and
                decode(actual["t_end_coefficient"]) == dot(sub(q, a), d) / magnitude and
                decode(actual["t_divisor_squared"]) == dot(d, d), "dimensionless source LINE t")
        require(decode(actual["length_squared_mm"]) == dot(sub(p, q), sub(p, q)), "edge length / unit scaling")
    for metric in observed["metrics"]:
        values = {k: decode(v) for k, v in metric["exact"].items()}
        kind = metric["kind"]
        if kind in {"unit_scale", "unit_cycle"}:
            continue
        if kind == "endpoint_curve":
            p, q, curve = edge(int(metric["owner_id"]))
            a, d, _ = line(curve)
            expected = [dot(cross(sub(v, a), d), cross(sub(v, a), d)) / dot(d, d) for v in (p, q)]
            require(values == dict(start_distance_squared_mm=expected[0], end_distance_squared_mm=expected[1],
                                   threshold_squared_mm=F(1, 10**14)), "endpoint residual / explicit Q")
        elif kind == "edge_surface":
            ec, _, plane = map(int, metric["witness_ids"])
            p, q, curve = edge(ec)
            a, d, _ = line(curve)
            c0, c1 = (add(a, times(d, dot(sub(v, a), d) / dot(d, d))) for v in (p, q))
            expected = dict(vertex_start_squared_mm=residual(p, plane), vertex_end_squared_mm=residual(q, plane),
                            line_start_squared_mm=residual(c0, plane), line_end_squared_mm=residual(c1, plane),
                            threshold_squared_mm=F(1, 10**14))
            require(values == expected, "edge/PLANE residual, including defining LINE segment")
        elif kind == "bound_orientation":
            bound = index[int(metric["owner_id"])]
            face = int(metric["witness_ids"][0])
            origin, x, y, n = frame(face)
            oe = [index[ref(t)] for t in aggregate(index[ref(bound.args[1])].args[1])]
            points = [vp(ref(index[ref(v.args[3])].args[1 if v.args[4] == '.T.' else 2])) for v in oe]
            area = (F(0),)*3
            for a, b in zip(points, points[1:] + points[:1]):
                area = add(area, times(cross(sub(a, origin), sub(b, origin)), F(1, 2)))
            metric2 = dot(x, x) * dot(y, y)
            coefficient = dot(area, cross(x, y)) / metric2 * (1 if bound.args[2] == '.T.' else -1)
            require(values == dict(signed_area_coefficient=coefficient, metric_squared=metric2,
                                   face_same_sense=1 if index[face].args[3] == '.T.' else -1),
                    "3D directed vector area")
        elif kind == "shell_incidence":
            uses = Counter()
            for face in observed["faces"]:
                for loop in face["loops"]:
                    uses.update(loop["edge_ids"])
            require(values == dict(boundary_edges=sum(v == 1 for v in uses.values()),
                                   paired_edges=sum(v == 2 for v in uses.values())), "source shell incidence")
        else:
            require(False, "unrecognised metric")
    if not observed["qualified_source_evidence"]:
        return
    area_sum = F(0)
    all_points = []
    for actual in observed["faces"]:
        face_id = int(actual["id"])
        origin, x, y, n = frame(face_id)
        require([decode(v) for v in actual["origin_mm"]] == list(origin) and
                [decode(v) for v in actual["basis_x"]] == list(x) and
                [decode(v) for v in actual["basis_y"]] == list(y) and
                [decode(v) for v in actual["basis_normal"]] == list(n), "exact source PLANE frame")
        require(actual["same_sense"] == (index[face_id].args[3] == '.T.') and
                decode(actual["source_length_scale_mm"]) == scale, "face sense / source units")
        require({loop["bound_id"] for loop in actual["loops"]} ==
                {str(ref(t)) for t in aggregate(index[face_id].args[1])}, "face bound set coverage")
        area_vector, weighted, total_weight, face_points = (F(0),)*3, (F(0),)*3, F(0), []
        for loop in actual["loops"]:
            bound = index[int(loop["bound_id"])]
            oe_ids = [ref(t) for t in aggregate(index[ref(bound.args[1])].args[1])]
            expected_vertices, expected_edges = [], []
            for oe_id in oe_ids:
                oe = index[oe_id]
                ec = ref(oe.args[3])
                expected_edges.append(str(ec))
                expected_vertices.append(str(ref(index[ec].args[1 if oe.args[4] == '.T.' else 2])))
            require(loop["oriented_edge_ids"] == list(map(str, oe_ids)) and loop["edge_ids"] == expected_edges and
                    loop["vertex_ids"] == expected_vertices, "source occurrence order and identity")
            require(loop["orientation"] == (bound.args[2] == '.T.') and
                    loop["outer"] == (bound.kind == 'FACE_OUTER_BOUND') and
                    loop["edge_orientations"] == [index[v].args[4] == '.T.' for v in oe_ids], "bound/OE sense")
            points = [vp(int(v)) for v in expected_vertices]
            face_points += points
            projected = [(dot(sub(p, origin), x) / dot(x, x), dot(sub(p, origin), y) / dot(y, y)) for p in points]
            require([[decode(v) for v in p] for p in loop["projected"]] == [list(p) for p in projected],
                    "source projected coordinates")
            orientation = 1 if loop["orientation"] else -1
            for j in range(1, len(points)-1):
                a, b, c = points[0], points[j], points[j+1]
                vector_area = times(cross(sub(b, a), sub(c, a)), F(orientation, 2))
                area_vector = add(area_vector, vector_area)
                weight = dot(vector_area, n)
                total_weight += weight
                weighted = add(weighted, times(add(add(a, b), c), weight / 3))
        area2 = dot(area_vector, area_vector)
        centroid = times(weighted, 1 / total_weight)
        bounds = [[min(p[k] for p in face_points) for k in range(3)],
                  [max(p[k] for p in face_points) for k in range(3)]]
        require(decode(actual["projected_region_area_squared_mm"]) == area2 and
                [decode(v) for v in actual["projected_region_centroid_mm"]] == list(centroid) and
                [[decode(v) for v in p] for p in actual["projected_region_bounds_mm"]] == bounds,
                "independent 3D fan triangulation area / centroid / bounds")
        area_sum += root(area2)
        all_points += face_points
    require(len(observed["edges"]) == truth["source_topology"]["edges"] and
            len(observed["faces"]) == truth["source_topology"]["faces"], "whole source topology coverage")
    require(area_sum == decode(truth["measures"]["area"]), "ticket-01 independent analytic area")
    perimeter = sum(root(decode(e["length_squared_mm"])) for e in observed["edges"])
    require(perimeter == decode(truth["measures"]["perimeter"]), "ticket-01 unique-edge length")
    bounds = [[min(p[k] for p in all_points) for k in range(3)], [max(p[k] for p in all_points) for k in range(3)]]
    require(bounds == [[decode(v) for v in p] for p in truth["bounds"]], "ticket-01 analytic bounds")
    if len(observed["faces"]) == 1:
        require(observed["faces"][0]["projected_region_centroid_mm"] == truth["centroid"], "analytic plane centroid")
        definition = truth["source_definition"]
        uv_by_id = dict(zip(map(str, definition["vertex_point_ids"]), definition["source_uv_points"]))
        face = observed["faces"][0]
        for loop in face["loops"]:
            for vertex, p in zip(loop["vertex_ids"], loop["projected"]):
                uv = [decode(p[0])*root(dot(x, x))/scale, decode(p[1])*root(dot(y, y))/scale]
                require(uv == [decode(v) for v in uv_by_id[vertex]], "source PLANE origin / UV is not re-centred")


def compare(case, observation, exit_code, data, ledger, witness=None):
    witness = witness or reference()
    upstream = observation["source_semantics"]
    upstream_exit = {"Checked": 0, "Deferred": 7, "Unsupported": 2, "Rejected": 1, "ResourceLimit": 3,
                     "SourceFailure": {"Rejected": 1, "Unsupported": 2, "ResourceLimit": 3, "IoFailure": 4}.get(
                         upstream["source"]["status"], 6)}[upstream["status"]]
    compare_semantic(case, upstream, upstream_exit, data, ledger)
    require(observation["format_version"] == "step-planar.v1" and
            observation["scope"] == "PlanarSourceRulesAndRegions", "reader scope")
    require(all(observation["checks"][key] == "NotRun" for key in LATER) and observation["model"] is None,
            "later ticket / native model overclaim")
    require(observation["quality"] == {"id": "Q-P0-mm-01", "consistency_mm": exact(F(1, 10**7)),
                                       "production_default": False, "repair": "None"}, "quality contract")
    key = case["case_id"]
    status = "UpstreamFailure" if key in REJECTIONS else "Rejected" if key in NEW_REJECTIONS else "Checked"
    require(observation["status"] == status and observation["qualified_source_evidence"] == (status == "Checked"),
            "stage outcome / qualification")
    require(exit_code == (upstream_exit if status == "UpstreamFailure" else
                          {"Checked": 0, "Rejected": 1, "Deferred": 7}[status]), "actual exit")
    if status == "UpstreamFailure":
        require(not observation["rules"] and not observation["metrics"] and
                all(observation["checks"][k] == "NotRun" for k in
                    ("local_rules", "global_rules_47", "planar_regions")),
                "upstream block entered rules/geometry")
        require([d["key"] for d in observation["diagnostics"]] == [case["expected"]["diagnostic_key"]],
                "upstream diagnostic")
    else:
        index = by_id(inventory(data))
        check_rules(observation, witness, index)
        check_units(observation, index)
        check_geometry(observation, index, case["truth"])
        if status == "Rejected":
            require(len(observation["diagnostics"]) == 1, "single first blocker")
            diagnostic = observation["diagnostics"][0]
            require((diagnostic["key"], diagnostic["stage"]) ==
                    (case["expected"]["diagnostic_key"], case["expected"]["expected_stage"]), "frozen diagnosis")
            require(diagnostic["location"] == position(data, diagnostic["span"][0]), "source diagnostic span")
            require(diagnostic["record_id"] and diagnostic["classification"] == "SourceInvalid",
                    "source responsibility")
            require(input_facts(key, data) == case["truth"]["facts"], "independent frozen negative facts")
        else:
            require(not observation["diagnostics"] and all(observation["checks"][k] == "Passed" for k in
                    ("local_rules", "global_rules_47", "planar_regions")), "incomplete checked result")
    return {"status": "Matched", "scope": "PlanarSourceRulesAndRegions", "full_import_contract": "NotChecked",
            "first_blocking_key": observation["diagnostics"][0]["key"] if observation["diagnostics"] else None}
