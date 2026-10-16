# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Actual-native ticket-03 contract tests; STEP_SEMANTIC_BINARY is mandatory."""

from __future__ import annotations

from collections import Counter
from copy import deepcopy
from fractions import Fraction
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_import_validation.common import InvalidEvidence, digest, load_json, write_json
from tools.step_import_validation.corpus import verify
from tools.step_import_validation.oracle import exact
from tools.step_semantic_validation import runner
from tools.step_semantic_validation.oracle import PREFIX, compare, reference

REPO = Path(__file__).resolve().parents[3]
CORPUS = REPO / "tools/step_import_validation/corpus/ap203e2-planar/v1"


class SemanticTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        configured = os.environ.get("STEP_SEMANTIC_BINARY")
        if not configured:
            raise RuntimeError("Set STEP_SEMANTIC_BINARY; tests must execute the real C++ reader")
        cls.binary = Path(configured).resolve(strict=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="step-ticket03-")
        cls.root = Path(cls.temporary.name).resolve()
        assert cls.root.parent == Path(tempfile.gettempdir()).resolve()
        cls.cases = verify(CORPUS)["cases"]
        cls.p01 = (CORPUS / "cases/S203-P01/input.step").read_bytes()

    @classmethod
    def tearDownClass(cls):
        assert cls.root.parent == Path(tempfile.gettempdir()).resolve() and cls.root.name.startswith("step-ticket03-")
        cls.temporary.cleanup()

    def invoke(self, data, *options):
        path = self.root / "probe.step"
        path.write_bytes(data)
        result = subprocess.run([str(self.binary), "--fixture-limits", str(path), *map(str, options)],
                                capture_output=True, timeout=10)
        return result, json.loads(result.stdout) if result.stdout else None

    def checked(self, data, *options):
        result, observation = self.invoke(data, *options)
        self.assertEqual(0, result.returncode, result.stderr or result.stdout)
        self.assertEqual("Checked", observation["status"])
        self.assertIsNotNone(observation["snapshot"])
        self.assertEqual(data.decode("ascii"), observation["source"]["input"]["raw"])
        return observation

    def failed(self, data, key, code=1, *options):
        result, observation = self.invoke(data, *options)
        self.assertEqual(code, result.returncode, result.stderr or result.stdout)
        self.assertEqual(key, observation["diagnostics"][0]["key"])
        self.assertIsNone(observation["snapshot"])
        return observation

    def changed(self, old, new, source=None):
        source = self.p01 if source is None else source
        self.assertEqual(1, source.count(old), "test mutation preimage must be unique")
        return source.replace(old, new, 1)

    def test_all_49_frozen_contracts_with_independent_schema_witness(self):
        counts = Counter()
        for case in self.cases:
            with self.subTest(case=case["case_id"]):
                data = (CORPUS / case["identity"]["materials"]["input"]["path"]).read_bytes()
                actual, observed = self.invoke(data)
                inventory = load_json(CORPUS / case["identity"]["materials"]["inventory"]["path"])
                result = compare(case, observed, actual.returncode, data, inventory)
                self.assertEqual("Matched", result["status"])
                self.assertEqual("NotChecked", result["full_import_contract"])
                counts[observed["status"]] += 1
        self.assertEqual({"Checked": 26, "Deferred": 6, "Rejected": 8, "Unsupported": 7, "SourceFailure": 2}, counts)

    def test_schema_binding_does_not_guess_alias_or_oid_from_apd(self):
        original = b"AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF"
        old = original + b" {1 0 10303 403 2 1 2}"
        for identifier in (original, original.lower() + b" {1 0 10303 403 2 1 2}", b"CONFIG_CONTROL_DESIGN",
                           original + b" {1 0 10303 403 2 1 3}", original + b" 1 0 {10303 403 2 1 2}",
                           original + b" {1 0 10303 403 2 1 2} suffix"):
            with self.subTest(identifier=identifier):
                self.failed(self.changed(old, identifier), "UnsupportedSchemaBinding", 2)
        self.checked(self.changed(old, original + b"   { 1  0 10303 403 2 1 2 }  "))

    def test_apd_text_and_year_are_distinct_from_header_registry(self):
        for year in (b"0", b"-20", b"+02011", b"123456789012345678901234567890"):
            data = self.changed(b",2011,#1)", b"," + year + b",#1)")
            observed = self.checked(data)
            self.assertEqual(year.decode(), observed["snapshot"]["part"]["apd_year_lexeme"])
        observed = self.checked((CORPUS / "cases/S203-N10/input.step").read_bytes())
        self.assertTrue(observed["snapshot"]["part"]["apd_schema_claim"].endswith("_mim_lf"))
        self.assertEqual("NotRun", observed["snapshot"]["part"]["apd_global_rule"])

    def test_diamond_ancestors_and_context_facets_are_not_duplicate_instances(self):
        snapshot = self.checked(self.p01)["snapshot"]
        populations = snapshot["type_populations"]
        self.assertEqual(["300"], populations["LOOP"])
        self.assertEqual(["300"], populations["PATH"])
        self.assertEqual(["9"], populations["PROPERTY_DEFINITION_REPRESENTATION"])
        self.assertEqual(["12"], populations["REPRESENTATION_CONTEXT"])
        row = next(r for r in snapshot["records"] if r["id"] == "110")
        self.assertTrue({"VERTEX_POINT", "VERTEX", "GEOMETRIC_REPRESENTATION_ITEM",
                         "TOPOLOGICAL_REPRESENTATION_ITEM", "REPRESENTATION_ITEM"} <= set(row["types"]))
        self.assertEqual(1, sum(a["name"] == "name" for a in row["attributes"]))

    def test_required_empty_string_and_optional_missing_are_distinct(self):
        self.checked(self.p01)
        self.failed(self.changed(b"#3=PRODUCT_CONTEXT('',", b"#3=PRODUCT_CONTEXT($,"), "RequiredAttributeMissing")
        self.checked(self.changed(b"#5=PRODUCT_DEFINITION_FORMATION('A',$,",
                                  b"#5=PRODUCT_DEFINITION_FORMATION('A','',"))
        self.failed(self.changed(b"#3=PRODUCT_CONTEXT('',", b"#3=PRODUCT_CONTEXT(1,"), "AttributeTypeMismatch")

    def test_slot_count_and_unexpected_derived_marker(self):
        self.failed(self.changed(b"#3=PRODUCT_CONTEXT('',#1,'mechanical')", b"#3=PRODUCT_CONTEXT('',#1)"),
                    "AttributeCountMismatch")
        self.failed(self.changed(b"#3=PRODUCT_CONTEXT('',", b"#3=PRODUCT_CONTEXT(*,"), "AttributeTypeMismatch")

    def test_integer_and_real_transport_types_are_not_silently_coerced(self):
        self.failed(self.changed(b"#201=VECTOR('',#200,12.)", b"#201=VECTOR('',#200,12)"), "AttributeTypeMismatch")
        self.failed(self.changed(b"CONTEXT(3) GLOBAL_UNIT", b"CONTEXT(3.) GLOBAL_UNIT"), "AttributeTypeMismatch")

    def test_complex_inheritance_slots_order_and_unknown_members(self):
        self.failed(self.changed(b"LENGTH_UNIT() NAMED_UNIT(*)", b"LENGTH_UNIT()"), "MissingComplexConstituent")
        self.failed(self.changed(b"LENGTH_UNIT() NAMED_UNIT(*)", b"NAMED_UNIT(*) LENGTH_UNIT()"),
                    "ComplexConstituentOrderInvalid")
        self.failed(self.changed(b"LENGTH_UNIT() NAMED_UNIT(*)", b"LENGTH_UNIT() LENGTH_UNIT() NAMED_UNIT(*)"),
                    "DuplicateComplexConstituent")
        self.failed(self.changed(b"LENGTH_UNIT() NAMED_UNIT(*)", b"LENGTH_UNIT() MYSTERY() NAMED_UNIT(*)"),
                    "UnsupportedComplexConstituent", 2)
        self.failed(self.changed(b"LENGTH_UNIT() NAMED_UNIT(*)", b"LENGTH_UNIT(1.) NAMED_UNIT(*)"),
                    "AttributeCountMismatch")

    def test_known_oneof_subtypes_do_not_become_a_merged_unit(self):
        self.failed(self.changed(b"LENGTH_UNIT() NAMED_UNIT(*)", b"LENGTH_UNIT() NAMED_UNIT(*) PLANE_ANGLE_UNIT()"),
                    "ComplexSubtypeConflict")

    def test_subtype_conflict_precedes_order_and_missing_constituent(self):
        data = self.changed(b"LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.)",
                            b"SI_UNIT(.MILLI.,.METRE.) LENGTH_UNIT() PLANE_ANGLE_UNIT()")
        observed = self.failed(data, "ComplexSubtypeConflict")
        diagnostic = observed["diagnostics"][0]
        self.assertEqual(("10", "D1", []),
                         (diagnostic["record_id"], diagnostic["stage"], diagnostic["parameter_path"]))

    def test_si_derived_dimensions_and_oe_endpoints_require_star(self):
        self.failed(self.changed(b"NAMED_UNIT(*) SI_UNIT(.MILLI.", b"NAMED_UNIT($) SI_UNIT(.MILLI."),
                    "DerivedSlotMarkerRequired")
        self.failed(self.changed(b"#204=ORIENTED_EDGE('',*,*,", b"#204=ORIENTED_EDGE('',#110,*,"),
                    "DerivedSlotMarkerRequired")

    def test_boolean_is_not_logical_unknown(self):
        self.failed(self.changed(b"#204=ORIENTED_EDGE('',*,*,#203,.T.)", b"#204=ORIENTED_EDGE('',*,*,#203,.U.)"),
                    "BooleanValueInvalid")

    def test_real_reference_type_is_checked_even_when_points_coincide(self):
        observed = self.failed((CORPUS / "cases/S203-N07/input.step").read_bytes(), "ReferenceTypeMismatch")
        diagnostic = observed["diagnostics"][0]
        self.assertEqual(("EDGE", "edge_start", "VERTEX", "CARTESIAN_POINT"),
                         tuple(diagnostic[k] for k in ("declared_in", "field", "expected", "actual")))

    def test_list_unique_and_set_are_identity_based(self):
        self.failed((CORPUS / "cases/S203-N08/input.step").read_bytes(), "AggregateUniqueViolation")
        self.failed((CORPUS / "cases/S203-N09/input.step").read_bytes(), "SetDuplicateMember")
        data = self.changed(b"(#204,#214,#224,#234)", b"(#204,#999,#214,#224,#234)")
        data = self.changed(b"ENDSEC;\nEND-ISO-10303-21;",
                            b"#999=ORIENTED_EDGE('',*,*,#203,.T.);\nENDSEC;\nEND-ISO-10303-21;", data)
        self.checked(data)  # Distinct OE identities may reference the same underlying EC; PATH is ticket 04.

    def test_aggregate_minimum_bounds_are_not_optional_defaults(self):
        self.failed(self.changed(b"#4=PRODUCT('P0-PART','P0 analytic fixture',$,(#3))",
                                 b"#4=PRODUCT('P0-PART','P0 analytic fixture',$,())"), "AggregateCardinalityViolation")
        self.failed(self.changed(b"#100=CARTESIAN_POINT('',(3.,-2.,5.))", b"#100=CARTESIAN_POINT('',())"),
                    "AggregateCardinalityViolation")

    def test_all_fixed_si_prefixes_use_exact_powers(self):
        for prefix, exponent in PREFIX.items():
            if prefix is None:
                token = b"$"
            else:
                token = ("." + prefix + ".").encode()
            with self.subTest(prefix=prefix):
                observed = self.checked(self.changed(b"SI_UNIT(.MILLI.,.METRE.)", b"SI_UNIT(" + token + b",.METRE.)"))
                self.assertEqual(exact(Fraction(10) ** (exponent + 3)),
                                 observed["snapshot"]["units"]["length_scale_mm"])
        for prefix in (b"YOTTA", b"QUETTA", b"RONT0"):
            self.failed(self.changed(b".MILLI.", b"." + prefix + b"."), "EnumerationValueInvalid")

    def test_unused_angle_prefix_is_still_checked(self):
        self.failed(self.changed(b"SI_UNIT($,.RADIAN.)", b"SI_UNIT(.QUETTA.,.RADIAN.)"), "EnumerationValueInvalid")
        observation = self.checked(self.changed(b"SI_UNIT($,.RADIAN.)", b"SI_UNIT(.MILLI.,.RADIAN.)"))
        angle = next(u for u in observation["snapshot"]["units"]["si_definitions"] if u["quantity"] == "PlaneAngle")
        self.assertEqual(exact(Fraction(1, 1000)), angle["scale"])

    def test_nominal_radian_and_steradian_are_not_interchanged(self):
        self.failed(self.changed(b"SI_UNIT($,.RADIAN.)", b"SI_UNIT($,.STERADIAN.)"), "UnsupportedSiUnitKind", 2)
        observation = self.checked((CORPUS / "cases/S203-P12/input.step").read_bytes())
        units = observation["snapshot"]["units"]
        self.assertEqual(("11", "16"), (units["plane_angle_unit"], units["solid_angle_unit"]))
        self.assertEqual({"PlaneAngle", "SolidAngle"},
                         {u["quantity"] for u in units["si_definitions"] if u["source_id"] != "10"})

    def test_missing_conflicting_or_wrong_units_do_not_guess_mm(self):
        data = self.changed(b"GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11))", b"GLOBAL_UNIT_ASSIGNED_CONTEXT((#11))")
        data = self.changed(b"#10=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.));\n", b"", data)
        self.failed(data, "UnsupportedUnitAssignment", 2)
        data = self.changed(b"GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11))", b"GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#17,#11))")
        data = self.changed(b"ENDSEC;\nEND-ISO-10303-21;",
                            b"#17=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT($,.METRE.));\nENDSEC;\nEND-ISO-10303-21;", data)
        self.failed(data, "UnsupportedUnitAssignment", 2)

    def test_dimension_count_and_3d_profile_are_separate(self):
        self.failed(self.changed(b"CONTEXT(3) GLOBAL_UNIT", b"CONTEXT(0) GLOBAL_UNIT"),
                    "SourceRuleViolation:DIMENSION_COUNT.WR1")
        self.failed(self.changed(b"CONTEXT(3) GLOBAL_UNIT", b"CONTEXT(2) GLOBAL_UNIT"),
                    "UnsupportedCoordinateDimension", 2)
        self.checked(self.changed(b"CONTEXT(3) GLOBAL_UNIT", b"CONTEXT(+003) GLOBAL_UNIT"))

    def test_bound_field_failures_keep_inherited_and_complex_locations(self):
        cases = (
            (b"'mechanical'", b"'electrical'", "UnsupportedProductContextDiscipline", 2,
             "3", "PRODUCT_CONTEXT", "discipline_type", "PRODUCT_CONTEXT", [3], b"'electrical'"),
            (b"'part definition'", b"'assembly'", "UnsupportedDefinitionContextRole", 2,
             "6", "PRODUCT_DEFINITION_CONTEXT", "name", "APPLICATION_CONTEXT_ELEMENT", [1], b"'assembly'"),
            (b"GEOMETRIC_REPRESENTATION_CONTEXT(3)", b"GEOMETRIC_REPRESENTATION_CONTEXT(2)",
             "UnsupportedCoordinateDimension", 2, "12", "GEOMETRIC_REPRESENTATION_CONTEXT",
             "coordinate_space_dimension", "GEOMETRIC_REPRESENTATION_CONTEXT", [1], b"2"),
            (b"SI_UNIT($,.RADIAN.)", b"SI_UNIT($,.STERADIAN.)", "UnsupportedSiUnitKind", 2,
             "11", "SI_UNIT", "name", "SI_UNIT", [2], b".STERADIAN."),
        )
        for old, new, key, code, owner, component, field, declared, path, primary in cases:
            with self.subTest(key=key):
                data = self.changed(old, new)
                diagnostic = self.failed(data, key, code)["diagnostics"][0]
                self.assertEqual((owner, component, field, declared, path),
                                 (diagnostic["record_id"], diagnostic["component"], diagnostic["field"],
                                  diagnostic["declared_in"], diagnostic["parameter_path"]))
                span = diagnostic["span"]
                self.assertEqual(primary, data[span[0]:span[1]])

    def test_bound_aggregate_member_keeps_member_span_and_attribute_path(self):
        data = self.changed(b"('body',(#21),#12)", b"('body',(#100),#12)")
        diagnostic = self.failed(data, "UnsupportedRepresentationItem", 2)["diagnostics"][0]
        self.assertEqual(("20", "items", "REPRESENTATION", [2]),
                         (diagnostic["record_id"], diagnostic["field"],
                          diagnostic["declared_in"], diagnostic["parameter_path"]))
        span = diagnostic["span"]
        self.assertEqual(b"#100", data[span[0]:span[1]])

    def test_single_part_identity_does_not_merge_product_label(self):
        data = self.changed(b"ENDSEC;\nEND-ISO-10303-21;",
                            b"#999=PRODUCT('P0-PART','duplicate label',$,(#3));\nENDSEC;\nEND-ISO-10303-21;")
        failed = self.failed(data, "UnsupportedSinglePartGraph", 2)
        self.assertIn("not PRODUCT.id uniqueness", failed["diagnostics"][0]["responsibility"])

    def test_other_definition_roles_and_discipline_have_profile_responsibility(self):
        self.failed((CORPUS / "cases/S203-N16/input.step").read_bytes(), "UnsupportedDefinitionContextRole", 2)
        self.failed((CORPUS / "cases/S203-N36/input.step").read_bytes(), "UnsupportedProductContextDiscipline", 2)

    def test_optional_and_legacy_branches_keep_specific_identity(self):
        self.checked((CORPUS / "cases/S203-P08/input.step").read_bytes())
        self.checked((CORPUS / "cases/S203-P09/input.step").read_bytes())
        snapshot = self.checked((CORPUS / "cases/S203-P10/input.step").read_bytes())["snapshot"]
        self.assertEqual(["3"], snapshot["type_populations"]["MECHANICAL_CONTEXT"])
        self.assertEqual(["6"], snapshot["type_populations"]["DESIGN_CONTEXT"])
        self.assertEqual(["5"], snapshot["type_populations"]["PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE"])
        n30 = self.checked((CORPUS / "cases/S203-N30/input.step").read_bytes())
        self.assertEqual("NotRun", n30["checks"]["uncertainty_evaluation"])

    def test_legacy_local_predicates_are_not_called_profile_rules(self):
        p10 = (CORPUS / "cases/S203-P10/input.step").read_bytes()
        self.failed(self.changed(b"'mechanical'", b"'electrical'", p10), "SourceRuleViolation:MECHANICAL_CONTEXT.WR1")
        self.failed(self.changed(b"#1,'design')", b"#1,'manufacturing')", p10),
                    "SourceRuleViolation:DESIGN_CONTEXT.WR1")

    def test_conversion_and_cycles_are_explicitly_deferred(self):
        for key in ("P11", "P13", "N31", "N32", "N33", "N34"):
            data = (CORPUS / f"cases/S203-{key}/input.step").read_bytes()
            result, observation = self.invoke(data)
            self.assertEqual(7, result.returncode)
            self.assertEqual("Deferred", observation["status"])
            self.assertIsNone(observation["snapshot"]["units"]["length_scale_mm"])
            self.assertEqual("NotRun", observation["checks"]["conversion_units"])

    def test_conversion_dimensions_remain_explicit_before_deferral(self):
        p11 = (CORPUS / "cases/S203-P11/input.step").read_bytes()
        self.failed(self.changed(b"NAMED_UNIT(#17)", b"NAMED_UNIT(*)", p11), "AttributeTypeMismatch")

    def test_source_shape_claim_does_not_validate_closedness_or_direction(self):
        for key in ("N11", "N12", "N13", "N19", "N20", "N21", "N22", "N23", "N24", "N25", "N26", "N27"):
            observation = self.checked((CORPUS / f"cases/S203-{key}/input.step").read_bytes())
            self.assertFalse(observation["snapshot"]["part"]["shape_geometry_verified"])
            self.assertEqual("NotRun", observation["checks"]["geometry_consistency"])

    def test_orphan_content_is_not_discarded_or_given_a_new_root(self):
        failed = self.failed((CORPUS / "cases/S203-N28/input.step").read_bytes(), "UnownedOrOrphanProfileContent", 2)
        self.assertEqual(11, len(failed["diagnostics"][0]["actual"].split()))
        extra = self.changed(b"ENDSEC;\nEND-ISO-10303-21;",
                             b"#999=CARTESIAN_POINT('',(1.,2.,3.));\nENDSEC;\nEND-ISO-10303-21;")
        self.failed(extra, "UnownedOrOrphanProfileContent", 2)

    def test_unknown_required_content_is_never_skipped(self):
        self.failed(self.changed(b"#100=CARTESIAN_POINT", b"#100=ALIEN_POINT"), "UnsupportedEntityType", 2)

    def test_whole_file_reverse_index_and_filtered_representation_use(self):
        snapshot = self.checked(self.p01)["snapshot"]
        self.assertEqual({"ac_context_elements": ["3", "6"], "context_representations": ["20"]},
                         snapshot["part"]["inverse_roles"])
        by_id = {r["id"]: r for r in snapshot["records"]}
        self.assertFalse(by_id["2"]["used_by_representation"])
        self.assertFalse(by_id["10"]["used_by_representation"])
        self.assertTrue(by_id["100"]["used_by_representation"])

    def test_record_reordering_and_sparse_renumbering_preserve_roles(self):
        import re
        data = self.p01
        data = re.sub(rb"#(\d+)", lambda match: b"#" + str(int(match[1]) + 1000000000000000000000).encode(), data)
        prefix, body = data.split(b"\nDATA;\n", 1)
        records, tail = body.split(b"ENDSEC;", 1)
        data = prefix + b"\nDATA;\n" + b"\n".join(reversed(records.rstrip().splitlines())) + b"\nENDSEC;" + tail
        observation = self.checked(data)
        self.assertEqual("1000000000000000000007", observation["snapshot"]["part"]["roles"]["definition"])

    def test_visits_limit_is_real_and_does_not_publish_partial_semantics(self):
        baseline = self.checked(self.p01)
        count = baseline["statistics"]["dependency_visits"]
        self.checked(self.p01, "--max-semantic-visits", count)
        self.failed(self.p01, "DependencyVisitLimit", 3, "--max-semantic-visits", count - 1)
        self.failed(self.p01, "InvalidSemanticLimits", 3, "--max-semantic-visits", 10000001)

    def test_total_report_budget_and_raw_capture_failure(self):
        first, _ = self.invoke(self.p01)
        budget = len(first.stdout)
        for _ in range(3):
            result, _ = self.invoke(self.p01, "--max-report-bytes", budget)
            self.assertEqual(0, result.returncode, result.stderr)
            budget = len(result.stdout)
        result, body = self.invoke(self.p01, "--max-report-bytes", budget - 1)
        self.assertEqual(5, result.returncode)
        self.assertEqual(b"", result.stdout)
        self.assertIsNone(body)
        self.failed(self.p01, "InputByteLimit", 3, "--max-input-bytes", len(self.p01) - 1)

    def test_checker_controls_detect_missing_ancestor_si_scale_and_claimed_geometry(self):
        case = next(c for c in self.cases if c["case_id"] == "S203-P01")
        actual, good = self.invoke(self.p01)
        inventory = load_json(CORPUS / "cases/S203-P01/inventory.json")
        for error in ("ancestor", "scale", "geometry", "inverse", "origin", "scope"):
            bad = deepcopy(good)
            if error == "ancestor":
                next(r for r in bad["snapshot"]["records"] if r["id"] == "300")["types"].remove("LOOP")
            elif error == "scale":
                bad["snapshot"]["units"]["length_scale_mm"] = exact(1000)
            elif error == "geometry":
                bad["snapshot"]["part"]["shape_geometry_verified"] = True
            elif error == "inverse":
                bad["snapshot"]["part"]["inverse_roles"]["ac_context_elements"].append("2")
            elif error == "origin":
                row = next(r for r in bad["snapshot"]["records"] if r["id"] == "203")
                row["attributes"][1]["declared_in"] = "EDGE_CURVE"
            else:
                bad["checks"]["global_rules_47"] = "Passed"
            with self.subTest(error=error), self.assertRaises(InvalidEvidence):
                compare(case, bad, actual.returncode, self.p01, inventory)

    def test_evidence_selected_cases_do_not_count_others(self):
        path = runner.run(self.binary, CORPUS, self.root / "reports", ["S203-P01", "S203-N07", "S203-N34"])
        self.assertEqual(3, runner.check(path)["scope_contract_matched"])
        summary = load_json(path / "summary.json")
        self.assertIn("SI单位", (path / "summary.html").read_text(encoding="utf-8"))
        self.assertNotIn("单位解释、几何导入及SMLib构建均未运行", (path / "summary.html").read_text(encoding="utf-8"))
        self.assertEqual({"Checked": 1, "Rejected": 1, "Deferred": 1}, summary["semantic_statuses"])
        self.assertIsNone(summary["import_success_count"])
        before = digest((path / "completion.json").read_bytes())
        other = runner.run(self.binary, CORPUS, self.root / "reports", ["S203-P01"])
        self.assertNotEqual(path.name, other.name)
        self.assertEqual(before, digest((path / "completion.json").read_bytes()))

    def test_witness_keeps_unique_attribute_and_multi_inheritance(self):
        witness = reference()
        self.assertEqual(["LOOP", "PATH"], witness["entities"]["EDGE_LOOP"]["parents"])
        self.assertIn("OF UNIQUE ORIENTED_EDGE", witness["entities"]["PATH"]["explicit_fields"][0]["declaration"])
        self.assertEqual(36, len(witness["simple_profile_leaves"]))


if __name__ == "__main__":
    unittest.main()
