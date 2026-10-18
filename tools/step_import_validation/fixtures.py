# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Literal fixture construction. Geometry expectations live in reference.py, not here."""

from __future__ import annotations

import re
from pathlib import Path

from .common import DESIGN_DIRECTORY, InvalidEvidence

PROFILE = "203e2-P0-Planar-0.1"
Q_ID = "Q-P0-mm-01"
SCHEMA_NAME = "AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF"
HEADER = (
    "ISO-10303-21;\nHEADER;\n"
    "FILE_DESCRIPTION(('AP203e2 planar profile design fixture'),'2;1');\n"
    "FILE_NAME('p0-analytic-design.step','2026-10-15T00:00:00',('P0'),('local'),"
    "'P0 design only','synthetic analytic oracle','');\n"
    f"FILE_SCHEMA(('{SCHEMA_NAME} {{1 0 10303 403 2 1 2}}'));\nENDSEC;\nDATA;\n"
)
END = "ENDSEC;\nEND-ISO-10303-21;\n"
COMMON = (
    "#1=APPLICATION_CONTEXT('configuration controlled 3d design of mechanical parts and assemblies');\n"
    "#2=APPLICATION_PROTOCOL_DEFINITION('international standard','"
    "ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011,#1);\n"
    "#3=PRODUCT_CONTEXT('',#1,'mechanical');\n"
    "#4=PRODUCT('P0-PART','P0 analytic fixture',$,(#3));\n"
    "#5=PRODUCT_DEFINITION_FORMATION('A',$,#4);\n"
    "#6=PRODUCT_DEFINITION_CONTEXT('part definition',#1,'design');\n"
    "#7=PRODUCT_DEFINITION('D1',$,#5,#6);\n"
    "#8=PRODUCT_DEFINITION_SHAPE('',$,#7);\n"
    "#9=SHAPE_DEFINITION_REPRESENTATION(#8,#20);\n"
    "#10=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.));\n"
    "#11=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));\n"
    "#12=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11)) "
    "REPRESENTATION_CONTEXT('local','3D'));\n"
)


def _document_records(path: Path, marker: str) -> list[str]:
    blocks = re.findall(r"```text\r?\n(.*?)```", path.read_text(encoding="utf-8"), re.S)
    selected = [block for block in blocks if marker in block and len(re.findall(r"^#\d+=", block, re.M)) >= 31]
    if len(selected) != 1:
        raise InvalidEvidence("Build.AmbiguousDesignBlock", str(path))
    return [line.strip() for line in selected[0].splitlines() if re.match(r"^#\d+=", line)]


def _replace(data: bytes, old: str, new: str) -> tuple[bytes, dict]:
    old_bytes, new_bytes = old.encode("ascii"), new.encode("ascii")
    if data.count(old_bytes) != 1:
        raise InvalidEvidence("Build.MutationPreimage", old)
    start = data.index(old_bytes)
    end = start + len(old_bytes)
    return data[:start] + new_bytes + data[end:], {
        "start": start, "end": end, "old_text": old, "new_text": new,
    }


def _body(records: list[str]) -> bytes:
    return (HEADER + COMMON + "\n".join(records) + "\n" + END).encode("ascii")


def box_records() -> list[str]:
    result = [
        "#20=ADVANCED_BREP_SHAPE_REPRESENTATION('body',(#21),#12);",
        "#21=MANIFOLD_SOLID_BREP('',#22);",
        "#22=CLOSED_SHELL('',(#507,#517,#527,#537,#547,#557));",
    ]
    points = [(2, -3, 5), (12, -3, 5), (12, 17, 5), (2, 17, 5),
              (2, -3, 35), (12, -3, 35), (12, 17, 35), (2, 17, 35)]
    for i, point in enumerate(points):
        xyz = ",".join(f"{n}." for n in point)
        result.append(f"#{100+i}=CARTESIAN_POINT('',({xyz}));")
    result += [f"#{110+i}=VERTEX_POINT('',#{100+i});" for i in range(8)]
    ends = [(0, 1), (1, 2), (2, 3), (3, 0), (4, 5), (5, 6),
            (6, 7), (7, 4), (0, 4), (1, 5), (2, 6), (3, 7)]
    directions = [(1, 0, 0), (0, 1, 0), (-1, 0, 0), (0, -1, 0)] * 2 + [(0, 0, 1)] * 4
    for i, ((a, b), direction) in enumerate(zip(ends, directions)):
        base = 200 + 10 * i
        length = 30 if i >= 8 else (10 if i % 2 == 0 else 20)
        ratios = ",".join(f"{n}." for n in direction)
        result += [
            f"#{base}=DIRECTION('',({ratios}));", f"#{base+1}=VECTOR('',#{base},{length}.);",
            f"#{base+2}=LINE('',#{100+a},#{base+1});",
            f"#{base+3}=EDGE_CURVE('',#{110+a},#{110+b},#{base+2},.T.);",
        ]
    uses = [[(3, False), (2, False), (1, False), (0, False)],
            [(4, True), (5, True), (6, True), (7, True)],
            [(0, True), (9, True), (4, False), (8, False)],
            [(11, True), (6, False), (10, False), (2, True)],
            [(8, True), (7, False), (11, False), (3, True)],
            [(1, True), (10, True), (5, False), (9, False)]]
    for face, use_list in enumerate(uses):
        for j, (edge, orientation) in enumerate(use_list):
            flag = ".T." if orientation else ".F."
            result.append(f"#{400+4*face+j}=ORIENTED_EDGE('',*,*,#{203+10*edge},{flag});")
    normals = [(0, 0, -1), (0, 0, 1), (0, -1, 0), (0, 1, 0), (-1, 0, 0), (1, 0, 0)]
    refs = [(0, 1, 0), (1, 0, 0), (1, 0, 0), (0, 0, 1), (0, 0, 1), (0, 1, 0)]
    origins = [100, 104, 100, 103, 100, 101]
    for i, (normal, ref, origin) in enumerate(zip(normals, refs, origins)):
        base = 500 + 10 * i
        n = ",".join(f"{v}." for v in normal)
        x = ",".join(f"{v}." for v in ref)
        oelist = ",".join(f"#{400+4*i+j}" for j in range(4))
        result += [
            f"#{base+1}=DIRECTION('',({n}));", f"#{base+2}=DIRECTION('',({x}));",
            f"#{base+3}=AXIS2_PLACEMENT_3D('',#{origin},#{base+1},#{base+2});",
            f"#{base+4}=PLANE('',#{base+3});", f"#{base+5}=EDGE_LOOP('',({oelist}));",
            f"#{base+6}=FACE_OUTER_BOUND('',#{base+5},.T.);",
            f"#{base+7}=ADVANCED_FACE('',(#{base+6}),#{base+4},.T.);",
        ]
    return result


def positives(repo: Path) -> tuple[dict[str, bytes], dict[str, dict]]:
    plane_path = repo / DESIGN_DIRECTORY / "ap203e2-sample-plane.md"
    f1 = _document_records(plane_path, "#100=CARTESIAN_POINT")
    f2 = _document_records(plane_path, "#104=CARTESIAN_POINT")
    if len(f1) != 39 or len(f2) != 31:
        raise InvalidEvidence("Build.RecordDesignChanged", [len(f1), len(f2)])
    values = {"P01": _body(f1), "P04": _body(box_records())}
    derivations = {"P01": {"kind": "RootPositive", "parent": None},
                   "P04": {"kind": "RootPositive", "parent": None}}

    def derive(key: str, parent: str, changes: list[tuple[str, str]], extra: str = "") -> None:
        value = values[parent]
        edits = []
        for old, new in changes:
            value, edit = _replace(value, old, new)
            edits.append(edit)
        if extra:
            value, edit = _replace(value, END, extra + END)
            edits.append(edit)
        values[key] = value
        derivations[key] = {"kind": "DerivedPositive", "parent": parent, "edits": edits}

    derive("P02", "P01", [(f1[-1], f2[-1])], "\n".join(f2[:-1]) + "\n")
    derive("P03", "P02", [("#130=CARTESIAN_POINT('',(3.,-2.,5.));",
                            "#130=CARTESIAN_POINT('',(3.4,5.2,5.));")])
    derive("P05", "P04", [("ADVANCED_BREP_SHAPE_REPRESENTATION", "MANIFOLD_SURFACE_SHAPE_REPRESENTATION"),
                            ("#21=MANIFOLD_SOLID_BREP('',#22);", "#21=SHELL_BASED_SURFACE_MODEL('',(#22));")])
    derive("P06", "P01", [("SI_UNIT(.MILLI.,.METRE.)", "SI_UNIT(.CENTI.,.METRE.)")])
    cm = [("SI_UNIT(.MILLI.,.METRE.)", "SI_UNIT(.CENTI.,.METRE.)")]
    for record_id, old, new in [(100, "3.,-2.,5.", "0.3,-0.2,0.5"),
                                 (101, "10.2,7.6,5.", "1.02,0.76,0.5"),
                                 (102, "3.8,12.4,5.", "0.38,1.24,0.5"),
                                 (103, "-3.4,2.8,5.", "-0.34,0.28,0.5"),
                                 (130, "3.,-2.,5.", "0.3,-0.2,0.5")]:
        cm.append((f"#{record_id}=CARTESIAN_POINT('',({old}));",
                   f"#{record_id}=CARTESIAN_POINT('',({new}));"))
    for i, (old, new) in enumerate([("12.", "1.2"), ("8.", "0.8"), ("12.", "1.2"), ("8.", "0.8")]):
        cm.append((f"#{201+10*i}=VECTOR('',#{200+10*i},{old});",
                   f"#{201+10*i}=VECTOR('',#{200+10*i},{new});"))
    derive("P07", "P01", cm)
    ctx = "#12=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11))"
    guunc = "#12=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#13,#14))"
    derive("P08", "P01", [(ctx, guunc + " GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11))")],
           "#13=UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.0001),#10,'distance_accuracy_A',$);\n"
           "#14=UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.0002),#10,'distance_accuracy_B',$);\n")
    derive("P09", "P01", [], "#15=PRODUCT_RELATED_PRODUCT_CATEGORY('part',$,(#4));\n")
    derive("P10", "P01", [("#3=PRODUCT_CONTEXT", "#3=MECHANICAL_CONTEXT"),
                            ("#6=PRODUCT_DEFINITION_CONTEXT", "#6=DESIGN_CONTEXT"),
                            ("#5=PRODUCT_DEFINITION_FORMATION('A',$,#4);",
                             "#5=PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE('A',$,#4,.NOT_KNOWN.);")])
    inch = [("GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11))", "GLOBAL_UNIT_ASSIGNED_CONTEXT((#19,#11))")]
    old_points = ["2.,-3.,5.", "12.,-3.,5.", "12.,17.,5.", "2.,17.,5.",
                  "2.,-3.,35.", "12.,-3.,35.", "12.,17.,35.", "2.,17.,35."]
    new_points = ["0.,0.,0.", "1.,0.,0.", "1.,2.,0.", "0.,2.,0.",
                  "0.,0.,3.", "1.,0.,3.", "1.,2.,3.", "0.,2.,3."]
    for i, (old, new) in enumerate(zip(old_points, new_points)):
        inch.append((f"#{100+i}=CARTESIAN_POINT('',({old}));", f"#{100+i}=CARTESIAN_POINT('',({new}));"))
    for i in range(12):
        old, new = (30, 3) if i >= 8 else ((10, 1) if i % 2 == 0 else (20, 2))
        inch.append((f"#{201+10*i}=VECTOR('',#{200+10*i},{old}.);",
                     f"#{201+10*i}=VECTOR('',#{200+10*i},{new}.);"))
    derive("P11", "P04", inch,
           "#17=DIMENSIONAL_EXPONENTS(1.,0.,0.,0.,0.,0.,0.);\n"
           "#18=LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#10);\n"
           "#19=(CONVERSION_BASED_UNIT('inch',#18) LENGTH_UNIT() NAMED_UNIT(#17));\n")
    derive("P12", "P01", [("GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11))",
                            "GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11,#16))")],
           "#16=(NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT());\n")
    derive("P13", "P11", [("GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT",
                            "GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#13)) "
                            "GLOBAL_UNIT_ASSIGNED_CONTEXT")],
           "#13=UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.0001),#10,'distance_accuracy_A',$);\n")
    return values, derivations


# The strings encode one contiguous byte edit, including delimiters when necessary.
NEGATIVES = [
    ("P01", "{1 0 10303 403 2 1 2}", "{1 0 10303 403 2 1 3}", "D2", "UnsupportedSchemaBinding"),
    ("P01", END, "#100=CARTESIAN_POINT('',(3.,-2.,5.));\n" + END, "D0", "DuplicateInstanceId"),
    ("P01", "SHAPE_DEFINITION_REPRESENTATION(#8,#20)", "SHAPE_DEFINITION_REPRESENTATION(#99999,#20)",
     "D1", "UnresolvedRequiredReference"),
    ("P01", "#3=PRODUCT_CONTEXT('',", "#3=PRODUCT_CONTEXT($,", "D1", "RequiredAttributeMissing"),
    ("P01", "#204=ORIENTED_EDGE('',*,*,", "#204=ORIENTED_EDGE('',$,*,", "D1", "DerivedSlotMarkerRequired"),
    ("P01", "#204=ORIENTED_EDGE('',*,*,#203,.T.)", "#204=ORIENTED_EDGE('',*,*,#203,.U.)",
     "D1", "BooleanValueInvalid"),
    ("P01", "#203=EDGE_CURVE('',#110,", "#203=EDGE_CURVE('',#100,", "D1", "ReferenceTypeMismatch"),
    ("P01", "(#204,#214,#224,#234)", "(#204,#204,#224,#234)", "D1", "AggregateUniqueViolation"),
    ("P04", "(#507,#517,#527,#537,#547,#557)", "(#507,#517,#527,#537,#547,#557,#507)",
     "D1", "SetDuplicateMember"),
    ("P01", "'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011",
     "'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies_mim_lf',2011",
     "D3", "SourceRuleViolation:application_protocol_definition_required.WR1"),
    ("P01", "#200=DIRECTION('',(0.6,0.8,0.))", "#200=DIRECTION('',(0.,0.,0.))",
     "D3", "SourceRuleViolation:DIRECTION.WR1"),
    ("P01", "#201=VECTOR('',#200,12.)", "#201=VECTOR('',#200,-12.)", "D3", "SourceRuleViolation:VECTOR.WR1"),
    ("P01", "#132=DIRECTION('',(0.6,0.8,0.))", "#132=DIRECTION('',(0.,0.,1.))",
     "D3", "SourceRuleViolation:AXIS2_PLACEMENT_3D.WR4"),
    ("P01", "#10=(LENGTH_UNIT() NAMED_UNIT(*)", "#10=(LENGTH_UNIT() NAMED_UNIT($)",
     "D1", "DerivedSlotMarkerRequired"),
    ("P01", "SI_UNIT($,.RADIAN.)", "SI_UNIT(.QUETTA.,.RADIAN.)", "D1", "EnumerationValueInvalid"),
    ("P01", "#6=PRODUCT_DEFINITION_CONTEXT('part definition'", "#6=PRODUCT_DEFINITION_CONTEXT('part occurrence'",
     "D2", "UnsupportedDefinitionContextRole"),
    ("P01", "#301=FACE_OUTER_BOUND", "#301=FACE_BOUND", "D2", "UnsupportedExplicitOuterBoundRequired"),
    ("P01", "#302=ADVANCED_FACE", "#302=FACE_SURFACE", "D2", "UnsupportedFaceSubtype"),
    ("P01", "#201=VECTOR('',#200,12.)", "#201=VECTOR('',#200,0.)", "D4", "NonzeroDefiningLineRequired"),
    ("P01", "#202=LINE('',#100,#201)", "#202=LINE('',#102,#201)", "D4", "EndpointCurveMismatch"),
    ("P01", "#130=CARTESIAN_POINT('',(3.,-2.,5.))", "#130=CARTESIAN_POINT('',(3.,-2.,5.001))",
     "D4", "EdgeSurfaceMismatch"),
    ("P01", "#203=EDGE_CURVE('',#110,#111,#202,.T.)", "#203=EDGE_CURVE('',#110,#111,#202,.F.)",
     "D4", "EdgeCurveSenseMismatch"),
    ("P01", "#214=ORIENTED_EDGE('',*,*,#213,.T.)", "#214=ORIENTED_EDGE('',*,*,#213,.F.)",
     "D3", "SourceRuleViolation:PATH.WR1"),
    ("P01", "#233=EDGE_CURVE('',#113,#110,", "#233=EDGE_CURVE('',#113,#111,",
     "D3", "SourceRuleViolation:EDGE_LOOP.WR1"),
    ("P02", "#311=FACE_BOUND('',#310,.T.)", "#311=FACE_BOUND('',#310,.F.)",
     "D4", "InnerBoundaryOrientationMismatch"),
    ("P01", "#22=OPEN_SHELL", "#22=CLOSED_SHELL", "D4", "ClaimedClosedShellHasBoundary"),
    ("P04", "#507=ADVANCED_FACE('',(#506),#504,.T.)", "#507=ADVANCED_FACE('',(#506),#504,.F.)",
     "D4", "FaceBoundaryOrShellOrientationMismatch"),
    ("P04", "(#507,#517,#527,#537,#547,#557)", "(#517,#527,#537,#547,#557)",
     "D2", "UnownedOrOrphanProfileContent"),
    ("P08", "'distance_accuracy_B'", "'distance_accuracy_A'", "D3", "SourceRuleViolation:consistent_uncertainty.WR1"),
    ("P08", "#13=UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.0001)",
     "#13=UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.)", "D3",
     "SourceRuleViolation:UNCERTAINTY_MEASURE_WITH_UNIT.WR1"),
    ("P11", "#17=DIMENSIONAL_EXPONENTS(1.,", "#17=DIMENSIONAL_EXPONENTS(0.,",
     "D3", "SourceRuleViolation:LENGTH_UNIT.WR1+CONVERSION_BASED_UNIT.WR1"),
    ("P11", "LENGTH_MEASURE(25.4)", "LENGTH_MEASURE(0.)", "D4", "UnitScaleNotPositive"),
    ("P11", "LENGTH_MEASURE(25.4)", "LENGTH_MEASURE(-25.4)", "D4", "UnitScaleNotPositive"),
    ("P13", "#18=LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#10)",
     "#18=LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#19)", "D4", "CyclicUnitDefinition"),
    ("P09", "PRODUCT_RELATED_PRODUCT_CATEGORY('part'", "PRODUCT_RELATED_PRODUCT_CATEGORY('ply'",
     "D2", "UnsupportedProductCategory"),
    ("P01", "#3=PRODUCT_CONTEXT('',#1,'mechanical')", "#3=PRODUCT_CONTEXT('',#1,'electrical')",
     "D2", "UnsupportedProductContextDiscipline"),
]


def all_cases(repo: Path) -> tuple[dict[str, bytes], dict[str, dict]]:
    values, derivations = positives(repo)
    for i, (parent, old, new, stage, diagnostic) in enumerate(NEGATIVES, 1):
        key = f"N{i:02}"
        if i == 2:
            position = values[parent].index(END.encode("ascii"))
            added = "#100=CARTESIAN_POINT('',(3.,-2.,5.));\n"
            values[key] = values[parent][:position] + added.encode("ascii") + values[parent][position:]
            edit = {"start": position, "end": position, "old_text": "", "new_text": added}
        else:
            values[key], edit = _replace(values[parent], old, new)
        derivations[key] = {"kind": "SingleMutationNegative", "parent": parent,
                            "edits": [edit], "stage": stage, "diagnostic": diagnostic}
    return {f"S203-{key}": value for key, value in sorted(values.items())}, {
        f"S203-{key}": {**value, "parent": f"S203-{value['parent']}" if value["parent"] else None}
        for key, value in derivations.items()
    }
