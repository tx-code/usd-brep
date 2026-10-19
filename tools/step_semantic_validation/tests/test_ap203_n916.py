# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Fixed modified N916 subset; a name-only HEADER never becomes an edition proof."""
import unittest

from tools.step_semantic_validation.tests import test_ap203_general as general
from tools.step_semantic_validation.tests.test_ap214 import AP203, CORPUS
from tools.step_semantic_validation.tests.test_ap214_entities import append_records

N916 = b"CONFIG_CONTROL_DESIGN"
SHA = "020b4d25dbd0b6ee7d15099b978e3448f6699a72cb862d381e416e32187562f1"


def legacy_source(data=None):
    if data is None:
        data = (CORPUS / "S203-P04/input.step").read_bytes()
    data = data.replace(AP203, N916)
    data = data.replace(b"'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011",
                        b"'config_control_design',2000")
    # Author a legal synthetic legacy carrier; never transform customer files.
    # N916 requires these TEXT slots, unlike the N2635 optional descriptions.
    for old, new in (
        (b"PRODUCT('P0-PART','P0 analytic fixture',$,", b"PRODUCT('P0-PART','P0 analytic fixture','',"),
        (b"PRODUCT_DEFINITION_FORMATION('A',$,", b"PRODUCT_DEFINITION_FORMATION('A','',"),
        (b"PRODUCT_DEFINITION('D1',$,", b"PRODUCT_DEFINITION('D1','',"),
        (b"PRODUCT_DEFINITION_SHAPE('',$,", b"PRODUCT_DEFINITION_SHAPE('','',"),
    ):
        data = data.replace(old, new)
    return data


def layer_source(target=21):
    record = f"#900=PRESENTATION_LAYER_ASSIGNMENT('source layer','source description',(#{target}));\n"
    return append_records(legacy_source(), record.encode())


class Ap203N916Tests(unittest.TestCase):
    invoke = general.Ap203GeneralTests.invoke

    def check(self, data, *options):
        run, result = self.invoke(data, "--general-brep-source", *options)
        self.assertEqual(0, run.returncode, run.stderr or result["diagnostics"])
        self.assertEqual(data.decode(), result["source"]["input"]["raw"])
        return result

    def test_name_only_profile_preserves_header_and_does_not_infer_an_edition(self):
        result = self.check(legacy_source())
        identifier, = result["schema_selection"]["identifiers"]
        self.assertEqual(("CONFIG_CONTROL_DESIGN", "NameOnly", [], None),
                         tuple(identifier[k] for k in ("name", "match", "oid", "registry_id")))
        profile = result["schema_selection"]["qualified_profile"]
        self.assertEqual("203-N916-Source-0.1", profile["profile"])
        self.assertEqual(SHA, profile["express_sha256"])
        self.assertEqual("NameOnlyGeneralSourceSubset", profile["binding_basis"])
        self.assertIs(False, profile["header_edition_verified"])
        self.assertEqual("NotRun", result["checks"]["global_rules_47"])

    def test_default_scope_and_neighboring_header_declarations_stay_unsupported(self):
        run, result = self.invoke(legacy_source())
        self.assertEqual(2, run.returncode)
        self.assertIsNone(result["schema_selection"]["qualified_profile"])
        for identifier in (b"CONFIG_CONTROL_DESIGN {1 0 10303 203 1}", b"CONFIG_CONTROL_DESIGN_2"):
            with self.subTest(identifier=identifier):
                run, result = self.invoke(legacy_source().replace(N916, identifier), "--general-brep-source")
                self.assertEqual(2, run.returncode)
                self.assertIsNone(result["snapshot"])

    def test_n916_required_descriptions_remain_required_under_interoperability(self):
        for tag in (b"PRODUCT_DEFINITION_FORMATION('A','',", b"PRODUCT_DEFINITION('D1','',",
                    b"PRODUCT_DEFINITION_SHAPE('','',"):
            with self.subTest(tag=tag):
                data = legacy_source().replace(tag, tag[:-3] + b"$,", 1)
                run, result = self.invoke(data, "--general-brep-source", "--interop-nongeometric")
                self.assertEqual(1, run.returncode, result["diagnostics"])
                self.assertEqual("RequiredAttributeMissing", result["diagnostics"][0]["key"])
                self.assertIsNone(result["snapshot"])

    def test_approval_select_matches_n916_instead_of_n2635(self):
        data = append_records(legacy_source(), b"""#900=APPROVAL_STATUS('approved');
#901=APPROVAL(#900,'design');
#902=CC_DESIGN_APPROVAL(#901,(#7));
""")
        result = self.check(data)
        self.assertEqual([], result.get("preserved_diagnostics", []))
        for options in ((), ("--interop-nongeometric",)):
            run, invalid = self.invoke(data.replace(b"#901,(#7)", b"#901,(#4)"), "--general-brep-source", *options)
            self.assertEqual(1, run.returncode)
            self.assertEqual("ReferenceTypeMismatch", invalid["diagnostics"][0]["key"])
            self.assertIsNone(invalid["snapshot"])

    def test_foreign_layer_needs_explicit_policy_and_keeps_original_type_diagnostic(self):
        data = layer_source()
        strict_run, strict = self.invoke(data, "--general-brep-source")
        self.assertEqual(2, strict_run.returncode)
        self.assertEqual("UnsupportedEntityType", strict["diagnostics"][0]["key"])
        result = self.check(data, "--interop-nongeometric")
        issue, = result["preserved_diagnostics"]
        self.assertEqual(strict["diagnostics"][0], issue)
        self.assertEqual("900", issue["record_id"])
        self.assertEqual("CheckedWithDiagnostics", result["status"])

    def test_foreign_layer_cannot_create_ownership_or_become_a_physical_unit(self):
        data = append_records(layer_source(901), b"#901=CARTESIAN_POINT('unowned',(1.,2.,3.));\n")
        run, result = self.invoke(data, "--general-brep-source", "--interop-nongeometric")
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedAdministrativeAttachment", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])
        data = layer_source().replace(b"#10=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.));",
                                      b"#10=(LENGTH_UNIT() NAMED_UNIT(#900));")
        run, result = self.invoke(data, "--general-brep-source", "--interop-nongeometric")
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(result["snapshot"])

    def test_borrowed_descriptors_do_not_admit_other_foreign_entities(self):
        for entity in (b"DERIVED_UNIT", b"MEASURE_REPRESENTATION_ITEM", b"STYLED_ITEM"):
            with self.subTest(entity=entity):
                data = append_records(legacy_source(), b"#900=" + entity + b"();\n")
                run, result = self.invoke(data, "--general-brep-source", "--interop-nongeometric")
                self.assertEqual(2, run.returncode)
                self.assertIsNone(result["snapshot"])

    def test_source_and_preservation_budgets_remain_atomic(self):
        data = layer_source()
        result = self.check(data, "--interop-nongeometric")
        visits = result["statistics"]["dependency_visits"]
        self.assertEqual(0, self.invoke(data, "--general-brep-source", "--interop-nongeometric",
                                       "--max-semantic-visits", visits)[0].returncode)
        for option, value in (("--max-semantic-visits", visits - 1), ("--max-preserved-diagnostics", 0)):
            run, limited = self.invoke(data, "--general-brep-source", "--interop-nongeometric", option, value)
            self.assertEqual(3, run.returncode)
            self.assertIsNone(limited["snapshot"])

    def test_absent_apd_needs_policy_and_never_creates_a_claim_or_role(self):
        data = b"\n".join(line for line in legacy_source().split(b"\n") if not line.startswith(b"#2="))
        strict_run, strict = self.invoke(data, "--general-brep-source")
        self.assertEqual(2, strict_run.returncode)
        result = self.check(data, "--interop-nongeometric")
        issue, = result["preserved_diagnostics"]
        self.assertEqual(strict["diagnostics"][0], issue)
        self.assertEqual("UnsupportedSinglePartGraph", issue["key"])
        self.assertNotIn("APPLICATION_PROTOCOL_DEFINITION(", result["source"]["input"]["raw"])
        self.assertIs(False, result["schema_selection"]["qualified_profile"]["header_edition_verified"])
        self.assertIsNone(result["snapshot"]["part"]["apd_status"])
        self.assertIsNone(result["snapshot"]["part"]["apd_schema_claim"])

    def test_conversion_prerequisites_do_not_invent_n916_cbu_where_rules(self):
        data = legacy_source().replace(
            b"#11=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));",
            b"""#11=(CONVERSION_BASED_UNIT('source angle',#902)
 NAMED_UNIT(#900) PLANE_ANGLE_UNIT());
#900=DIMENSIONAL_EXPONENTS(0.,0.,0.,0.,0.,0.,0.);
#901=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));
#902=PLANE_ANGLE_MEASURE_WITH_UNIT(PLANE_ANGLE_MEASURE(0.0174532925),#901);""")
        self.check(data)
        for broken, key in (
            (data.replace(b"DIMENSIONAL_EXPONENTS(0.,", b"DIMENSIONAL_EXPONENTS(1.,"),
             "SourceConversionDimensionsUnestablished"),
            (data.replace(b"PLANE_ANGLE_MEASURE(0.0174532925)", b"PLANE_ANGLE_MEASURE(0.)"),
             "SourceUnitScaleNonPositive"),
            (data.replace(b"0.0174532925),#901", b"0.0174532925),#11"), "SourceUnitDependencyCycle"),
        ):
            with self.subTest(key=key):
                run, result = self.invoke(broken, "--general-brep-source", "--interop-nongeometric")
                self.assertEqual(2, run.returncode, result["diagnostics"])
                self.assertEqual(key, result["diagnostics"][0]["key"])
                self.assertEqual("ProfileUnsupported", result["diagnostics"][0]["classification"])
                self.assertNotIn("CONVERSION_BASED_UNIT.WR1", result["diagnostics"][0]["responsibility"])
                self.assertEqual(broken.decode(), result["source"]["input"]["raw"])
                self.assertIsNone(result["snapshot"])

    def test_n916_utc_enum_excludes_the_later_exact_literal(self):
        data = append_records(legacy_source(), b"""#900=CALENDAR_DATE(2026,19,10);
#901=COORDINATED_UNIVERSAL_TIME_OFFSET(0,$,.AHEAD.);
#902=LOCAL_TIME(10,0,0.,#901);
#903=DATE_AND_TIME(#900,#902);
#904=DATE_TIME_ROLE('created');
#905=CC_DESIGN_DATE_AND_TIME_ASSIGNMENT(#903,#904,(#7));
""")
        result = self.check(data)
        record = next(r for r in result["snapshot"]["records"] if r["id"] == "901")
        self.assertEqual("HOUR_IN_DAY", record["attributes"][0]["declared_type"])
        run, invalid = self.invoke(data.replace(b".AHEAD.", b".EXACT."),
                                   "--general-brep-source", "--interop-nongeometric")
        self.assertEqual(1, run.returncode)
        self.assertIsNone(invalid["snapshot"])
