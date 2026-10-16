# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Real C++ CLI contracts and independent-checker controls. No OCCT/SMLib calls."""
from __future__ import annotations

from collections import Counter
from copy import deepcopy
from fractions import Fraction as F
import itertools
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tools.step_import_validation.common import InvalidEvidence, load_json
from tools.step_import_validation.corpus import verify
from tools.step_import_validation.oracle import exact, decode
from tools.step_import_validation.records import inventory, by_id
from tools.step_planar_validation.oracle import compare, reference
from tools.step_planar_validation import runner

CORPUS = Path(__file__).resolve().parents[2] / "step_import_validation/corpus/ap203e2-planar/v1"


class PlanarTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.environ.get("STEP_PLANAR_BINARY"):
            raise RuntimeError("STEP_PLANAR_BINARY must identify the actual native reader")
        cls.binary = Path(os.environ["STEP_PLANAR_BINARY"]).resolve(strict=True)
        cls.temp = tempfile.TemporaryDirectory(prefix="step-ticket04-")
        cls.root = Path(cls.temp.name).resolve()
        cls.cases = verify(CORPUS)["cases"]
        for case in cls.cases:
            case["truth"] = load_json(CORPUS / case["identity"]["materials"]["truth"]["path"])
        cls.p01 = cls.input("S203-P01")

    @classmethod
    def tearDownClass(cls):
        assert cls.root.parent == Path(tempfile.gettempdir()).resolve() and cls.root.name.startswith("step-ticket04-")
        cls.temp.cleanup()

    @staticmethod
    def input(case):
        return (CORPUS / f"cases/{case}/input.step").read_bytes()

    def invoke(self, data, *options):
        path = self.root / "probe.step"
        path.write_bytes(data)
        result = subprocess.run([str(self.binary), "--fixture-limits", "--quality", "Q-P0-mm-01", str(path),
                                 *map(str, options)], capture_output=True, timeout=15)
        return result, json.loads(result.stdout) if result.stdout else None

    def checked(self, data, *options):
        actual, observed = self.invoke(data, *options)
        self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
        self.assertEqual("Checked", observed["status"])
        self.assertTrue(observed["qualified_source_evidence"])
        self.assertIsNone(observed["model"])
        self.assertEqual("NotRun", observed["checks"]["native_build"])
        return observed

    def failed(self, data, key, code=1, *options):
        actual, observed = self.invoke(data, *options)
        self.assertEqual(code, actual.returncode, actual.stderr or actual.stdout)
        self.assertFalse(observed["qualified_source_evidence"])
        self.assertEqual(key, observed["diagnostics"][0]["key"])
        self.assertIsNone(observed["model"])
        self.assertEqual("NotRun", observed["checks"]["native_build"])
        return observed

    def record(self, data, identity, body):
        result, count = re.subn(rb"(?m)^#" + str(identity).encode() + rb"=.*;$",
                                lambda _: f"#{identity}={body};".encode(), data)
        self.assertEqual(1, count, "Unique mutation preimage")
        return result

    def rule(self, observed, name):
        return next(r for r in observed["rules"] if r["name"] == name)

    def test_all_49_frozen_diagnostic_and_math_contracts(self):
        counts = Counter()
        for case in self.cases:
            with self.subTest(case=case["case_id"]):
                data = self.input(case["case_id"])
                actual, observed = self.invoke(data)
                ledger = load_json(CORPUS / case["identity"]["materials"]["inventory"]["path"])
                self.assertEqual("Matched", compare(case, observed, actual.returncode, data, ledger)["status"])
                counts[observed["status"]] += 1
        self.assertEqual(Counter(Checked=11, Rejected=15, UpstreamFailure=17, Deferred=6), counts)

    def test_upstream_failure_keeps_captured_source_location(self):
        for case in ("S203-N02", "S203-N03"):
            with self.subTest(case=case):
                actual, observed = self.invoke(self.input(case))
                self.assertEqual(1, actual.returncode)
                self.assertEqual("UpstreamFailure", observed["status"])
                diagnostic = observed["diagnostics"][0]
                raw = observed["source_semantics"]["source"]["diagnostics"][0]
                self.assertIsNotNone(raw["location"])
                self.assertEqual(raw["key"], diagnostic["key"])
                self.assertEqual(raw["span"], diagnostic["span"])
                self.assertEqual(raw["location"], diagnostic["location"])

    def test_47_global_and_ancestor_population_proofs(self):
        observed = self.checked(self.p01)
        rules = [r for r in observed["rules"] if r["kind"] == "GLOBAL"]
        self.assertEqual(47, len(rules))
        self.assertEqual(Counter(Mandatory=5, Conditional=1, Guarded=41), Counter(r["applicability"] for r in rules))
        self.assertEqual(["9"], rules[35]["populations"]["PROPERTY_DEFINITION_REPRESENTATION"])
        self.assertEqual(["300"], rules[46]["populations"]["LOOP"])
        self.assertEqual(59, len(rules[46]["populations"]))
        self.assertEqual("1", rules[35]["facts"]["correlation_UNKNOWN_count"])

    def test_global_execution_order_matches_independent_schema(self):
        observed = self.checked(self.p01)
        names = [rule["name"] for rule in observed["rules"] if rule["kind"] == "GLOBAL"]
        self.assertEqual([rule["name"] for rule in reference()["rules"]], names)

    def test_first_global_failure_and_field_survive_multiple_violations(self):
        original = b"'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies'"
        unsupported_claim = original[:-1] + b"_mim_lf'"
        data = self.input("S203-N29")
        self.assertEqual(1, data.count(original))
        data = data.replace(original, unsupported_claim)
        observed = self.failed(data, "SourceRuleViolation:application_protocol_definition_required.WR1")
        diagnostic = observed["diagnostics"][0]
        self.assertEqual(("2", "application_interpreted_model_schema_name", [2]),
                         (diagnostic["record_id"], diagnostic["field"], diagnostic["parameter_path"]))
        start = data.index(unsupported_claim)
        self.assertEqual([start, start + len(unsupported_claim)], diagnostic["span"])
        self.assertEqual(["alternative_solution_requires_solution_definition", "application_protocol_definition_required"],
                         [rule["name"] for rule in observed["rules"] if rule["kind"] == "GLOBAL"])
        uncertainty = self.failed(self.input("S203-N29"), "SourceRuleViolation:consistent_uncertainty.WR1")
        diagnostic = uncertainty["diagnostics"][0]
        self.assertEqual(("12", "uncertainty", [1]),
                         (diagnostic["record_id"], diagnostic["field"], diagnostic["parameter_path"]))

    def test_nonempty_category_does_not_activate_ply_outer_loop(self):
        observed = self.checked(self.input("S203-P09"))
        rule = self.rule(observed, "ply_stock_material_select")
        self.assertEqual(["15"], rule["populations"]["PRODUCT_RELATED_PRODUCT_CATEGORY"])
        self.assertEqual("TRUE", rule["logical_value"])
        self.assertEqual([], self.rule(observed, "component_class_for_assembly_select")["populations"][
            "COMPOSITE_ASSEMBLY_SEQUENCE_DEFINITION"])

    def test_path_and_closure_have_different_responsibilities(self):
        n23 = self.failed(self.input("S203-N23"), "SourceRuleViolation:PATH.WR1")
        self.assertEqual("2", self.rule(n23, "PATH.WR1")["facts"]["adjacent_identity_mismatches"])
        n24 = self.failed(self.input("S203-N24"), "SourceRuleViolation:EDGE_LOOP.WR1")
        self.assertEqual("TRUE", self.rule(n24, "PATH.WR1")["logical_value"])
        self.assertEqual("FALSE", self.rule(n24, "EDGE_LOOP.WR1")["logical_value"])

    def test_zero_line_is_not_a_vector_where_violation(self):
        observed = self.failed(self.input("S203-N19"), "NonzeroDefiningLineRequired")
        self.assertEqual("TRUE", self.rule(observed, "VECTOR.WR1")["logical_value"])
        self.assertEqual("Passed", observed["checks"]["local_rules"])

    def test_8mm_endpoint_residual_exact(self):
        observed = self.failed(self.input("S203-N20"), "EndpointCurveMismatch")
        self.assertEqual(exact(64), observed["metrics"][0]["exact"]["start_distance_squared_mm"])
        self.assertEqual(exact(-1), observed["edges"][0]["t_start_coefficient"])

    def test_point001mm_plane_residual_exact(self):
        observed = self.failed(self.input("S203-N21"), "EdgeSurfaceMismatch")
        metric = next(v for v in observed["metrics"] if v["kind"] == "edge_surface")
        self.assertEqual(exact(F(1, 10**6)), metric["exact"]["vertex_start_squared_mm"])

    def test_16_legal_sense_combinations_preserve_same_world_region(self):
        for reverse_curve, swap_edge, reverse_bound, reverse_face in itertools.product([False, True], repeat=4):
            with self.subTest(factors=(reverse_curve, swap_edge, reverse_bound, reverse_face)):
                data = self.p01
                if reverse_curve:
                    data = self.record(data, 200, "DIRECTION('',(-0.6,-0.8,0.))")
                ec_sense = ".F." if reverse_curve ^ swap_edge else ".T."
                data = self.record(data, 203, f"EDGE_CURVE('',#{111 if swap_edge else 110},"
                                             f"#{110 if swap_edge else 111},#202,{ec_sense})")
                if reverse_bound:
                    data = self.record(data, 300, "EDGE_LOOP('',(#234,#224,#214,#204))")
                    data = self.record(data, 301, "FACE_OUTER_BOUND('',#300,.F.)")
                for oe, ec in [(204, 203), (214, 213), (224, 223), (234, 233)]:
                    sense = reverse_bound ^ (swap_edge and oe == 204)
                    data = self.record(data, oe, f"ORIENTED_EDGE('',*,*,#{ec},{'.F.' if sense else '.T.'})")
                if reverse_face:
                    data = self.record(data, 131, "DIRECTION('',(0.,0.,-1.))")
                    data = self.record(data, 302, "ADVANCED_FACE('',(#301),#134,.F.)")
                observed = self.checked(data)
                self.assertEqual(exact(9216), observed["faces"][0]["projected_region_area_squared_mm"])
                self.assertEqual([exact(F(17, 5)), exact(F(26, 5)), exact(5)],
                                 observed["faces"][0]["projected_region_centroid_mm"])

    def test_nonunit_direction_and_nondefault_line_parameters(self):
        data = self.record(self.p01, 200, "DIRECTION('',(3.,4.,0.))")
        observed = self.checked(data)
        self.assertEqual(exact(5), observed["edges"][0]["t_end_coefficient"])
        self.assertEqual(exact(25), observed["edges"][0]["t_divisor_squared"])
        data = self.record(self.p01, 202, "LINE('',#101,#201)")
        observed = self.checked(data)
        self.assertEqual(exact(-1), observed["edges"][0]["t_start_coefficient"])
        self.assertEqual(exact(0), observed["edges"][0]["t_end_coefficient"])

    def test_source_uv_origin_and_cm_no_double_scaling(self):
        p03 = self.checked(self.input("S203-P03"))
        self.assertEqual([exact(-6), exact(-4)], p03["faces"][0]["loops"][0]["projected"][0])
        p06 = self.checked(self.input("S203-P06"))
        p07 = self.checked(self.input("S203-P07"))
        self.assertEqual(exact(10), p06["faces"][0]["source_length_scale_mm"])
        self.assertEqual(exact(92160000), p06["faces"][0]["projected_region_area_squared_mm"])
        self.assertEqual(exact(9216), p07["faces"][0]["projected_region_area_squared_mm"])
        self.assertTrue(all(decode(e["t_end_coefficient"]) == 1 for e in p06["edges"] + p07["edges"]))

    def test_irrational_normalisation_is_algebraic(self):
        observed = self.checked(self.record(self.p01, 132, "DIRECTION('',(1.,1.,0.))"))
        self.assertEqual(exact(2), observed["faces"][0]["x_norm_squared"])
        self.assertEqual(exact(9216), observed["faces"][0]["projected_region_area_squared_mm"])

    def test_missing_axis_or_ref_uses_exact_source_defaults(self):
        for axis, ref_direction in [("$", "#132"), ("#131", "$"), ("$", "$")]:
            data = self.record(self.p01, 133, f"AXIS2_PLACEMENT_3D('',#130,{axis},{ref_direction})")
            for identity, absent in [(131, axis == "$"), (132, ref_direction == "$")]:
                if absent:
                    data = re.sub(rb"(?m)^#" + str(identity).encode() + rb"=.*;\n", b"", data)
            self.checked(data)

    def test_auxiliary_placement_derived_indeterminate_is_not_unknown_true(self):
        data = self.record(self.p01, 20, "MANIFOLD_SURFACE_SHAPE_REPRESENTATION('',(#21,#900),#12)")
        data = data.replace(b"ENDSEC;\nEND-ISO", b"#900=AXIS2_PLACEMENT_3D('',#100,$,#901);\n"
                            b"#901=DIRECTION('',(0.,0.,1.));\nENDSEC;\nEND-ISO")
        observed = self.failed(data, "DerivedPlacementAxesIndeterminate", 8)
        self.assertEqual("Indeterminate", observed["status"])
        self.assertEqual("Passed", observed["checks"]["local_rules"])

    def test_auxiliary_placement_default_ref_for_plus_minus_x(self):
        for axis in (1, -1):
            data = self.record(self.p01, 20, "MANIFOLD_SURFACE_SHAPE_REPRESENTATION('',(#21,#900),#12)")
            extra = f"#900=AXIS2_PLACEMENT_3D('',#100,#901,$);\n#901=DIRECTION('',({axis}.,0.,0.));\n".encode()
            data = data.replace(b"ENDSEC;\nEND-ISO", extra + b"ENDSEC;\nEND-ISO")
            self.checked(data)

    def test_exact_consistency_boundary_and_strict_failure(self):
        boundary = self.record(self.p01, 130, "CARTESIAN_POINT('',(3.,-2.,5.0000001))")
        self.checked(boundary)
        self.failed(self.record(self.p01, 130, "CARTESIAN_POINT('',(3.,-2.,5.0000002))"), "EdgeSurfaceMismatch")

    def test_numeric_capacity_is_indeterminate_with_provenance(self):
        data = self.record(self.p01, 200, "DIRECTION('',(1.E200,0.8,0.))")
        observed = self.failed(data, "ExactArithmeticCapacityExceeded", 8)
        self.assertEqual("200", observed["diagnostics"][0]["record_id"])
        self.assertEqual("Indeterminate", observed["rules"][-1]["evaluation"])
        self.assertIsNone(observed["rules"][-1]["logical_value"])

    def test_uncertainty_rules_and_named_type_ancestors(self):
        data = self.input("S203-P08").replace(b"LENGTH_MEASURE(0.0001)", b"POSITIVE_LENGTH_MEASURE(0.0001)")
        observed = self.checked(data)
        self.assertEqual("TRUE", self.rule(observed, "NON_NEGATIVE_LENGTH_MEASURE.WR1")["logical_value"])
        self.assertEqual("TRUE", self.rule(observed, "POSITIVE_LENGTH_MEASURE.WR1")["logical_value"])
        self.failed(self.input("S203-N29"), "SourceRuleViolation:consistent_uncertainty.WR1")
        self.failed(self.input("S203-N30"), "SourceRuleViolation:UNCERTAINTY_MEASURE_WITH_UNIT.WR1")

    def polygon(self, points, hole=None):
        data = self.p01 if hole is None else self.input("S203-P02")
        all_points = points + (hole or [])
        for i, p in enumerate(all_points):
            data = self.record(data, 100+i, "CARTESIAN_POINT('',(" + ",".join(f"{v}." for v in (*p, 0)) + "))")
        data = self.record(data, 130, "CARTESIAN_POINT('',(0.,0.,0.))")
        data = self.record(data, 132, "DIRECTION('',(1.,0.,0.))")
        for ring, offset in [(points, 0)] + ([(hole, 4)] if hole is not None else []):
            for j, p in enumerate(ring):
                q = ring[(j+1) % len(ring)]
                i = offset+j
                data = self.record(data, 200+10*i, f"DIRECTION('',({q[0]-p[0]}.,{q[1]-p[1]}.,0.))")
                data = self.record(data, 201+10*i, f"VECTOR('',#{200+10*i},1.)")
        return data

    def test_self_crossing_polygon_passes_source_rules_but_fails_region(self):
        data = self.polygon([(0, 0), (4, 3), (0, 3), (4, 0)])
        observed = self.failed(data, "PlanarBoundaryIntersection")
        self.assertEqual("Passed", observed["checks"]["global_rules_47"])

    def test_adjacent_backtracking_polygon_rejected(self):
        data = self.polygon([(0, 0), (4, 0), (2, 0), (2, 3)])
        self.failed(data, "PlanarBoundaryIntersection")

    def test_hole_outside_or_touching_is_not_filled_or_reversed(self):
        outer = [(0, 0), (12, 0), (12, 8), (0, 8)]
        outside = [(20, 2), (20, 4), (24, 4), (24, 2)]
        touching = [(0, 2), (0, 4), (4, 4), (4, 2)]
        self.failed(self.polygon(outer, outside), "InnerBoundaryOutsideOuter")
        self.failed(self.polygon(outer, touching), "PlanarBoundaryIntersection")

    def two_holes(self, second):
        data = self.polygon([(0, 0), (12, 0), (12, 8), (0, 8)], [(2, 2), (2, 6), (8, 6), (8, 2)])
        index = by_id(inventory(data))
        copied = {*range(104, 108), *range(114, 118), 310, 311}
        copied |= {240 + 10*i + j for i in range(4) for j in range(5)}
        lines = []
        for i in sorted(copied):
            body = f"#{i}={index[i].raw};\n".encode()
            lines.append(re.sub(rb"#(\d+)", lambda m: f"#{int(m[1])+1000}".encode()
                                if int(m[1]) in copied else m[0], body))
        data = data.replace(b"ENDSEC;\nEND-ISO", b"".join(lines) + b"ENDSEC;\nEND-ISO")
        data = self.record(data, 302, "ADVANCED_FACE('',(#301,#311,#1311),#134,.T.)")
        for j, p in enumerate(second):
            q = second[(j+1) % 4]
            data = self.record(data, 1104+j, f"CARTESIAN_POINT('',({p[0]}.,{p[1]}.,0.))")
            data = self.record(data, 1240+10*j, f"DIRECTION('',({q[0]-p[0]}.,{q[1]-p[1]}.,0.))")
        return data

    def test_two_disjoint_holes_preserve_area(self):
        observed = self.checked(self.two_holes([(9, 2), (9, 3), (10, 3), (10, 2)]))
        self.assertEqual(exact(71**2), observed["faces"][0]["projected_region_area_squared_mm"])

    def test_nested_holes_do_not_silently_reinterpret_islands(self):
        self.failed(self.two_holes([(3, 3), (3, 4), (4, 4), (4, 3)]), "AmbiguousNestedInnerBoundary")

    def test_intersecting_holes_are_rejected_before_nesting(self):
        self.failed(self.two_holes([(7, 3), (7, 4), (9, 4), (9, 3)]), "PlanarBoundaryIntersection")

    def test_bad_hole_winding_keeps_positive_area_counterexample(self):
        observed = self.failed(self.input("S203-N25"), "InnerBoundaryOrientationMismatch")
        metric = [m for m in observed["metrics"] if m["kind"] == "bound_orientation"][-1]
        self.assertEqual(exact(8), metric["exact"]["signed_area_coefficient"])

    def test_closed_shell_boundary_and_surface_role_not_solid(self):
        observed = self.failed(self.input("S203-N26"), "ClaimedClosedShellHasBoundary")
        self.assertEqual(exact(4), observed["metrics"][-1]["exact"]["boundary_edges"])
        surface = self.checked(self.input("S203-P05"))
        self.assertEqual("ClosedSurfaceModel", surface["source_semantics"]["snapshot"]["part"]["source_shape_role"])
        self.assertEqual("NotRun", surface["checks"]["material_volume"])

    def test_closed_profile_special_names_remain_unsupported(self):
        for name in ("centre of mass", "treatment result"):
            data = self.record(self.p01, 20, f"MANIFOLD_SURFACE_SHAPE_REPRESENTATION('{name}',(#21),#12)")
            self.failed(data, "UnsupportedRepresentationPurpose", 2)
        self.failed(self.record(self.p01, 8, "PRODUCT_DEFINITION_SHAPE('hardness',$,#7)"), "UnsupportedShapePurpose", 2)

    def test_global_dimension_includes_indirect_line_direction(self):
        data = self.record(self.p01, 200, "DIRECTION('',(0.6,0.8))")
        observed = self.failed(data, "SourceRuleViolation:LINE.WR1")
        self.assertEqual("NotRun", observed["checks"]["global_rules_47"])

    def test_global_dimension_includes_indirect_vertex_point(self):
        data = self.record(self.p01, 110, "VERTEX_POINT('',#2000)")
        data = data.replace(b"ENDSEC;\nEND-ISO", b"#2000=CARTESIAN_POINT('',(3.,-2.));\nENDSEC;\nEND-ISO")
        observed = self.failed(data, "SourceRuleViolation:compatible_dimension.WR1")
        self.assertEqual("Passed", observed["checks"]["local_rules"])

    def test_work_limit_exact_and_n_minus_one(self):
        work = self.checked(self.p01)["work"]
        self.checked(self.p01, "--max-planar-work", work)
        self.failed(self.p01, "PlanarWorkLimit", 3, "--max-planar-work", work-1)

    def test_many_auxiliary_placements_keep_evidence_within_report_budget(self):
        identities = list(range(1000, 1700))
        items = ",".join(f"#{i}" for i in [21, *identities])
        data = self.record(self.p01, 20, f"MANIFOLD_SURFACE_SHAPE_REPRESENTATION('',({items}),#12)")
        extra = "".join(f"#{i}=AXIS2_PLACEMENT_3D('',#130,#131,#132);\n" for i in identities).encode()
        data = data.replace(b"ENDSEC;\nEND-ISO", extra + b"ENDSEC;\nEND-ISO")
        observed = self.checked(data, "--max-report-bytes", 8000000, "--max-planar-work", 50000)
        population = observed["source_semantics"]["snapshot"]["type_populations"]["AXIS2_PLACEMENT_3D"]
        self.assertEqual(701, len(population))
        self.assertEqual(701, sum(r["name"] == "AXIS2_PLACEMENT_3D.WR4" for r in observed["rules"]))

    def test_report_byte_limit_exact_and_n_minus_one(self):
        result, _ = self.invoke(self.p01)
        result, _ = self.invoke(self.p01, "--max-report-bytes", len(result.stdout))
        budget = len(result.stdout)
        result, _ = self.invoke(self.p01, "--max-report-bytes", budget)
        self.assertEqual((0, budget), (result.returncode, len(result.stdout)))
        result, _ = self.invoke(self.p01, "--max-report-bytes", budget-1)
        self.assertEqual((5, b""), (result.returncode, result.stdout))

    def test_quality_is_explicit_and_does_not_select_production_defaults(self):
        path = self.root / "quality.step"
        path.write_bytes(self.p01)
        result = subprocess.run([str(self.binary), "--fixture-limits", str(path)], capture_output=True, timeout=10)
        self.assertEqual((64, b""), (result.returncode, result.stdout))

    def test_deferred_conversion_never_enters_rules_or_geometry(self):
        for key in ("S203-P11", "S203-P13", "S203-N31", "S203-N32", "S203-N33", "S203-N34"):
            result, observed = self.invoke(self.input(key))
            self.assertEqual(7, result.returncode)
            self.assertEqual([], observed["rules"])
            self.assertEqual([], observed["faces"])

    def test_checker_tampering_controls(self):
        cases = {c["case_id"]: c for c in self.cases}
        for key, change in [
            ("S203-P01", lambda d: d["rules"].pop()),
            ("S203-P01", lambda d: self.rule(d, "restrict_representation_for_surface_condition")["facts"].update(
                correlation_UNKNOWN_count="0")),
            ("S203-P01", lambda d: self.rule(d, "validate_dependently_instantiable_entity_data_types")[
                "populations"].update(LOOP=[])),
            ("S203-P01", lambda d: d["faces"][0].update(projected_region_area_squared_mm=exact(1))),
            ("S203-P01", lambda d: d["quality"].update(consistency_mm=exact(1))),
            ("S203-N20", lambda d: d["metrics"][0]["exact"].update(start_distance_squared_mm=exact(0))),
            ("S203-P01", lambda d: d["checks"].update(native_build="Passed")),
        ]:
            with self.subTest(case=key, change=change):
                data = self.input(key)
                result, observed = self.invoke(data)
                changed = deepcopy(observed)
                change(changed)
                ledger = load_json(CORPUS / cases[key]["identity"]["materials"]["inventory"]["path"])
                with self.assertRaises(InvalidEvidence):
                    compare(cases[key], changed, result.returncode, data, ledger, reference())

    def test_real_evidence_publication_and_recheck(self):
        run = runner.run(self.binary, CORPUS, self.root / "evidence", ["S203-P01", "S203-N20", "S203-N02"])
        self.assertEqual(3, runner.check(run)["scope_contract_matched"])
        self.assertTrue((run / "completion.json").is_file())
        self.assertTrue((run / "cases/S203-P01/truth.json").is_file())


if __name__ == "__main__":
    unittest.main()
