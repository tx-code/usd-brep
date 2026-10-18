# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Reviewed mathematical/identity anchors transcribed independently from the design.

This module must not import fixture construction. The literal numbers below are
authoritative case definitions, not measurements of generator/kernel output.
"""

from __future__ import annotations

from copy import deepcopy
from fractions import Fraction
from pathlib import Path
import re

from .common import DESIGN_DIRECTORY


def rat(num: str | int, den: str | int = 1) -> dict:
    # Literal anchors are reviewed in this form; normalization is checked by oracle.
    return {"kind": "rational", "num": str(num), "den": str(den)}


PARENTS = {
    "P01": None, "P02": "P01", "P03": "P02", "P04": None, "P05": "P04", "P06": "P01",
    "P07": "P01", "P08": "P01", "P09": "P01", "P10": "P01", "P11": "P04", "P12": "P01", "P13": "P11",
}
NEGATIVE_PARENTS = {
    **{f"N{i:02}": "P01" for i in list(range(1, 9)) + list(range(10, 25)) + [26, 36]},
    "N09": "P04", "N25": "P02", "N27": "P04", "N28": "P04", "N29": "P08", "N30": "P08",
    "N31": "P11", "N32": "P11", "N33": "P11", "N34": "P13", "N35": "P09",
}
# Independent, reviewed pre/post token contract. No generator mutation code is imported.
CHANGE_TOKENS = [
    ("{1 0 10303 403 2 1 2}", "{1 0 10303 403 2 1 3}"),
    ("", "#100=CARTESIAN_POINT('',(3.,-2.,5.));\n"),
    ("SHAPE_DEFINITION_REPRESENTATION(#8,#20)", "SHAPE_DEFINITION_REPRESENTATION(#99999,#20)"),
    ("#3=PRODUCT_CONTEXT('',", "#3=PRODUCT_CONTEXT($,"),
    ("#204=ORIENTED_EDGE('',*,*,", "#204=ORIENTED_EDGE('',$,*,"),
    ("#204=ORIENTED_EDGE('',*,*,#203,.T.)", "#204=ORIENTED_EDGE('',*,*,#203,.U.)"),
    ("#203=EDGE_CURVE('',#110,", "#203=EDGE_CURVE('',#100,"),
    ("(#204,#214,#224,#234)", "(#204,#204,#224,#234)"),
    ("(#507,#517,#527,#537,#547,#557)", "(#507,#517,#527,#537,#547,#557,#507)"),
    ("'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011",
     "'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies_mim_lf',2011"),
    ("#200=DIRECTION('',(0.6,0.8,0.))", "#200=DIRECTION('',(0.,0.,0.))"),
    ("#201=VECTOR('',#200,12.)", "#201=VECTOR('',#200,-12.)"),
    ("#132=DIRECTION('',(0.6,0.8,0.))", "#132=DIRECTION('',(0.,0.,1.))"),
    ("#10=(LENGTH_UNIT() NAMED_UNIT(*)", "#10=(LENGTH_UNIT() NAMED_UNIT($)"),
    ("SI_UNIT($,.RADIAN.)", "SI_UNIT(.QUETTA.,.RADIAN.)"),
    ("#6=PRODUCT_DEFINITION_CONTEXT('part definition'", "#6=PRODUCT_DEFINITION_CONTEXT('part occurrence'"),
    ("#301=FACE_OUTER_BOUND", "#301=FACE_BOUND"), ("#302=ADVANCED_FACE", "#302=FACE_SURFACE"),
    ("#201=VECTOR('',#200,12.)", "#201=VECTOR('',#200,0.)"),
    ("#202=LINE('',#100,#201)", "#202=LINE('',#102,#201)"),
    ("#130=CARTESIAN_POINT('',(3.,-2.,5.))", "#130=CARTESIAN_POINT('',(3.,-2.,5.001))"),
    ("#203=EDGE_CURVE('',#110,#111,#202,.T.)", "#203=EDGE_CURVE('',#110,#111,#202,.F.)"),
    ("#214=ORIENTED_EDGE('',*,*,#213,.T.)", "#214=ORIENTED_EDGE('',*,*,#213,.F.)"),
    ("#233=EDGE_CURVE('',#113,#110,", "#233=EDGE_CURVE('',#113,#111,"),
    ("#311=FACE_BOUND('',#310,.T.)", "#311=FACE_BOUND('',#310,.F.)"),
    ("#22=OPEN_SHELL", "#22=CLOSED_SHELL"),
    ("#507=ADVANCED_FACE('',(#506),#504,.T.)", "#507=ADVANCED_FACE('',(#506),#504,.F.)"),
    ("(#507,#517,#527,#537,#547,#557)", "(#517,#527,#537,#547,#557)"),
    ("'distance_accuracy_B'", "'distance_accuracy_A'"),
    ("#13=UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.0001)",
     "#13=UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.)"),
    ("#17=DIMENSIONAL_EXPONENTS(1.,", "#17=DIMENSIONAL_EXPONENTS(0.,"),
    ("LENGTH_MEASURE(25.4)", "LENGTH_MEASURE(0.)"),
    ("LENGTH_MEASURE(25.4)", "LENGTH_MEASURE(-25.4)"),
    ("#18=LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#10)",
     "#18=LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#19)"),
    ("PRODUCT_RELATED_PRODUCT_CATEGORY('part'", "PRODUCT_RELATED_PRODUCT_CATEGORY('ply'"),
    ("#3=PRODUCT_CONTEXT('',#1,'mechanical')", "#3=PRODUCT_CONTEXT('',#1,'electrical')"),
]
COUNTS = [51, 81, 81, 145, 145, 51, 51, 53, 52, 51, 148, 52, 149]
# Independently transcribed from the first-blocking column of sample-cases.md.
DIAGNOSTICS = [
    ("D2", "UnsupportedSchemaBinding"), ("D0", "DuplicateInstanceId"),
    ("D1", "UnresolvedRequiredReference"), ("D1", "RequiredAttributeMissing"),
    ("D1", "DerivedSlotMarkerRequired"), ("D1", "BooleanValueInvalid"),
    ("D1", "ReferenceTypeMismatch"), ("D1", "AggregateUniqueViolation"), ("D1", "SetDuplicateMember"),
    ("D3", "SourceRuleViolation:application_protocol_definition_required.WR1"),
    ("D3", "SourceRuleViolation:DIRECTION.WR1"), ("D3", "SourceRuleViolation:VECTOR.WR1"),
    ("D3", "SourceRuleViolation:AXIS2_PLACEMENT_3D.WR4"), ("D1", "DerivedSlotMarkerRequired"),
    ("D1", "EnumerationValueInvalid"), ("D2", "UnsupportedDefinitionContextRole"),
    ("D2", "UnsupportedExplicitOuterBoundRequired"), ("D2", "UnsupportedFaceSubtype"),
    ("D4", "NonzeroDefiningLineRequired"), ("D4", "EndpointCurveMismatch"),
    ("D4", "EdgeSurfaceMismatch"), ("D4", "EdgeCurveSenseMismatch"),
    ("D3", "SourceRuleViolation:PATH.WR1"), ("D3", "SourceRuleViolation:EDGE_LOOP.WR1"),
    ("D4", "InnerBoundaryOrientationMismatch"), ("D4", "ClaimedClosedShellHasBoundary"),
    ("D4", "FaceBoundaryOrShellOrientationMismatch"), ("D2", "UnownedOrOrphanProfileContent"),
    ("D3", "SourceRuleViolation:consistent_uncertainty.WR1"),
    ("D3", "SourceRuleViolation:UNCERTAINTY_MEASURE_WITH_UNIT.WR1"),
    ("D3", "SourceRuleViolation:LENGTH_UNIT.WR1+CONVERSION_BASED_UNIT.WR1"),
    ("D4", "UnitScaleNotPositive"), ("D4", "UnitScaleNotPositive"), ("D4", "CyclicUnitDefinition"),
    ("D2", "UnsupportedProductCategory"), ("D2", "UnsupportedProductContextDiscipline"),
]
BOX_FACES = [[0, 3, 2, 1], [4, 5, 6, 7], [0, 1, 5, 4], [3, 7, 6, 2], [0, 4, 7, 3], [1, 2, 6, 5]]
BOX_ENDS = [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6],
            [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]]


def anchors(repo: Path | None = None) -> dict:
    result = {}
    for number in range(1, 14):
        key = f"P{number:02}"
        is_box = key in {"P04", "P05", "P11", "P13"}
        hole = key in {"P02", "P03"}
        scale = "10" if key == "P06" else ("127/5" if key in {"P11", "P13"} else "1")
        source_scale = "10" if key in {"P06", "P07"} else (
            "127/5" if key in {"P11", "P13"} else "1")
        uv = [["0", "0"], ["12", "0"], ["12", "8"], ["0", "8"]]
        points = [["3", "-2", "5"], ["51/5", "38/5", "5"],
                  ["19/5", "62/5", "5"], ["-17/5", "14/5", "5"]]
        if hole:
            points += [["13/5", "4/5", "5"], ["1", "2", "5"],
                       ["17/5", "26/5", "5"], ["5", "4", "5"]]
            uv += [["2", "2"], ["2", "4"], ["6", "4"], ["6", "2"]]
        if is_box:
            points = ([["2", "-3", "5"], ["12", "-3", "5"], ["12", "17", "5"], ["2", "17", "5"],
                       ["2", "-3", "35"], ["12", "-3", "35"], ["12", "17", "35"], ["2", "17", "35"]]
                      if key in {"P04", "P05"} else
                      [["0", "0", "0"], ["127/5", "0", "0"], ["127/5", "254/5", "0"],
                       ["0", "254/5", "0"], ["0", "0", "381/5"], ["127/5", "0", "381/5"],
                       ["127/5", "254/5", "381/5"], ["0", "254/5", "381/5"]])
        measures = ({"area": rat(2200), "perimeter": rat(240), "volume": rat(6000)} if is_box else
                    {"area": rat(88 if hole else 96), "perimeter": rat(52 if hole else 40)})
        if key in {"P11", "P13"}:
            measures = {"area": rat(354838, 25), "perimeter": rat(3048, 5), "volume": rat(12290298, 125)}
        if key == "P06":
            measures = {"area": rat(9600), "perimeter": rat(400)}
            points = [[str(Fraction(v) * 10) for v in point] for point in points]
        if key == "P05":
            measures.pop("volume")
        result[f"S203-{key}"] = {
            "kind": "Positive", "parent": f"S203-{PARENTS[key]}" if PARENTS[key] else None,
            "record_count": COUNTS[number - 1], "source_length_scale_mm": source_scale,
            "role": "MaterialSolid" if is_box and key != "P05" else "SurfaceModel",
            "closed": is_box, "world_points": points, "uv_points": uv if not is_box else None,
            "loops": [list(range(4)), list(range(4, 8))] if hole else [list(range(4))],
            "faces": BOX_FACES if is_box else [list(range(4))],
            "edge_ends": BOX_ENDS if is_box else ([[0, 1], [1, 2], [2, 3], [3, 0]] +
                                                ([[4, 5], [5, 6], [6, 7], [7, 4]] if hole else [])),
            "plane_origin": ["17/5", "26/5", "5"] if key == "P03" else (
                ["30", "-20", "50"] if key == "P06" else ["3", "-2", "5"]),
            "measures": measures, "scale": scale,
        }
        if repo is not None:
            header, common = author_literals(repo, key)
            result[f"S203-{key}"].update(header_prefix=header, literal_records=common)
    for key, parent in NEGATIVE_PARENTS.items():
        base = result[f"S203-{parent}"]
        result[f"S203-{key}"] = {
            "kind": "Negative", "parent": f"S203-{parent}",
            "record_count": base["record_count"] + (1 if key == "N02" else 0),
            "facts": negative_facts(key),
            "mutation_tokens": list(CHANGE_TOKENS[int(key[1:]) - 1]),
            "expected_stage": DIAGNOSTICS[int(key[1:]) - 1][0],
            "diagnostic_key": DIAGNOSTICS[int(key[1:]) - 1][1],
        }
    return deepcopy(result)


def author_literals(repo: Path, key: str) -> tuple[str, dict]:
    """Read author text, never the generator's constructed output or constants."""
    text = (repo / DESIGN_DIRECTORY / "ap203e2-sample-design.md").read_text(encoding="utf-8")
    fence = chr(96) * 3
    blocks = re.findall(fence + r"text\n(.*?)" + fence, text, re.S)
    header = next(block for block in blocks if block.startswith("ISO-10303-21;"))
    common_text = next(block for block in blocks if block.startswith("#1=APPLICATION_CONTEXT"))
    common = {str(i): body for i, body in re.findall(r"(?m)^#(\d+)=(.*);$", common_text)}
    if key in {"P06", "P07"}:
        common["10"] = common["10"].replace(".MILLI.", ".CENTI.")
    if key == "P08":
        common["12"] = common["12"].replace(
            " GLOBAL_UNIT_ASSIGNED", " GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#13,#14)) GLOBAL_UNIT_ASSIGNED")
        common.update({"13": "UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.0001),#10,'distance_accuracy_A',$)",
                       "14": "UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.0002),#10,'distance_accuracy_B',$)"})
    if key == "P09":
        common["15"] = "PRODUCT_RELATED_PRODUCT_CATEGORY('part',$,(#4))"
    if key == "P10":
        common["3"] = "MECHANICAL_CONTEXT('',#1,'mechanical')"
        common["6"] = "DESIGN_CONTEXT('part definition',#1,'design')"
        common["5"] = "PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE('A',$,#4,.NOT_KNOWN.)"
    if key in {"P11", "P13"}:
        common["12"] = common["12"].replace("((#10,#11))", "((#19,#11))")
        common.update({"17": "DIMENSIONAL_EXPONENTS(1.,0.,0.,0.,0.,0.,0.)",
                       "18": "LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#10)",
                       "19": "(CONVERSION_BASED_UNIT('inch',#18) LENGTH_UNIT() NAMED_UNIT(#17))"})
    if key == "P13":
        common["12"] = common["12"].replace(
            " GLOBAL_UNIT_ASSIGNED", " GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#13)) GLOBAL_UNIT_ASSIGNED")
        common["13"] = "UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.0001),#10,'distance_accuracy_A',$)"
    if key == "P12":
        common["12"] = common["12"].replace("((#10,#11))", "((#10,#11,#16))")
        common["16"] = "(NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT())"
    solid = key in {"P04", "P11", "P13"}
    boxed = key in {"P04", "P05", "P11", "P13"}
    common.update({"20": ("ADVANCED_BREP_SHAPE_REPRESENTATION" if solid else
                           "MANIFOLD_SURFACE_SHAPE_REPRESENTATION") + "('body',(#21),#12)",
                   "21": "MANIFOLD_SOLID_BREP('',#22)" if solid else "SHELL_BASED_SURFACE_MODEL('',(#22))",
                   "22": "CLOSED_SHELL('',(#507,#517,#527,#537,#547,#557))" if boxed else
                         "OPEN_SHELL('',(#302))"})
    return header, common


def negative_facts(key: str) -> dict:
    # Directly reviewed independent facts. Product diagnostics remain EXPECTATIONS.
    facts = {
        "N01": {"oid_tail": 3}, "N02": {"occurrences": 52, "unique_ids": 51, "duplicate_id": 100},
        "N03": {"dangling": [99999]}, "N04": {"required_name_token": "$"},
        "N05": {"derived_start_token": "$"}, "N06": {"boolean_token": ".U."},
        "N07": {"edge_start_kind": "CARTESIAN_POINT"}, "N08": {"orphans": list(range(210, 215))},
        "N09": {"duplicate_face": 507}, "N10": {"apd_suffix": "_mim_lf"},
        "N11": {"direction_norm_squared": rat(0)}, "N12": {"magnitude": rat(-12)},
        "N13": {"axis_cross_norm_squared": rat(0)}, "N14": {"si_dimensions_token": "$"},
        "N15": {"prefix": ".QUETTA."}, "N16": {"context_role": "'part occurrence'"},
        "N17": {"explicit_outer_bounds": 0}, "N18": {"face_kind": "FACE_SURFACE"},
        "N19": {"magnitude": rat(0)}, "N20": {"endpoint_line_distance_squared": rat(64)},
        "N21": {"edge_plane_distance": rat(1, 1000)}, "N22": {"same_sense": ".F."},
        "N23": {"path_connected": False, "loop_closed": True},
        "N24": {"path_connected": True, "loop_closed": False, "end_distance_squared": rat(144)},
        "N25": {"effective_hole_area": rat(8)}, "N26": {"free_edges": 4, "free_length": rat(40)},
        "N27": {"selected_bottom_normal": [0, 0, 1]},
        "N28": {"orphans": list(range(501, 508)) + list(range(400, 404)),
                "remaining_area": rat(2000), "free_length": rat(60)},
        "N29": {"names_unique": False}, "N30": {"uncertainty": rat(0)},
        "N31": {"declared_length_exponent": rat(0)}, "N32": {"factor": rat(0)},
        "N33": {"factor": rat(-127, 5)}, "N34": {"unit_cycle": [19, 18, 19], "mm_anchor_present": True},
        "N35": {"category": "'ply'"}, "N36": {"discipline": "'electrical'"},
    }
    return facts[key]
