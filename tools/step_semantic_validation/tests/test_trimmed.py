# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Pinned TRIMMED_CURVE fields, mixed SELECTs and dependency closure."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests import test_ap214_geometry as geometry
from tools.step_semantic_validation.tests.test_ap214 import AP203, AP214


def trimmed_source(trim1=b"PARAMETER_VALUE(0.0)", trim2=b"PARAMETER_VALUE(1.25)", master=b".PARAMETER."):
    source = geometry.geometry_source("ELLIPSE")
    basis = b"ELLIPSE('source ellipse',#133,1.25,2.50)"
    replacement = b"TRIMMED_CURVE('',#950,(" + trim1 + b"),(" + trim2 + b"),.F.," + master + b")"
    assert source.count(basis) == 1
    source = source.replace(basis, replacement)
    return source.replace(b"ENDSEC;\nEND-ISO", b"#950=" + basis + b";\nENDSEC;\nEND-ISO")


class TrimmedSourceTests(unittest.TestCase):
    def invoke(self, source, *options):
        with tempfile.TemporaryDirectory(prefix="step-trimmed-source-") as folder:
            path = Path(folder) / "source.step"
            path.write_bytes(source)
            result = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path), *options],
                                    capture_output=True, timeout=30)
        return result, json.loads(result.stdout) if result.stdout else None

    def rejected(self, source, key):
        run, result = self.invoke(source)
        self.assertEqual(1, run.returncode, run.stderr or result)
        self.assertEqual(key, result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])
        self.assertEqual(source.decode("ascii"), result["source"]["input"]["raw"])
        return result

    def test_source_parameters_and_mixed_trim_selects_are_lossless_in_all_general_profiles(self):
        original = trimmed_source()
        variants = [(original, ()),
                    (original.replace(AP214, b"AUTOMOTIVE_DESIGN {1 0 10303 214 1 1 1 1}"), ()),
                    (original.replace(AP214, AP203).replace(b"'automotive_design',2010",
                     b"'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011"),
                     ("--general-brep-source",))]
        for source, options in variants:
            with self.subTest(options=options):
                run, result = self.invoke(source, *options)
                self.assertEqual(0, run.returncode, run.stderr or result)
                self.assertEqual(source.decode("ascii"), result["source"]["input"]["raw"])
                record = next(r for r in result["snapshot"]["records"] if r["id"] == "202")
                self.assertEqual(["name", "basis_curve", "trim_1", "trim_2", "sense_agreement", "master_representation"],
                                 [a["name"] for a in record["attributes"]])
        for members in [(b"#100", b"#101"), (b"#100,PARAMETER_VALUE(0.0)", b"PARAMETER_VALUE(1.25),#101")]:
            source = trimmed_source(*members)
            run, result = self.invoke(source)
            self.assertEqual(0, run.returncode, run.stderr or result)
            self.assertEqual(source.decode("ascii"), result["source"]["input"]["raw"])

    def test_trim_sets_check_cardinality_select_types_and_exact_duplicate_values(self):
        self.rejected(trimmed_source(b""), "AggregateCardinalityViolation")
        self.rejected(trimmed_source(b"#100,#101,PARAMETER_VALUE(0.0)"), "AggregateCardinalityViolation")
        self.rejected(trimmed_source(b"#133"), "ReferenceTypeMismatch")
        self.rejected(trimmed_source(b"LENGTH_MEASURE(0.0)"), "AttributeTypeMismatch")
        self.rejected(trimmed_source(b"0.0"), "AttributeTypeMismatch")
        self.rejected(trimmed_source(b"#100,#100"), "SetDuplicateMember")
        self.rejected(trimmed_source(b"PARAMETER_VALUE(0.0),PARAMETER_VALUE(0.E0)"), "SetDuplicateMember")
        self.rejected(trimmed_source(master=b".UNKNOWN."), "EnumerationValueInvalid")

    def test_two_distinct_members_of_the_same_select_type_violate_the_pinned_where(self):
        for member in [b"#100,#101", b"PARAMETER_VALUE(0.0),PARAMETER_VALUE(1.0)"]:
            self.rejected(trimmed_source(member), "SourceRuleViolation:TRIMMED_CURVE.WR1")
            self.rejected(trimmed_source(trim2=member), "SourceRuleViolation:TRIMMED_CURVE.WR2")

    def test_basis_reference_is_a_curve_and_dependency_cycles_are_rejected(self):
        source = trimmed_source()
        self.rejected(source.replace(b"TRIMMED_CURVE('',#950", b"TRIMMED_CURVE('',#133"), "ReferenceTypeMismatch")
        self.rejected(source.replace(b"TRIMMED_CURVE('',#950", b"TRIMMED_CURVE('',#202"), "SourceGeometryDependencyViolation")
        nested = (b"#950=TRIMMED_CURVE('',#202,(PARAMETER_VALUE(0.)),(PARAMETER_VALUE(1.)),.T.,.PARAMETER.);")
        self.rejected(source.replace(b"#950=ELLIPSE('source ellipse',#133,1.25,2.50);", nested),
                      "SourceGeometryDependencyViolation")
        # Reference nesting is not lexical nesting. Check a finite chain without
        # using the native layer or relying on recursive stack depth.
        chain = b"".join(f"#{n}=TRIMMED_CURVE('',#{n+1},(PARAMETER_VALUE(0.)),"
                         f"(PARAMETER_VALUE(1.)),.T.,.PARAMETER.);\n".encode("ascii") for n in range(950, 970))
        chain += b"#970=ELLIPSE('source ellipse',#133,1.25,2.50);"
        run, result = self.invoke(source.replace(b"#950=ELLIPSE('source ellipse',#133,1.25,2.50);", chain))
        self.assertEqual(0, run.returncode, run.stderr or result)
