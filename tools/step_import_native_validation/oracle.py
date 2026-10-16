# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Independent frozen-01 truth comparison; no CAD reader and no native-output-derived expectations."""
from collections import Counter
from fractions import Fraction as F
import hashlib
import math

from tools.step_import_validation.corpus import quality_contract
from tools.step_import_validation.oracle import decode
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
    require(Counter(x["stage"] for x in rows) == Counter({k: 10 for k in (
        "Constructed", "UvInstalled", "PointersChecked", "DeepChecked", "Postchecked")}), "tolerance stage coverage")
    for stage in {x["stage"] for x in rows}:
        keys = [(x["kind"], x["source_id"]) for x in rows if x["stage"] == stage]
        require(Counter(x[0] for x in keys) == Counter(Brep=1, Face=1, Vertex=4, Edge=4) and
                len(set(keys)) == 10, "tolerance object coverage")
    require(all(x["value_mm"] == float(decode(q["initial_working_tolerance_mm"])) for x in rows),
            "clean tolerance growth")
    require(len(observed["edge_mappings"]) == 4 and len({x["source_edge"] for x in observed["edge_mappings"]}) == 4,
            "source edge identity")
    for edge in observed["edge_mappings"]:
        require(edge["native_interval"] == [0, 1] and edge["source_parameter"] ==
                "coefficient/sqrt(direction_norm_squared)", "explicit parameter mapping")
    require(observed["kernel_path"] and observed["kernel_path"] != "Unmeasured", "loaded kernel observation")


def corpus_case(case, observed, exit_code, data):
    key = case["case_id"]
    if key == "S203-P01":
        require(exit_code == 0, "F1 native success exit")
        accepted(observed, data, case["truth"])
        return
    status = ("Deferred" if key in DEFERRED else "Unsupported" if key in UNSUPPORTED or "-P" in key
              else "Rejected")
    require(observed["status"] == status and exit_code == {"Deferred": 7, "Unsupported": 2, "Rejected": 1}[status],
            "frozen source/target scope outcome " + key)
    require(observed["model"] is None and observed["native_measurements"] is None and
            observed["native_calls"] == 0 and not observed["candidate_discarded"], "blocked before native construction")
    require(observed["validations"] == {"pointer_reports": None, "level2_reports": None} and
            not observed["tolerances"] and not observed["edge_mappings"], "unexecuted native checks")
    require(observed["source"]["sha256"] == hashlib.sha256(data).hexdigest(), "rejected input identity")
    if "-N" in key and key not in DEFERRED:
        diagnostic = observed["diagnostics"][0]
        require((diagnostic["key"], diagnostic["stage"]) == (
            case["expected"]["diagnostic_key"], case["expected"]["expected_stage"]), "original diagnostic")
        require(diagnostic["location"] == position(data, diagnostic["span"][0]), "diagnostic position")
