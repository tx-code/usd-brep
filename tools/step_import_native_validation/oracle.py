# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Independent frozen-01 truth comparison; no CAD reader and no native-output-derived expectations."""
from collections import Counter
from fractions import Fraction as F
import hashlib
import math

from tools.step_import_validation.corpus import quality_contract
from tools.step_import_validation.oracle import decode
from tools.step_import_validation.records import inventory, by_id, ref, aggregate, vector as source_vector
from tools.step_inspect_validation.oracle import position
from tools.step_semantic_validation.oracle import DEFERRED, UNSUPPORTED


def require(value, message):
    if not value:
        raise AssertionError(message)


def vector(actual, expected, allowance, label):
    require(len(actual) == len(expected), label + " dimension")
    require(all(math.isfinite(v) for v in actual), label + " finite")
    squared = sum((F(a) - e) ** 2 for a, e in zip(actual, expected))
    require(squared <= allowance ** 2, label + " error")


def accepted(observed, data, truth, normal=(F(0), F(0), F(1)), uv_bounds=(0, 0, 12, 8)):
    q = quality_contract()
    require(observed["status"] == "Accepted" and observed["model"] == {
        "kind": "OwningReadOnlySnapshot", "role": "OpenSurfaceModel"}, "complete publication")
    require(observed["format_version"] == "step-native-import.v1" and
            observed["scope"] == "SingleOpenPlanarFace", "qualified scope")
    require(observed["profile"] == "203e2-P0-Planar-0.1" and observed["schema_sha256"] ==
            "c68dd02200e2f4213e311553a4f6295effd168209de1fdc4e8003ce4c9ddd088", "schema identity")
    require(observed["source"]["sha256"] == hashlib.sha256(data).hexdigest(), "captured input identity")
    require(observed["source"]["captured"] and not observed["candidate_discarded"], "accepted capture/candidate")
    require(observed["source"]["read"] == "Inspected" and observed["source"]["semantics"] == "Checked" and
            observed["source"]["planar"] == "Checked" and not observed["diagnostics"], "source qualification")
    require(observed["repair"] == "None", "repair contract")
    fields = {"definition_mm": "definition_error_mm", "consistency_mm": "consistency_error_mm",
              "derived_uv_mm": "derived_uv_error_mm", "initial_work_tolerance_mm": "initial_working_tolerance_mm",
              "maximum_work_tolerance_mm": "max_working_tolerance_mm", "tolerance_growth_mm": "clean_growth_mm",
              "query_mm": "aux_length_error_mm", "area_mm2": "aux_area_error_mm2"}
    require(observed["quality"]["id"] == q["id"] and all(
        observed["quality"][a] == q[b] for a, b in fields.items()), "full explicit Q")
    n = observed["native_measurements"]
    require({k: n[k] for k in ("faces", "edges", "vertices")} == truth["source_topology"], "native topology")
    for field, true_field, error in [("area_mm2", "area", "aux_area_error_mm2"),
                                    ("boundary_mm", "perimeter", "aux_length_error_mm")]:
        require(math.isfinite(n[field]) and abs(F(n[field]) - decode(truth["measures"][true_field])) <=
                decode(q[error]), "independent " + field)
    tolerance = decode(q["aux_length_error_mm"])
    vector(n["centroid_mm"], list(map(decode, truth["centroid"])), tolerance, "centroid")
    for actual, expected in zip(n["bounds_mm"], truth["bounds"]):
        vector(actual, list(map(decode, expected)), tolerance, "bounds")
    vector(n["normal"], normal, tolerance, "selected face normal")
    require(n["material_volume"] == "NotApplicable", "open surface material-volume qualification")
    window = n["window_mm"]
    require(len(window) == 4 and all(math.isfinite(x) for x in window), "finite window")
    require(window[0] <= uv_bounds[0] and window[1] <= uv_bounds[1] and
            window[2] >= uv_bounds[2] and window[3] >= uv_bounds[3], "complete trim domain")
    require(all(abs(a - b) <= 1e-9 for a, b in zip(window, uv_bounds)), "window protection, no arbitrary domain")
    require(observed["normalization_policy"] == {
        "curve_parameter": "SourceIncreasingToNative01", "uv": "DerivedAfterTopologyOnSameOwner",
        "loop_order": "OuterFirstIdentityPreserving",
        "finite_window": "SourceTrimEnvelope",
        "window_protection": "OutwardIntervalPlus64EpsilonTimes1PlusMaxAbsPerAxis",
        "definition_fitting": False}, "normalization provenance")
    require(observed["validations"] == {"pointer_reports": 0, "level2_reports": 0}, "actual validators")
    for field, error in [("definition_bound_mm", "definition_error_mm"),
                         ("consistency_bound_mm", "consistency_error_mm"),
                         ("uv_bound_mm", "derived_uv_error_mm")]:
        require(isinstance(observed[field], (int, float)) and math.isfinite(observed[field]) and
                0 <= F(observed[field]) <= decode(q[error]), "whole-domain " + field)
    rows = observed["tolerances"]
    object_count = 2 + n["vertices"] + n["edges"]
    require(Counter(x["stage"] for x in rows) == Counter({k: object_count for k in (
        "Constructed", "UvInstalled", "PointersChecked", "DeepChecked", "Postchecked")}), "tolerance stage coverage")
    for stage in {x["stage"] for x in rows}:
        keys = [(x["kind"], x["source_id"]) for x in rows if x["stage"] == stage]
        require(Counter(x[0] for x in keys) == Counter(Brep=1, Face=1, Vertex=n["vertices"], Edge=n["edges"]) and
                len(set(keys)) == object_count, "tolerance object coverage")
    require(all(x["value_mm"] == float(decode(q["initial_working_tolerance_mm"])) for x in rows),
            "clean tolerance growth")
    require(len(observed["edge_mappings"]) == n["edges"] and
            len({x["source_edge"] for x in observed["edge_mappings"]}) == n["edges"],
            "source edge identity")
    for edge in observed["edge_mappings"]:
        require(edge["native_interval"] == [0, 1] and edge["source_parameter"] ==
                "coefficient/sqrt(direction_norm_squared)", "explicit parameter mapping")
    require(observed["kernel_path"] and observed["kernel_path"] != "Unmeasured", "loaded kernel observation")
    loop_provenance(observed, data)


def loop_provenance(observed, data):
    """STEP's explicit OE/bound semantics and a separately computed source-frame projection."""
    index = by_id(inventory(data))
    face = next(r for r in index.values() if r.kind == "ADVANCED_FACE")
    bounds = [ref(x) for x in aggregate(face.args[1])]
    placement = index[ref(index[ref(face.args[2])].args[1])]
    origin = source_vector(index[ref(placement.args[1])].args[1])

    def unit(values):
        length = math.sqrt(sum(v * v for v in values))
        return tuple(float(v) / length for v in values)

    z = unit(source_vector(index[ref(placement.args[2])].args[1]) if placement.args[2] != "$" else (0, 0, 1))
    direction = source_vector(index[ref(placement.args[3])].args[1]) if placement.args[3] != "$" else (1, 0, 0)
    along_z = sum(a * b for a, b in zip(direction, z))
    x = unit(tuple(a - along_z * b for a, b in zip(direction, z)))
    y = (z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0])

    def uv(vertex):
        point = source_vector(index[ref(index[vertex].args[1])].args[1])
        delta = [float(a - b) for a, b in zip(point, origin)]
        return tuple(F(sum(a * b for a, b in zip(delta, axis))) for axis in (x, y))

    rows = observed["loop_mappings"]
    require(len(rows) == len(bounds) and rows[0]["outer"] and sum(r["outer"] for r in rows) == 1,
            "native outer-first loop coverage")
    require({r["source_bound"] for r in rows} == {str(b) for b in bounds}, "bound identity coverage")
    edge_maps = {e["source_edge"]: e for e in observed["edge_mappings"]}
    all_uses = []
    for row in rows:
        bound = index[int(row["source_bound"])]
        loop = index[ref(bound.args[1])]
        require(row["source_face"] == str(face.entity_id) and row["source_loop"] == str(loop.entity_id) and
                row["outer"] == (bound.kind == "FACE_OUTER_BOUND") and
                row["source_orientation"] == (bound.args[2] == ".T."), "source loop binding")
        wanted = []
        for token in aggregate(loop.args[1]):
            oe = index[ref(token)]
            edge = index[ref(oe.args[3])]
            start, end = ref(edge.args[1]), ref(edge.args[2])
            if oe.args[4] == ".F.":
                start, end = end, start
            wanted.append((str(oe.entity_id), str(edge.entity_id), str(start), str(end)))
        if bound.args[2] == ".F.":
            wanted = [(oe, edge, end, start) for oe, edge, start, end in reversed(wanted)]
        actual = [(u["source_oriented_edge"], u["source_edge"], u["start_vertex"], u["end_vertex"])
                  for u in row["uses"]]
        require(len(actual) == len(wanted) and any(actual == wanted[k:] + wanted[:k] for k in range(len(wanted))),
                "directed occurrence identity and cyclic order")
        for use in row["uses"]:
            forward = use["start_vertex"] == edge_maps[use["source_edge"]]["native_min_vertex"]
            require(use["uv_owner_edgeuse"] == "SAME" and use["forward_in_native_curve"] == forward and
                    use["uv_owner_on_selected_side"] == forward, "actual occurrence UV owner and direction")
            vector(use["uv_start_mm"], uv(int(use["start_vertex"])), F(1, 10**7), "source UV start")
            vector(use["uv_end_mm"], uv(int(use["end_vertex"])), F(1, 10**7), "source UV end")
            all_uses.append(use["source_oriented_edge"])
    require(len(all_uses) == len(set(all_uses)) == observed["native_measurements"]["edges"], "all source occurrences")


def corpus_case(case, observed, exit_code, data):
    key = case["case_id"]
    if key in {"S203-P01", "S203-P02", "S203-P03"}:
        require(exit_code == 0, "qualified native success exit")
        window = case["truth"]["source_definition"]["source_uv_window"]
        accepted(observed, data, case["truth"], uv_bounds=tuple(decode(v) for corner in window for v in corner))
        return
    status = ("Deferred" if key in DEFERRED else "Unsupported" if key in UNSUPPORTED or "-P" in key
              else "Rejected")
    require(observed["status"] == status and exit_code == {"Deferred": 7, "Unsupported": 2, "Rejected": 1}[status],
            "frozen source/target scope outcome " + key)
    require(observed["model"] is None and observed["native_measurements"] is None and
            observed["native_calls"] == 0 and not observed["candidate_discarded"], "blocked before native construction")
    require(observed["validations"] == {"pointer_reports": None, "level2_reports": None} and
            not observed["tolerances"] and not observed["edge_mappings"] and not observed["loop_mappings"],
            "unexecuted native checks")
    require(observed["source"]["sha256"] == hashlib.sha256(data).hexdigest(), "rejected input identity")
    if "-N" in key and key not in DEFERRED:
        diagnostic = observed["diagnostics"][0]
        require((diagnostic["key"], diagnostic["stage"]) == (
            case["expected"]["diagnostic_key"], case["expected"]["expected_stage"]), "original diagnostic")
        require(diagnostic["location"] == position(data, diagnostic["span"][0]), "diagnostic position")
