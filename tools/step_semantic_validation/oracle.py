# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Independent checks against pinned declarations and ticket-01 input/diagnostic contracts."""

from __future__ import annotations

from collections import defaultdict
from fractions import Fraction
from pathlib import Path
import re

from tools.step_import_validation.common import InvalidEvidence, digest, load_json
from tools.step_import_validation.oracle import exact
from tools.step_inspect_validation.oracle import compare as compare_raw, position

REFERENCE = Path(__file__).parent / "contracts/n2635-reference-v2.json"
REFERENCE_SHA = "95eaed220c9e638f8f94c6e9429c2b562668ad643070efa77f26b8b305b29a58"
REJECTIONS = {f"S203-N{i:02}" for i in [1, 2, 3, 4, 5, 6, 7, 8, 9, 14, 15, 16, 17, 18, 28, 35, 36]}
UNSUPPORTED = {f"S203-N{i:02}" for i in [1, 16, 17, 18, 28, 35, 36]}
DEFERRED = {f"S203-{key}" for key in ["P11", "P13", "N31", "N32", "N33", "N34"]}
FIELD_FAILURES = {
    "S203-N04": ("3", "name", "APPLICATION_CONTEXT_ELEMENT", [1]),
    "S203-N05": ("204", "edge_start", "EDGE", [2]),
    "S203-N06": ("204", "orientation", "ORIENTED_EDGE", [5]),
    "S203-N07": ("203", "edge_start", "EDGE", [2]),
    "S203-N08": ("300", "edge_list", "PATH", [2, 2]),
    "S203-N09": ("22", "cfs_faces", "CONNECTED_FACE_SET", [2, 7]),
    "S203-N14": ("10", "dimensions", "NAMED_UNIT", [1]),
    "S203-N15": ("11", "prefix", "SI_UNIT", [1]),
    "S203-N16": ("6", "name", "APPLICATION_CONTEXT_ELEMENT", [1]),
    "S203-N17": ("302", "bounds", "FACE", [2]),
    "S203-N35": ("15", "name", "PRODUCT_CATEGORY", [1]),
    "S203-N36": ("3", "discipline_type", "PRODUCT_CONTEXT", [3]),
}
PREFIX = {None: 0, "EXA": 18, "PETA": 15, "TERA": 12, "GIGA": 9, "MEGA": 6, "KILO": 3,
          "HECTO": 2, "DECA": 1, "DECI": -1, "CENTI": -2, "MILLI": -3, "MICRO": -6,
          "NANO": -9, "PICO": -12, "FEMTO": -15, "ATTO": -18}
LATER_CHECKS = ("global_rules_47", "other_entity_where", "named_measure_predicates", "geometry_consistency",
                "conversion_units", "uncertainty_evaluation", "native_build", "full_import")


def require(condition, detail):
    if not condition:
        raise InvalidEvidence("Ticket03.ObservationConflict", detail)


def reference():
    require(digest(REFERENCE.read_bytes()) == REFERENCE_SHA, "pinned independent declaration witness")
    return load_json(REFERENCE)


def ancestors(name, entities):
    result = {name}
    for parent in entities[name]["parents"]:
        result |= ancestors(parent, entities)
    return result


def fields(name, entities):
    result, seen = [], set()

    def append(type_name):
        for parent in entities[type_name]["parents"]:
            append(parent)
        for field in entities[type_name]["explicit_fields"]:
            identity = (type_name, field["name"])
            if identity not in seen:
                seen.add(identity)
                result.append((type_name, field))
    append(name)
    return result


def compare(case: dict, observation: dict, exit_code: int, data: bytes, inventory: dict, witness=None):
    witness = witness or reference()
    raw = observation["source"]
    raw_exit = {"Inspected": 0, "Rejected": 1, "Unsupported": 2, "ResourceLimit": 3, "IoFailure": 4}[raw["status"]]
    compare_raw(case, raw, raw_exit, data, inventory)
    require(observation["format_version"] == "step-semantics.v1" and observation["scope"] == "SinglePartTypesSI",
            "reader scope")
    require(all(observation["checks"][key] == "NotRun" for key in LATER_CHECKS), "later-stage overclaim")
    key = case["case_id"]
    if key in REJECTIONS:
        status = ("SourceFailure" if key in {"S203-N02", "S203-N03"} else
                  "Unsupported" if key in UNSUPPORTED else "Rejected")
        require(observation["status"] == status and observation["snapshot"] is None,
                "source rejection/no partial semantics")
        require(exit_code == (2 if key in UNSUPPORTED else 1), "rejection exit code")
        require(len(observation["diagnostics"]) == 1, "one first-blocking diagnostic")
        diagnostic = observation["diagnostics"][0]
        require(diagnostic["key"] == case["expected"]["diagnostic_key"], "frozen diagnostic key")
        require(diagnostic["stage"] == case["expected"]["expected_stage"], "frozen logical responsibility")
        require(diagnostic["location"] == position(data, diagnostic["span"][0]), "diagnostic source position")
        if key in FIELD_FAILURES:
            record, field, origin, path = FIELD_FAILURES[key]
            require((diagnostic["record_id"], diagnostic["field"], diagnostic["declared_in"],
                     diagnostic["parameter_path"]) ==
                    (record, field, origin, path), "declaring ancestor/field/aggregate slot")
        if key not in {"S203-N17", "S203-N28"}:
            start, end = diagnostic["span"]
            changed_start, changed_end = case["expected"]["mutation_source_span"]
            require(start < changed_end and changed_start < end, "diagnostic does not point to the mutation evidence")
        if key == "S203-N07":
            require(diagnostic["expected"] == "VERTEX" and diagnostic["actual"] == "CARTESIAN_POINT" and
                    diagnostic["target_id"] == "100", "type mismatch counterexample")
        if key in {"S203-N08", "S203-N09"}:
            require(len(diagnostic["related"]) == 1, "first aggregate occurrence missing")
            a, b = diagnostic["span"], diagnostic["related"][0]["span"]
            require(data[a[0]:a[1]] == data[b[0]:b[1]], "duplicate identity evidence")
        if key == "S203-N17":
            require(diagnostic["actual"] == "0" and "FACE_BOUND" in data.decode("ascii"), "explicit outer-bound guard")
        if key == "S203-N28":
            require(set(re.findall(r"#(\d+)", diagnostic["actual"])) ==
                    {str(n) for n in list(range(400, 404)) + list(range(501, 508))}, "all eleven orphan identities")
            require(diagnostic["classification"] == "ProfileUnsupported", "orphan guard was called a schema WHERE")
        return {"scope": "SinglePartTypesSI", "status": "Matched", "expected_status": status,
                "first_blocking_key": diagnostic["key"], "full_import_contract": "NotChecked"}

    deferred = key in DEFERRED
    require(observation["deferred_reason"] == ("ConversionLengthInterpretationPendingTicket09" if deferred else None),
            "deferred mandatory interpretation was hidden")
    require(observation["status"] == ("Deferred" if deferred else "Checked") and exit_code == (7 if deferred else 0),
            "stage-specific success/deferred status")
    require(observation["diagnostics"] == [], "unexpected semantic diagnostic")
    checks = observation["checks"]
    require(all(checks[p] == "Passed" for p in ("schema_binding", "type_fields", "single_part_profile")),
            "required phases")
    require(checks["si_units"] == ("Deferred" if deferred else "Passed"), "unit phase")
    snapshot = observation["snapshot"]
    require(snapshot and snapshot["type_index_complete"] is True, "missing complete type index")
    require(snapshot["schema"]["express_sha256"] == witness["schema_sha256"], "schema byte identity")
    require(snapshot["schema"]["oid"] == [1, 0, 10303, 403, 2, 1, 2], "precise edition OID")
    require(snapshot["schema"]["profile"] == case["identity"]["profile_id"], "profile binding")
    entities = witness["entities"]
    expected_populations = {name: [] for name in entities}
    records = raw["records"]
    require(len(snapshot["records"]) == len(records), "all records in type index")
    type_sets = []
    for index, (view, record) in enumerate(zip(snapshot["records"], records)):
        types = set().union(*(ancestors(component["name"], entities) for component in record["components"]))
        type_sets.append(types)
        require(view["source_record_index"] == index and view["id"] == record["id"],
                "type view uses wrong source index")
        require(set(view["types"]) == types and len(view["types"]) == len(types),
                "missing/duplicate ancestor membership")
        for name in types:
            expected_populations[name].append(record["id"])
        expected_attributes = []
        overrides = {(item["origin"], item["name"]): name for name in types
                     for item in entities[name]["derived_redeclarations"]}
        for c, component in enumerate(record["components"]):
            layout = ([(component["name"], f) for f in entities[component["name"]]["explicit_fields"]]
                      if record["complex"] else fields(component["name"], entities))
            require(len(layout) == len(component["parameters"]), "independent schema slot count")
            for p, (origin, field) in enumerate(layout):
                declaration = field["declaration"]
                agg = re.match(r"(SET|LIST)\s*\[(\d+):(\d+|\?)\]", declaration)
                expected_attributes.append({"component_index": c, "parameter_index": p, "name": field["name"],
                    "declared_in": origin, "declared_type": declaration.split()[-1],
                    "redeclared_by": overrides.get((origin, field["name"]), ""),
                    "optional": declaration.startswith("OPTIONAL "),
                    "derived_slot": (origin, field["name"]) in overrides,
                    "aggregate": agg[1] if agg else "", "minimum": int(agg[2]) if agg else 0,
                    "maximum": int(agg[3]) if agg and agg[3] != "?" else None,
                    "unique_elements": bool(agg and (agg[1] == "SET" or " OF UNIQUE " in declaration)),
                    "span": component["parameters"][p]["span"]})
        require(view["attributes"] == expected_attributes,
                f"schema declaration/field-origin mismatch in #{record['id']}")
    require(snapshot["type_populations"] == expected_populations, "complete captured population/ancestor coverage")
    part = snapshot["part"]
    expected_roles = {"application_context": "1", "application_protocol": "2", "product_context": "3",
                      "definition_context": "6", "product": "4", "formation": "5", "definition": "7",
                      "product_shape": "8", "shape_definition_representation": "9", "representation": "20",
                      "unit_context": "12", "shape": "21", "shell": "22"}
    if key == "S203-P09":
        expected_roles["category"] = "15"
    require(part["roles"] == expected_roles, "source product/shape/context identity chain")
    require(part["inverse_roles"] == {"ac_context_elements": ["3", "6"], "context_representations": ["20"]},
            "inverse role filtering included APD or dropped inherited context elements")
    by_id = {record["id"]: record for record in records}
    require(part["apd_schema_claim"] == by_id["2"]["components"][0]["parameters"][1]["text"] and
            part["apd_year_lexeme"] == "2011", "APD was inferred/overwritten by HEADER")
    require(part["shape_geometry_verified"] is False, "source shape role became a geometry certificate")
    shell = by_id["22"]["components"][0]["name"]
    shape = by_id["21"]["components"][0]["name"]
    expected_role = "MaterialSolid" if shape == "MANIFOLD_SOLID_BREP" else (
        "ClosedSurfaceModel" if shell == "CLOSED_SHELL" else "OpenSurfaceModel")
    require(part["source_shape_role"] == expected_role, "solid/surface source role")

    pending = [i for i, r in enumerate(records) if r["id"] == "21"]
    outgoing = defaultdict(list)
    for ref in raw["references"]:
        outgoing[ref["record_index"]].append(ref["target_record_index"])
    used = set()
    while pending:
        r = pending.pop()
        if r not in used:
            used.add(r)
            pending += [t for t in outgoing[r] if "REPRESENTATION_ITEM" in type_sets[t]]
    require([row["used_by_representation"] for row in snapshot["records"]] ==
            [r in used and "REPRESENTATION_ITEM" in type_sets[r] for r in range(len(records))],
            "RI-filtered use closure followed management/unit edges or skipped indirect geometry")
    units = snapshot["units"]
    require(units["length_unit"] == ("19" if deferred else "10") and units["plane_angle_unit"] == "11",
            "context unit identities")
    require(units["solid_angle_unit"] == ("16" if key == "S203-P12" else None), "rad/sr nominal distinction")
    require(units["length_scale_mm"] == (None if deferred else exact(10 if key in {"S203-P06", "S203-P07"} else 1)),
            "canonical SI scale or deferred conversion")
    expected_si = []
    for r, record in enumerate(records):
        if "SI_UNIT" not in type_sets[r]:
            continue
        components = {c["name"]: c for c in record["components"]}
        prefix, name = components["SI_UNIT"]["parameters"]
        prefix_value = None if prefix["kind"] == "Unset" else prefix["text"]
        quantity, canonical, power = (("Length", "mm", 3) if "LENGTH_UNIT" in type_sets[r] else
                                      ("PlaneAngle", "rad", 0) if "PLANE_ANGLE_UNIT" in type_sets[r] else
                                      ("SolidAngle", "sr", 0))
        expected_si.append({"source_id": record["id"], "quantity": quantity, "name": name["text"],
                            "prefix": prefix_value, "canonical_unit": canonical,
                            "scale": exact(Fraction(10) ** (PREFIX[prefix_value] + power)),
                            "dimensions": [int(quantity == "Length"), 0, 0, 0, 0, 0, 0]})
    require(units["si_definitions"] == expected_si, "exact SI prefix and dimensions interpretation")
    require(snapshot["parameter_contract"] == {"coordinates": "Length", "direction_ratios": "Dimensionless",
            "vector_magnitude": "Length", "line_parameter": "Dimensionless", "plane_parameters": "Length",
            "target_mapping": "NotRun", "geometry_conversion": "NotRun"}, "length/ratio/t/UV semantics conflated")
    return {"scope": "SinglePartTypesSI", "status": "Matched", "expected_status": observation["status"],
            "checks": ["schema identity", "independent ancestor/field declarations", "all populations",
                       "source roles/inverses", "RI use filtering", "SI scale and parameter semantics"],
            "full_import_contract": "NotChecked"}
