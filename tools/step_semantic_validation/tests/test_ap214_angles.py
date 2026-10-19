# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Pinned AP214 angle measures: dimensional and nominal rules are distinct."""
from fractions import Fraction
import unittest

from tools.step_semantic_validation.tests.test_ap214 import AP214, CORPUS, ap214_source, converted_ap214_source
from tools.step_semantic_validation.tests.test_ap214_entities import append_records
from tools.step_semantic_validation.tests import test_ap214_units as units


MBX = b"AUTOMOTIVE_DESIGN {1 0 10303 214 1 1 1 1}"


def angle_source():
    data = ap214_source((CORPUS / "S203-P01/input.step").read_bytes())
    return angle_context(data)


def angle_context(data):
    """Keep geometry unchanged while selecting a source conversion angle."""
    data = data.replace(b"#11=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));",
                        b"#11=(CONVERSION_BASED_UNIT('source angle',#902) NAMED_UNIT(*) PLANE_ANGLE_UNIT());")
    return append_records(data, b"""#901=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));
#902=PLANE_ANGLE_MEASURE_WITH_UNIT(PLANE_ANGLE_MEASURE(+0.0174532925199433E+0),#901);
""")


class Ap214AngleTests(unittest.TestCase):
    invoke = units.Ap214UnitTests.invoke
    failed = units.Ap214UnitTests.failed
    checked = units.Ap214UnitTests.checked
    dimensions = units.Ap214UnitTests.dimensions

    def test_both_profile_identities_retain_angle_factor_and_derived_marker(self):
        for identifier in (AP214, MBX):
            with self.subTest(identifier=identifier):
                data = angle_source().replace(AP214, identifier)
                result = self.checked(data)
                self.assertEqual([Fraction(0)] * 7, self.dimensions(result, "11"))
                self.assertEqual("NotRun", result["snapshot"]["units"]["conversion_status"])
                self.assertEqual("NotRun", result["checks"]["native_build"])
                records = {r["id"]: r for r in result["snapshot"]["records"]}
                fields = records["902"]["attributes"]
                self.assertEqual([("value_component", "MEASURE_VALUE"), ("unit_component", "UNIT")],
                                 [(a["name"], a["declared_type"]) for a in fields])
                self.assertTrue(all(a["declared_in"] == "MEASURE_WITH_UNIT" for a in fields))
                dimensions = next(a for a in records["11"]["attributes"] if a["name"] == "dimensions")
                self.assertEqual((True, "CONVERSION_BASED_UNIT"),
                                 (dimensions["derived_slot"], dimensions["redeclared_by"]))
                raw = next(r for r in result["source"]["records"] if r["id"] == "902")
                self.assertEqual("PLANE_ANGLE_MEASURE_WITH_UNIT", raw["components"][0]["name"])
                self.assertIn("PLANE_ANGLE_MEASURE(+0.0174532925199433E+0),#901", data.decode())
                self.assertIn("source angle", result["source"]["input"]["raw"])

    def test_generic_angle_measure_checks_dimensions_without_subtype_nominality(self):
        # AP214E3_2010.exp valid_units requires a zero dimensional vector;
        # a generic MWU does not acquire the angle subtype's nominal WR1.
        data = angle_source().replace(b"PLANE_ANGLE_MEASURE_WITH_UNIT(", b"MEASURE_WITH_UNIT(")
        data = data.replace(b"#901=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));",
                            b"#901=NAMED_UNIT(#903);\n#903=DIMENSIONAL_EXPONENTS(0.,0.,0.,0.,0.,0.,0.);")
        self.checked(data)
        self.failed(data.replace(b"DIMENSIONAL_EXPONENTS(0.,", b"DIMENSIONAL_EXPONENTS(1.,"),
                    "SourceRuleViolation:PLANE_ANGLE_UNIT.WR1")

    def test_generic_measure_dimension_mismatch_has_its_own_rule(self):
        # Keep the conversion LENGTH_UNIT dimensionally valid so the MWU
        # value predicate, rather than the enclosing nominal unit, fails first.
        data = converted_ap214_source().replace(b"LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4)",
                                                b"MEASURE_WITH_UNIT(PLANE_ANGLE_MEASURE(25.4)")
        result = self.failed(data, "SourceRuleViolation:MEASURE_WITH_UNIT.WR1")
        self.assertEqual("unit_component", result["diagnostics"][0]["field"])

    def test_subtype_nominal_rule_applies_independently_of_the_value_tag(self):
        data = converted_ap214_source().replace(b"LENGTH_MEASURE_WITH_UNIT(", b"PLANE_ANGLE_MEASURE_WITH_UNIT(")
        for options in ((), ("--interop-nongeometric",)):
            with self.subTest(options=options):
                run, result = self.invoke(data, *options)
                self.assertEqual(1, run.returncode)
                self.assertIsNone(result["snapshot"])
                self.assertEqual("SourceRuleViolation:PLANE_ANGLE_MEASURE_WITH_UNIT.WR1",
                                 result["diagnostics"][0]["key"])
                self.assertEqual("unit_component", result["diagnostics"][0]["field"])

    def test_nonangle_dimensionless_unit_cannot_satisfy_the_subtype(self):
        data = angle_source().replace(b"#901=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));",
                                     b"#901=(NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT());")
        self.failed(data, "SourceRuleViolation:PLANE_ANGLE_MEASURE_WITH_UNIT.WR1")

    def test_angle_transport_and_unit_cycles_keep_existing_failures(self):
        for old, new, key in (
            (b"+0.0174532925199433E+0", b"1", "AttributeTypeMismatch"),
            (b"NAMED_UNIT(*) PLANE_ANGLE_UNIT());", b"NAMED_UNIT($) PLANE_ANGLE_UNIT());",
             "DerivedSlotMarkerRequired"),
            (b"E+0),#901", b"E+0),#11", "SourceUnitDependencyCycle"),
        ):
            with self.subTest(key=key):
                self.failed(angle_source().replace(old, new), key)

    def test_angle_measure_does_not_guess_conversion_scale_or_change_source(self):
        # AP214's CBU declaration derives dimensions; this source pass does
        # not evaluate the scalar conversion or manufacture a radian scale.
        for token in (b"0.", b"-1.", b"1.E999999999999999999999999"):
            with self.subTest(token=token):
                result = self.checked(angle_source().replace(b"+0.0174532925199433E+0", token))
                self.assertEqual("NotRun", result["snapshot"]["units"]["conversion_status"])

    def test_angle_predicates_respect_the_exact_visit_budget(self):
        data = angle_source()
        result = self.checked(data)
        visits = result["statistics"]["dependency_visits"]
        self.assertEqual(0, self.invoke(data, "--max-semantic-visits", visits)[0].returncode)
        stopped, limited = self.invoke(data, "--max-semantic-visits", visits - 1)
        self.assertEqual(3, stopped.returncode)
        self.assertIsNone(limited["snapshot"])

    def test_p0_keeps_its_closed_declarations(self):
        data = append_records((CORPUS / "S203-P01/input.step").read_bytes(),
                              b"#902=PLANE_ANGLE_MEASURE_WITH_UNIT(PLANE_ANGLE_MEASURE(1.),#11);\n")
        self.failed(data, "UnsupportedEntityType", 2)
