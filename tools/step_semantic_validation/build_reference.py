# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Extract narrow audit facts from the already pinned N2635 source, not a runtime schema compiler."""

from __future__ import annotations

import argparse
from pathlib import Path
import re

from tools.step_import_validation.common import digest, write_json

SHA = "c68dd02200e2f4213e311553a4f6295effd168209de1fdc4e8003ce4c9ddd088"
SIMPLE = """CARTESIAN_POINT DIRECTION VECTOR AXIS2_PLACEMENT_3D LINE PLANE VERTEX_POINT EDGE_CURVE ORIENTED_EDGE
EDGE_LOOP FACE_BOUND FACE_OUTER_BOUND ADVANCED_FACE OPEN_SHELL CLOSED_SHELL MANIFOLD_SOLID_BREP
SHELL_BASED_SURFACE_MODEL ADVANCED_BREP_SHAPE_REPRESENTATION MANIFOLD_SURFACE_SHAPE_REPRESENTATION
APPLICATION_CONTEXT APPLICATION_PROTOCOL_DEFINITION PRODUCT_CONTEXT MECHANICAL_CONTEXT PRODUCT
PRODUCT_DEFINITION_FORMATION PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE PRODUCT_DEFINITION_CONTEXT
DESIGN_CONTEXT PRODUCT_DEFINITION PRODUCT_DEFINITION_SHAPE SHAPE_DEFINITION_REPRESENTATION
PRODUCT_RELATED_PRODUCT_CATEGORY DIMENSIONAL_EXPONENTS MEASURE_WITH_UNIT LENGTH_MEASURE_WITH_UNIT
UNCERTAINTY_MEASURE_WITH_UNIT""".split()
FACETS = """REPRESENTATION_CONTEXT GEOMETRIC_REPRESENTATION_CONTEXT GLOBAL_UNIT_ASSIGNED_CONTEXT
GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT NAMED_UNIT SI_UNIT LENGTH_UNIT PLANE_ANGLE_UNIT SOLID_ANGLE_UNIT
CONVERSION_BASED_UNIT""".split()
GUARDS = ["MASS_UNIT", "DERIVED_UNIT", "DERIVED_UNIT_ELEMENT", "FOUNDED_ITEM"]


def extract(schema: Path) -> dict:
    payload = schema.read_bytes()
    if digest(payload) != SHA:
        raise ValueError("Schema does not match the pinned N2635 byte identity")
    text = payload.decode("latin1").replace("\r", "")
    entities = {}

    def visit(name):
        if name in entities:
            return
        match = re.search(r"\bENTITY\s+" + name + r"\b(.*?)\bEND_ENTITY\s*;", text, re.I | re.S)
        if not match:
            raise ValueError(f"Missing pinned declaration {name}")
        body = re.sub(r"\(\*.*?\*\)", "", match[1], flags=re.S)
        header, tail = body.split(";", 1)
        parent = re.search(r"SUBTYPE\s+OF\s*\((.*?)\)", header, re.I | re.S)
        parents = [v.strip().upper() for v in parent[1].split(",")] if parent else []
        explicit = re.split(r"^\s*(?:DERIVE|INVERSE|WHERE|UNIQUE)\b", tail, flags=re.I | re.M)[0]
        fields = [{"name": n.lower(), "declaration": " ".join(t.upper().split())}
                  for n, t in re.findall(r"\b([a-z][a-z_0-9]*)\s*:\s*(.*?);", explicit, re.I | re.S)]
        derives = []
        section = re.search(r"\bDERIVE\b(.*?)(?:\bINVERSE\b|\bWHERE\b|\bUNIQUE\b|\Z)", tail, re.I | re.S)
        if section:
            derives = [{"origin": o.upper(), "name": n.lower()} for o, n in
                       re.findall(r"SELF\s*\\\s*([a-z_0-9]+)\s*\.\s*([a-z_0-9]+)\s*:", section[1], re.I)]
        entities[name] = {"parents": parents, "explicit_fields": fields, "derived_redeclarations": derives}
        for parent in parents:
            visit(parent)

    for name in SIMPLE + FACETS + GUARDS:
        visit(name)
    return {"schema_sha256": SHA, "authority": "pinned MBx N2635 implementation artifact, not certified final ISO",
            "source_url": "https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/"
                          "data/ap203e2/ap203e2_mim_lf.exp",
            "method": "independent source-declaration extraction; no native output or C++ catalog used",
            "simple_profile_leaves": sorted(SIMPLE), "entities": entities}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError("Use a new reference revision; existing output is never overwritten")
    reference = extract(args.schema)
    write_json(args.output, reference)
    print({"entities": len(reference["entities"]), "simple_leaves": len(reference["simple_profile_leaves"]),
           "sha256": digest(args.output.read_bytes())})


if __name__ == "__main__":
    main()
