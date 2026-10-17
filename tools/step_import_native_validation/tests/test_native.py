# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
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

from tools.step_import_validation.common import load_json
from tools.step_import_validation.corpus import verify
from tools.step_import_validation.oracle import decode, exact
from tools.step_import_validation.records import inventory, by_id, ref, vector as source_vector
from tools.step_import_native_validation.oracle import accepted, corpus_case, vector

CORPUS = Path(__file__).resolve().parents[2] / "step_import_validation/corpus/ap203e2-planar/v1"


class NativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary = Path(os.environ["STEP_NATIVE_BINARY"]).resolve(strict=True)
        cls.temp = tempfile.TemporaryDirectory(prefix="step-ticket05-")
        cls.root = Path(cls.temp.name)
        cls.cases = verify(CORPUS)["cases"]
        for case in cls.cases:
            case["truth"] = load_json(CORPUS / case["identity"]["materials"]["truth"]["path"])
        cls.p01 = (CORPUS / "cases/S203-P01/input.step").read_bytes()
        cls.truth = next(c["truth"] for c in cls.cases if c["case_id"] == "S203-P01")

    @classmethod
    def tearDownClass(cls):
        assert cls.root.parent.resolve() == Path(tempfile.gettempdir()).resolve()
        cls.temp.cleanup()

    def invoke(self, data, *options):
        path = self.root / "probe.step"
        path.write_bytes(data)
        result = subprocess.run([str(self.binary), "--fixture-limits", "--quality", "Q-P0-mm-01", str(path),
                                 *map(str, options)], capture_output=True, timeout=30)
        return result, json.loads(result.stdout) if result.stdout else None

    def good(self, data, **kwargs):
        result, observed = self.invoke(data)
        self.assertEqual(0, result.returncode, result.stderr or result.stdout)
        accepted(observed, data, self.truth, **kwargs)
        return observed

    def record(self, data, identity, body):
        result, count = re.subn(rb"(?m)^#" + str(identity).encode() + rb"=.*;$",
                                lambda _: f"#{identity}={body};".encode(), data)
        self.assertEqual(1, count)
        return result

    def test_49_frozen_cases_no_silent_partial_native_import(self):
        counts = Counter()
        for case in self.cases:
            with self.subTest(case=case["case_id"]):
                data = (CORPUS / case["identity"]["materials"]["input"]["path"]).read_bytes()
                actual, observed = self.invoke(data)
                corpus_case(case, observed, actual.returncode, data)
                counts[observed["status"]] += 1
        self.assertEqual(Counter(Accepted=13, Rejected=29, Unsupported=7), counts)

    def box(self):
        return (CORPUS / "cases/S203-P04/input.step").read_bytes(), next(
            c["truth"] for c in self.cases if c["case_id"] == "S203-P04")

    def test_optional_branches_deliver_the_same_f1_queries(self):
        for key in ("P08", "P09", "P10", "P12"):
            with self.subTest(case=key):
                data = (CORPUS / f"cases/S203-{key}/input.step").read_bytes()
                actual, observed = self.invoke(data, "--query", 4.2, 9.6, 8)
                self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
                truth = next(c["truth"] for c in self.cases if c["case_id"] == f"S203-{key}")
                accepted(observed["import"], data, truth)
                vector(observed["query"]["closest_point_mm"], (F(21, 5), F(48, 5), F(5)),
                       F(1, 10**7), "optional declaration geometry")

    def test_optional_declarations_retain_source_values_types_and_warnings(self):
        for key in ("P01", "P08", "P09", "P10", "P12"):
            with self.subTest(case=key):
                data = (CORPUS / f"cases/S203-{key}/input.step").read_bytes()
                actual, observed = self.invoke(data)
                self.assertEqual(0, actual.returncode)
                declarations = observed["source_declarations"]
                self.assertEqual("RecordAndWarnOnly", declarations["uncertainty_policy"])
                records = {r["id"]: r for r in declarations["records"]}
                for identity in ("3", "5", "6", "10", "11", "12"):
                    self.assertIn(identity, records)
                if key == "P08":
                    self.assertEqual([F(1, 10000), F(1, 5000)],
                                     [decode(u["value_mm"]) for u in declarations["uncertainties"]])
                    self.assertTrue(all(u["consistency_comparison"] == "Exceeds" and
                                        u["warning"] == "SourceUncertaintyExceedsConsistencyBudget"
                                        for u in declarations["uncertainties"]))
                else:
                    self.assertEqual([], declarations["uncertainties"])
                if key == "P10":
                    self.assertIn("MECHANICAL_CONTEXT", records["3"]["types"])
                    self.assertIn("DESIGN_CONTEXT", records["6"]["types"])
                    self.assertIn("PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE", records["5"]["types"])
                    self.assertIn(".NOT_KNOWN.", records["5"]["raw"])
                if key == "P12":
                    self.assertEqual("16", declarations["units"]["solid_angle_unit"])
                    self.assertIn("PLANE_ANGLE_UNIT", records["11"]["types"])
                    self.assertNotIn("SOLID_ANGLE_UNIT", records["11"]["types"])
                    self.assertIn("SOLID_ANGLE_UNIT", records["16"]["types"])

    def test_uncertainty_uses_its_own_unit_and_only_warns(self):
        original = (CORPUS / "cases/S203-P08/input.step").read_bytes()
        for prefix, value, expected in (("CENTI", "0.00001", F(1, 10000)),
                                         ("MILLI", "0.00000001", F(1, 10**8)), ("EXA", "1.", None)):
            with self.subTest(prefix=prefix):
                data = original.replace(b"ENDSEC;\nEND-ISO", (
                    f"#90=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.{prefix}.,.METRE.));\nENDSEC;\nEND-ISO").encode())
                data = self.record(data, 13, f"UNCERTAINTY_MEASURE_WITH_UNIT("
                                   f"LENGTH_MEASURE({value}),#90,'distance_accuracy_A',$)")
                observed = self.good(data)
                uncertainty = observed["source_declarations"]["uncertainties"][0]
                self.assertEqual("90", uncertainty["unit"])
                if expected is None:
                    self.assertIsNone(uncertainty["value_mm"])
                    self.assertEqual("Unestablished", uncertainty["consistency_comparison"])
                else:
                    self.assertEqual(expected, decode(uncertainty["value_mm"]))
                self.assertTrue(all(t["value_mm"] == 1e-5 for t in observed["tolerances"]))
                self.assertEqual([], observed["diagnostics"])
        displaced = self.record(original, 101, "CARTESIAN_POINT('',(10.2,7.6,5.00001))")
        actual, observed = self.invoke(displaced)
        self.assertEqual(1, actual.returncode)
        self.assertEqual("Rejected", observed["source"]["planar"])
        self.assertEqual("D4", observed["diagnostics"][0]["stage"])
        self.assertEqual(0, observed["native_calls"])
        self.assertIsNone(observed["model"])

    def test_legacy_subtypes_keep_constraints_and_source_enumerations(self):
        original = (CORPUS / "cases/S203-P10/input.step").read_bytes()
        for source in ("MADE", "BOUGHT", "NOT_KNOWN"):
            observed = self.good(original.replace(b".NOT_KNOWN.", f".{source}.".encode()))
            record = next(r for r in observed["source_declarations"]["records"] if r["id"] == "5")
            self.assertIn(f".{source}.", record["raw"])
        for data, key, stage in (
            (self.record(original, 3, "MECHANICAL_CONTEXT('',#1,'electrical')"),
             "SourceRuleViolation:MECHANICAL_CONTEXT.WR1", "D3"),
            (self.record(original, 6, "DESIGN_CONTEXT('part definition',#1,'analysis')"),
             "SourceRuleViolation:DESIGN_CONTEXT.WR1", "D3"),
            (original.replace(b".NOT_KNOWN.", b".UNKNOWN."), "EnumerationValueInvalid", "D1"),
        ):
            actual, observed = self.invoke(data)
            self.assertEqual(1, actual.returncode)
            self.assertEqual((key, stage), (observed["diagnostics"][0]["key"], observed["diagnostics"][0]["stage"]))
            self.assertIsNone(observed["model"])
            self.assertEqual(0, observed["native_calls"])

    def test_new_metadata_relationship_is_not_silently_dropped(self):
        original = (CORPUS / "cases/S203-P09/input.step").read_bytes()
        data = original.replace(b"ENDSEC;\nEND-ISO", (
            "#91=PRODUCT_CATEGORY_RELATIONSHIP('new relationship',$,#15,#15);\nENDSEC;\nEND-ISO").encode())
        actual, observed = self.invoke(data)
        self.assertEqual(2, actual.returncode)
        self.assertEqual(("UnsupportedEntityType", "D2"),
                         (observed["diagnostics"][0]["key"], observed["diagnostics"][0]["stage"]))
        self.assertIsNone(observed["model"])
        self.assertEqual(0, observed["native_calls"])

    def test_optional_declarations_survive_record_order_and_sparse_ids(self):
        for case in ("P08", "P09", "P10", "P12"):
            original = (CORPUS / f"cases/S203-{case}/input.step").read_bytes()
            header, body = original.split(b"\nDATA;\n")
            body, end = body.split(b"ENDSEC;\nEND-ISO")
            data = (header + b"\nDATA;\n" + b"".join(reversed(body.splitlines(keepends=True))) +
                    b"ENDSEC;\nEND-ISO" + end)
            data = re.sub(rb"#([0-9]+)", lambda m: b"#" + str(int(m[1]) * 37 + 9000).encode(), data)
            self.good(data)

    def test_declaration_checker_rejects_loss_or_reinterpretation(self):
        for case in ("P08", "P10", "P12"):
            data = (CORPUS / f"cases/S203-{case}/input.step").read_bytes()
            good = self.good(data)
            corruptions = []
            bad = deepcopy(good)
            bad["source_declarations"]["records"].pop()
            corruptions.append(bad)
            bad = deepcopy(good)
            bad["source_declarations"]["records"][0]["raw"] += " "
            corruptions.append(bad)
            bad = deepcopy(good)
            bad["source_declarations"]["records"][0]["types"] = []
            corruptions.append(bad)
            if case == "P08":
                for field, value in (("value_mm", exact(F(1, 1000))), ("unit", "11"),
                                     ("consistency_comparison", "Within"), ("warning", None)):
                    bad = deepcopy(good)
                    bad["source_declarations"]["uncertainties"][0][field] = value
                    corruptions.append(bad)
                bad = deepcopy(good)
                bad["source_declarations"]["uncertainties"].pop()
                corruptions.append(bad)
            if case == "P12":
                bad = deepcopy(good)
                bad["source_declarations"]["units"]["solid_angle_unit"] = "11"
                corruptions.append(bad)
            for bad in corruptions:
                with self.assertRaises(AssertionError):
                    accepted(bad, data, self.truth)

    def test_unit_scale_and_parameter_maps_are_explicit(self):
        for case, scale, unit in (("P06", F(10), "10"), ("P07", F(10), "10"), ("P11", F(127, 5), "19")):
            data = (CORPUS / f"cases/S203-{case}/input.step").read_bytes()
            actual, observed = self.invoke(data)
            self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
            mapping = observed["unit_normalization"]
            self.assertEqual(unit, mapping["source_length_unit"])
            self.assertEqual(scale, decode(mapping["length_to_mm"]))
            self.assertEqual(scale, decode(mapping["source_plane_uv_to_native_scale"]))
            self.assertEqual("DimensionlessUnchanged", mapping["line_parameter"])
            for edge in observed["edge_mappings"]:
                self.assertEqual((F(0), F(1)), (decode(edge["source_start_coefficient"]),
                                              decode(edge["source_end_coefficient"])))

    def test_closed_surface_matches_boundary_without_material_qualification(self):
        data = (CORPUS / "cases/S203-P05/input.step").read_bytes()
        actual, observed = self.invoke(data, "--query", 7, 7, 20)
        self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
        imported = observed["import"]
        truth = next(c["truth"] for c in self.cases if c["case_id"] == "S203-P05")
        accepted(imported, data, truth)
        self.assertEqual(dict(role="ClosedSurfaceModel", representation_id="20",
                              representation_type="MANIFOLD_SURFACE_SHAPE_REPRESENTATION", shape_id="21",
                              shape_type="SHELL_BASED_SURFACE_MODEL", shell_id="22", shell_type="CLOSED_SHELL"),
                         imported["source_shape"])
        self.assertEqual(dict(regions=2, shells=2, material_regions=0, bounded_void_regions=1,
                              manifold_solid_predicate=True), imported["native_role"])
        self.assertEqual("NotApplicable", imported["native_measurements"]["material_volume"])
        self.assertEqual("NotApplicable", observed["query"]["material_location"])
        for field in ("volume_bound_mm3", "volume_junction_bound_mm", "volume_gap_flux_bound_mm3",
                      "volume_skipped_flux_bound_mm3"):
            self.assertIsNone(imported[field])

    def test_unit_queries_and_quality_remain_canonical(self):
        for key in ("S203-P06", "S203-P07", "S203-P11"):
            data = (CORPUS / f"cases/{key}/input.step").read_bytes()
            truth = next(c["truth"] for c in self.cases if c["case_id"] == key)
            query = truth["queries"]["off_face"]
            actual, observed = self.invoke(data, "--query", *map(float, map(decode, query["point_mm"])))
            self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
            scale = decode(truth["source_definition"]["source_length_scale_mm"])
            window = truth["source_definition"].get("source_uv_window")
            bounds = tuple(decode(v) * scale for p in window for v in p) if window else (0, 0, 12, 8)
            accepted(observed["import"], data, truth, uv_bounds=bounds)
            vector(observed["query"]["closest_point_mm"], list(map(decode, query["closest_point_mm"])),
                   F(1, 10**7), "unit-normalized boundary query")
            self.assertLessEqual(abs(F(observed["query"]["distance_mm"]) - 3), F(1, 10**7))

    def test_conversion_uses_factor_values_and_dimension_values_instead_of_names_or_identity(self):
        original = (CORPUS / "cases/S203-P11/input.step").read_bytes()
        truth = next(c["truth"] for c in self.cases if c["case_id"] == "S203-P11")
        renamed = original.replace(b"'inch'", b"'not a unit table: SI_UNIT(fake)'" )
        self.assertNotEqual(renamed, original)
        actual, observed = self.invoke(renamed)
        self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
        accepted(observed, renamed, truth)
        # Keep the name 'inch', but deliberately define 2mm per source unit.
        changed = self.record(original, 18, "LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(2.),#10)")
        small = deepcopy(truth)
        small["measures"] = dict(area=exact(88), perimeter=exact(48), volume=exact(48))
        small["centroid"] = list(map(exact, (1, 2, 3)))
        small["bounds"] = [list(map(exact, p)) for p in ((0, 0, 0), (2, 4, 6))]
        actual, observed = self.invoke(changed)
        self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
        accepted(observed, changed, small)
        # Two conversion nodes use different dimensions identities with the same seven values.
        chain = self.record(original, 18, "LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#29)")
        rows = ("#28=MEASURE_WITH_UNIT(LENGTH_MEASURE(1.),#10);\n"
                "#29=(CONVERSION_BASED_UNIT('anything',#28) LENGTH_UNIT() NAMED_UNIT(#37));\n"
                "#37=DIMENSIONAL_EXPONENTS(1.,0.,0.,0.,0.,0.,0.);\n")
        chain = chain.replace(b"ENDSEC;\nEND-ISO", rows.encode() + b"ENDSEC;\nEND-ISO")
        actual, observed = self.invoke(chain)
        self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
        accepted(observed, chain, truth)
        self.assertEqual(2, len(observed["unit_normalization"]["definitions"]))

    def test_nondefault_line_parameters_survive_centimetre_units(self):
        original = (CORPUS / "cases/S203-P07/input.step").read_bytes()
        truth = next(c["truth"] for c in self.cases if c["case_id"] == "S203-P07")
        nonunit = self.record(original, 200, "DIRECTION('',(3.,4.,0.))")
        actual, observed = self.invoke(nonunit)
        self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
        accepted(observed, nonunit, truth)
        edge = next(e for e in observed["edge_mappings"] if e["source_edge"] == "203")
        self.assertEqual((F(5), F(25)), (decode(edge["source_end_coefficient"]),
                                       decode(edge["source_direction_norm_squared"])))
        shifted = self.record(original, 202, "LINE('',#101,#201)")
        actual, observed = self.invoke(shifted)
        self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
        accepted(observed, shifted, truth)
        edge = next(e for e in observed["edge_mappings"] if e["source_edge"] == "203")
        self.assertEqual((F(-1), F(0)), (decode(edge["source_start_coefficient"]),
                                       decode(edge["source_end_coefficient"])))

    def test_unit_checker_rejects_wrong_physical_and_parameter_metadata(self):
        data = (CORPUS / "cases/S203-P11/input.step").read_bytes()
        truth = next(c["truth"] for c in self.cases if c["case_id"] == "S203-P11")
        actual, original = self.invoke(data)
        self.assertEqual(0, actual.returncode)
        for defect in ("scale", "uv", "t", "unit", "factor", "dimensions", "missing", "budget_unit"):
            observed = deepcopy(original)
            mapping = observed["unit_normalization"]
            if defect == "scale":
                mapping["length_to_mm"] = exact(1)
            if defect == "uv":
                mapping["source_plane_uv_to_native_scale"] = exact(1)
            if defect == "t":
                observed["edge_mappings"][0]["source_end_coefficient"] = exact(F(127, 5))
            if defect == "unit":
                mapping["source_length_unit"] = "10"
            if defect == "factor":
                mapping["definitions"][0]["factor_value"] = exact(1)
            if defect == "dimensions":
                mapping["definitions"][0]["dimensions"][0] = exact(0)
            if defect == "missing":
                mapping["definitions"].clear()
            if defect == "budget_unit":
                mapping["canonical_length"] = "inch"
            with self.subTest(defect=defect), self.assertRaises(AssertionError):
                accepted(observed, data, truth)

    def test_material_box_independent_queries_and_classifications(self):
        data, truth = self.box()
        for name in ("off_face", "corner"):
            query = truth["queries"][name]
            actual, observed = self.invoke(data, "--query", *map(float, map(decode, query["point_mm"])))
            self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
            accepted(observed["import"], data, truth)
            vector(observed["query"]["closest_point_mm"], list(map(decode, query["closest_point_mm"])),
                   F(1, 10**7), "whole-box independent closest")
            distance = F(observed["query"]["distance_mm"])
            self.assertLessEqual(decode(query["distance_mm"]["lower"]) - F(1, 10**7), distance)
            self.assertLessEqual(distance, decode(query["distance_mm"]["upper"]) + F(1, 10**7))
            self.assertEqual("Outside", observed["query"]["material_location"])
        for query in truth["queries"]["classifications"]:
            actual, observed = self.invoke(data, "--query", *map(float, map(decode, query["point_mm"])))
            self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
            self.assertEqual(query["material_location"], observed["query"]["material_location"])

    def test_surface_and_material_share_independent_boundary_queries(self):
        solid, solid_truth = self.box()
        surface = (CORPUS / "cases/S203-P05/input.step").read_bytes()
        surface_truth = next(c["truth"] for c in self.cases if c["case_id"] == "S203-P05")
        observations = []
        for data, truth in ((solid, solid_truth), (surface, surface_truth)):
            actual, observed = self.invoke(data)
            self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
            accepted(observed, data, truth)
            observations.append(observed)
        a, b = observations
        self.assertEqual(a["edge_mappings"], b["edge_mappings"])
        self.assertEqual(a["edge_adjacency"], b["edge_adjacency"])
        self.assertEqual(a["loop_mappings"], b["loop_mappings"])
        self.assertEqual(a["face_mappings"], b["face_mappings"])
        self.assertNotEqual(a["source"]["sha256"], b["source"]["sha256"])
        for name in ("off_face", "corner"):
            query = surface_truth["queries"][name]
            actual, observed = self.invoke(surface, "--query", *map(float, map(decode, query["point_mm"])))
            self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
            vector(observed["query"]["closest_point_mm"], list(map(decode, query["closest_point_mm"])),
                   F(1, 10**7), "closed surface boundary closest")
            d = F(observed["query"]["distance_mm"])
            self.assertLessEqual(decode(query["distance_mm"]["lower"]) - F(1, 10**7), d)
            self.assertLessEqual(d, decode(query["distance_mm"]["upper"]) + F(1, 10**7))
            self.assertEqual("NotApplicable", observed["query"]["material_location"])
        for query in surface_truth["queries"]["classifications"]:
            actual, observed = self.invoke(surface, "--query", *map(float, map(decode, query["point_mm"])))
            self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
            self.assertEqual("NotApplicable", observed["query"]["material_location"])

    def test_closed_surface_preserves_role_through_geometry_and_id_changes(self):
        original = (CORPUS / "cases/S203-P05/input.step").read_bytes()
        truth = next(c["truth"] for c in self.cases if c["case_id"] == "S203-P05")
        reverse = self.record(original, 22, "CLOSED_SHELL('',(#557,#547,#537,#527,#517,#507))")
        lines = reverse.splitlines(keepends=True)
        rows = [line for line in lines if line.startswith(b"#")]
        for i, row in zip([i for i, line in enumerate(lines) if line.startswith(b"#")], reversed(rows)):
            lines[i] = row
        sparse = re.sub(rb"#([0-9]+)", lambda m: b"#" + str(int(m[1]) + 100000).encode(), original)
        nondefault = self.record(original, 201, "VECTOR('',#200,5.)")
        for data in (b"".join(lines), sparse, nondefault):
            actual, observed = self.invoke(data)
            self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
            accepted(observed, data, truth)
        for tilt in (False, True):
            data, transformed = self.transformed_box(tilt=tilt)
            data = self.record(data, 20, "MANIFOLD_SURFACE_SHAPE_REPRESENTATION('body',(#21),#12)")
            data = self.record(data, 21, "SHELL_BASED_SURFACE_MODEL('',(#22))")
            expected = deepcopy(truth)
            expected["centroid"], expected["bounds"] = transformed["centroid"], transformed["bounds"]
            actual, observed = self.invoke(data)
            self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
            accepted(observed, data, expected)

    def test_surface_checker_rejects_materialization_and_lost_source_identity(self):
        data = (CORPUS / "cases/S203-P05/input.step").read_bytes()
        truth = next(c["truth"] for c in self.cases if c["case_id"] == "S203-P05")
        actual, original = self.invoke(data)
        self.assertEqual(0, actual.returncode)
        for defect in ("model_role", "source_role", "representation", "shape", "shell", "material_region",
                       "bounded_void", "zero_volume", "fake_census", "certificate", "policy"):
            observed = deepcopy(original)
            if defect == "model_role":
                observed["model"]["role"] = "MaterialSolid"
            if defect == "source_role":
                observed["source_shape"]["role"] = "MaterialSolid"
            if defect == "representation":
                observed["source_shape"]["representation_type"] = "ADVANCED_BREP_SHAPE_REPRESENTATION"
            if defect == "shape":
                observed["source_shape"]["shape_type"] = "MANIFOLD_SOLID_BREP"
            if defect == "shell":
                observed["source_shape"]["shell_id"] = "21"
            if defect == "material_region":
                observed["native_role"]["material_regions"] = 1
            if defect == "bounded_void":
                observed["native_role"]["bounded_void_regions"] = 0
            if defect == "zero_volume":
                observed["native_measurements"]["material_volume"] = dict(applicability="Applicable", value_mm3=0)
            if defect == "fake_census":
                observed["material_census"] = dict(solid_regions=0, void_cavities=0)
            if defect == "certificate":
                observed["volume_bound_mm3"] = 0
            if defect == "policy":
                observed["volume_evidence_policy"] = "ActualOrientedFiniteSegmentsZGaugeGapAndTinyDuBounds"
            with self.subTest(defect=defect), self.assertRaises(AssertionError):
                accepted(observed, data, truth)

    def test_shared_box_survives_orders_sparse_ids_and_edge_senses(self):
        original, truth = self.box()
        for reverse_curve, swap_edge, reverse_faces in itertools.product([False, True], repeat=3):
            with self.subTest(flags=(reverse_curve, swap_edge, reverse_faces)):
                data = original
                if reverse_curve:
                    data = self.record(data, 200, "DIRECTION('',(-1.,0.,0.))")
                sense = ".F." if reverse_curve ^ swap_edge else ".T."
                data = self.record(data, 203, f"EDGE_CURVE('',#{111 if swap_edge else 110},"
                                             f"#{110 if swap_edge else 111},#202,{sense})")
                index = by_id(inventory(data))
                for row in index.values():
                    if row.kind == "ORIENTED_EDGE" and ref(row.args[3]) == 203 and swap_edge:
                        backward = row.args[4] == ".T."
                        data = self.record(data, row.entity_id,
                                           f"ORIENTED_EDGE('',*,*,#203,{'.F.' if backward else '.T.'})")
                if reverse_faces:
                    data = self.record(data, 22, "CLOSED_SHELL('',(#557,#547,#537,#527,#517,#507))")
                    lines = data.splitlines(keepends=True)
                    rows = [line for line in lines if line.startswith(b"#")]
                    for i, row in zip([i for i, line in enumerate(lines) if line.startswith(b"#")], reversed(rows)):
                        lines[i] = row
                    data = b"".join(lines)
                actual, observed = self.invoke(data)
                self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
                accepted(observed, data, truth)
        sparse = re.sub(rb"#([0-9]+)", lambda m: b"#" + str(int(m[1]) + 100000).encode(), original)
        actual, observed = self.invoke(sparse)
        self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
        accepted(observed, sparse, truth)

    def transformed_box(self, shear=False, tilt=False):
        data, truth = self.box()
        rows = inventory(data)
        axes = {ref(row.args[2]) for row in rows if row.kind == "AXIS2_PLACEMENT_3D" and row.args[2] != "$"}
        translation = (F(0), F(0), F(0)) if shear else (F(-17, 5), F(8, 5), F(-9))
        def transform(point, normal=False):
            x, y, z = point
            if shear:
                return (x, y - x / 5, z) if normal else (x + y / 5, y, z)
            if tilt:
                # Exact proper rotation Rx*Rz*Ry; all three axes leave the coordinate planes.
                return (F(9, 25)*x - F(4, 5)*y + F(12, 25)*z,
                        F(116, 125)*x + F(9, 25)*y - F(12, 125)*z,
                        -F(12, 125)*x + F(12, 25)*y + F(109, 125)*z)
            return (F(3, 5)*x - F(4, 5)*y, F(4, 5)*x + F(3, 5)*y, z)
        def text(value):
            scale = 0
            while 10**scale % value.denominator:
                scale += 1
                self.assertLess(scale, 15)
            if not scale:
                return str(value.numerator) + "."
            integer = value.numerator * 10**scale // value.denominator
            digits = str(abs(integer)).rjust(scale + 1, "0")
            return ("-" if integer < 0 else "") + digits[:-scale] + "." + digits[-scale:]
        for row in rows:
            if row.kind not in {"CARTESIAN_POINT", "DIRECTION"}:
                continue
            values = transform(source_vector(row.args[1]), row.kind == "DIRECTION" and row.entity_id in axes)
            if row.kind == "CARTESIAN_POINT":
                values = tuple(a + b for a, b in zip(values, translation))
            data = self.record(data, row.entity_id, row.kind + "('',(" + ",".join(map(text, values)) + "))")
        if not shear:
            truth = deepcopy(truth)
            truth["centroid"] = [exact(a + b) for a, b in zip(
                transform(tuple(map(decode, truth["centroid"]))), translation)]
            points = [tuple(a + b for a, b in zip(transform(tuple(map(decode, p))), translation))
                      for p in truth["source_definition"]["world_points_mm"]]
            truth["bounds"] = [[exact(min(p[i] for p in points)) for i in range(3)],
                               [exact(max(p[i] for p in points)) for i in range(3)]]
        return data, truth

    def test_rigidly_transformed_material_box_keeps_geometry_and_role(self):
        for tilt in (False, True):
            with self.subTest(tilt=tilt):
                data, truth = self.transformed_box(tilt=tilt)
                actual, observed = self.invoke(data)
                self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
                accepted(observed, data, truth)
                self.assertGreater(observed["volume_gap_flux_bound_mm3"], 0)
                self.assertGreater(observed["volume_skipped_flux_bound_mm3"], 0)

    def test_box_scope_rejects_skew_without_materializing_surface(self):
        data, _ = self.transformed_box(shear=True)
        actual, observed = self.invoke(data)
        self.assertEqual(2, actual.returncode, actual.stderr or actual.stdout)
        self.assertEqual("Checked", observed["source"]["planar"])
        self.assertEqual("UnsupportedNativeMaterialBoxScope", observed["diagnostics"][0]["key"])
        self.assertEqual(0, observed["native_calls"])
        self.assertIsNone(observed["model"])
        self.assertIsNone(observed["native_measurements"])
        actual, observed = self.invoke((CORPUS / "cases/S203-P05/input.step").read_bytes())
        self.assertEqual(0, actual.returncode)
        self.assertEqual("ClosedSurfaceModel", observed["model"]["role"])
        self.assertEqual(dict(solid_regions=None, void_cavities=None), observed["material_census"])

    def test_box_checker_rejects_false_material_adjacency_volume_and_face_data(self):
        data, truth = self.box()
        actual, original = self.invoke(data)
        self.assertEqual(0, actual.returncode)
        for defect in ("volume", "flux", "junction", "gap_flux", "skipped_flux", "role", "region", "adjacency",
                       "uv_owner", "plane", "use", "normal", "parameter"):
            observed = deepcopy(original)
            if defect == "volume":
                observed["native_measurements"]["material_volume"]["value_mm3"] += 1
            if defect == "flux":
                observed["volume_bound_mm3"] = None
            if defect == "junction":
                observed["volume_junction_bound_mm"] = 2e-7
            if defect == "gap_flux":
                observed["volume_gap_flux_bound_mm3"] = None
            if defect == "skipped_flux":
                observed["volume_skipped_flux_bound_mm3"] = 2e-5
            if defect == "role":
                observed["model"]["role"] = "OpenSurfaceModel"
            if defect == "region":
                observed["material_census"]["solid_regions"] = 0
            if defect == "adjacency":
                row = observed["edge_adjacency"][0]
                row["source_faces"][1] = row["source_faces"][0]
            if defect == "uv_owner":
                observed["loop_mappings"][1]["uses"][0]["uv_owner_edgeuse"] = "OPPOSITE"
            if defect == "plane":
                observed["face_mappings"][1]["source_plane"] = observed["face_mappings"][0]["source_plane"]
            if defect == "use":
                observed["loop_mappings"][1]["uses"].pop()
            if defect == "normal":
                observed["native_measurements"]["normal"] = [0, 0, -1]
            if defect == "parameter":
                observed["edge_mappings"][0]["source_start_coefficient"] = exact(7)
            with self.subTest(defect=defect), self.assertRaises(AssertionError):
                accepted(observed, data, truth)

    def test_frozen_independent_closest_point(self):
        truth = self.truth["queries"]["off_face"]
        query = list(map(decode, truth["point_mm"]))
        actual, observed = self.invoke(self.p01, "--query", *map(float, query))
        self.assertEqual(0, actual.returncode, actual.stderr)
        accepted(observed["import"], self.p01, self.truth)
        vector(observed["query"]["closest_point_mm"], list(map(decode, truth["closest_point_mm"])),
               F(1, 10**7), "independent closest point")
        self.assertLessEqual(abs(F(observed["query"]["distance_mm"]) - 3), F(1, 10**7))

    def test_hole_loop_and_use_uv_provenance_is_retained(self):
        data = (CORPUS / "cases/S203-P02/input.step").read_bytes()
        result, observed = self.invoke(data)
        self.assertEqual(0, result.returncode, result.stderr or result.stdout)
        loops = observed["loop_mappings"]
        self.assertEqual([("301", "300", True), ("311", "310", False)],
                         [(r["source_bound"], r["source_loop"], r["outer"]) for r in loops])
        self.assertEqual(8, sum(len(row["uses"]) for row in loops))

    def test_perforated_plane_queries_and_negative_uv_world_invariance(self):
        observations = []
        for case_id in ("S203-P02", "S203-P03"):
            with self.subTest(case=case_id):
                truth = next(c["truth"] for c in self.cases if c["case_id"] == case_id)
                data = (CORPUS / f"cases/{case_id}/input.step").read_bytes()
                query = truth["queries"]["hole"]
                actual, observed = self.invoke(data, "--query", *map(float, map(decode, query["point_mm"])))
                self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
                window = (-6, -4, 6, 4) if case_id == "S203-P03" else (0, 0, 12, 8)
                accepted(observed["import"], data, truth, uv_bounds=window)
                vector(observed["query"]["closest_point_mm"], list(map(decode, query["closest_point_mm"])),
                       F(1, 10**7), "closest point above hole")
                distance = F(observed["query"]["distance_mm"])
                self.assertLessEqual(decode(query["distance_mm"]["lower"]) - F(1, 10**7), distance)
                self.assertLessEqual(distance, decode(query["distance_mm"]["upper"]) + F(1, 10**7))
                observations.append(observed)
        first, second = [o["import"]["native_measurements"] for o in observations]
        for field, budget in (("area_mm2", F(1, 10**6)), ("boundary_mm", F(1, 10**7))):
            self.assertLessEqual(abs(F(first[field]) - F(second[field])), budget)
        for field in ("centroid_mm", "normal"):
            vector(first[field], list(map(F, second[field])), F(1, 10**7), "same world " + field)
        for a, b in zip(first["bounds_mm"], second["bounds_mm"]):
            vector(a, list(map(F, b)), F(1, 10**7), "same world bounds")
        vector(observations[0]["query"]["closest_point_mm"],
               list(map(F, observations[1]["query"]["closest_point_mm"])), F(1, 10**7), "same world closest point")

    def test_hole_directions_and_bound_set_order_preserve_source_identity(self):
        for shifted in (False, True):
            case_id = "S203-P03" if shifted else "S203-P02"
            original = (CORPUS / f"cases/{case_id}/input.step").read_bytes()
            truth = next(c["truth"] for c in self.cases if c["case_id"] == case_id)
            for reverse_curve, swap_edge, reverse_bound, reverse_face in itertools.product([False, True], repeat=4):
                with self.subTest(shifted=shifted, flags=(reverse_curve, swap_edge, reverse_bound, reverse_face)):
                    # Inner bound deliberately precedes outer in the source SET.
                    data = self.record(original, 302, "ADVANCED_FACE('',(#311,#301),#134,.T.)")
                    if reverse_curve:
                        data = self.record(data, 240, "DIRECTION('',(0.8,-0.6,0.))")
                    sense = ".F." if reverse_curve ^ swap_edge else ".T."
                    data = self.record(data, 243, f"EDGE_CURVE('',#{115 if swap_edge else 114},"
                                                 f"#{114 if swap_edge else 115},#242,{sense})")
                    if reverse_bound:
                        data = self.record(data, 310, "EDGE_LOOP('',(#274,#264,#254,#244))")
                        data = self.record(data, 311, "FACE_BOUND('',#310,.F.)")
                    for oe, ec in [(244, 243), (254, 253), (264, 263), (274, 273)]:
                        backward = reverse_bound ^ (swap_edge and oe == 244)
                        data = self.record(data, oe, f"ORIENTED_EDGE('',*,*,#{ec},{'.F.' if backward else '.T.'})")
                    if reverse_face:
                        data = self.record(data, 131, "DIRECTION('',(0.,0.,-1.))")
                        data = self.record(data, 302, "ADVANCED_FACE('',(#311,#301),#134,.F.)")
                    actual, observed = self.invoke(data)
                    self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
                    window = (-6, -4, 6, 4) if shifted else (0, -8, 12, 0) if reverse_face else (0, 0, 12, 8)
                    accepted(observed, data, truth, uv_bounds=window)

    def test_multiple_holes_do_not_depend_on_inner_loop_list_order(self):
        data = (CORPUS / "cases/S203-P02/input.step").read_bytes()
        truth = deepcopy(next(c["truth"] for c in self.cases if c["case_id"] == "S203-P02"))
        # Additional clockwise rectangle: source UV [8,1]..[10,2], area 2, perimeter 6.
        rows = ["#140=CARTESIAN_POINT('',(7.,5.,5.));", "#141=CARTESIAN_POINT('',(6.2,5.6,5.));",
                "#142=CARTESIAN_POINT('',(7.4,7.2,5.));", "#143=CARTESIAN_POINT('',(8.2,6.6,5.));"]
        directions = ["(-0.8,0.6,0.)", "(0.6,0.8,0.)", "(0.8,-0.6,0.)", "(-0.6,-0.8,0.)"]
        for i, direction in enumerate(directions):
            base = 400 + 10 * i
            rows += [f"#{120+i}=VERTEX_POINT('',#{140+i});", f"#{base}=DIRECTION('',{direction});",
                     f"#{base+1}=VECTOR('',#{base},{1 if i % 2 == 0 else 2}.);",
                     f"#{base+2}=LINE('',#{140+i},#{base+1});",
                     f"#{base+3}=EDGE_CURVE('',#{120+i},#{120+(i+1)%4},#{base+2},.T.);",
                     f"#{base+4}=ORIENTED_EDGE('',*,*,#{base+3},.T.);"]
        rows += ["#320=EDGE_LOOP('',(#404,#414,#424,#434));", "#321=FACE_BOUND('',#320,.T.);"]
        data = data.replace(b"ENDSEC;\nEND-ISO", ("\n".join(rows) + "\nENDSEC;\nEND-ISO").encode())
        data = self.record(data, 302, "ADVANCED_FACE('',(#321,#301,#311),#134,.T.)")
        truth["source_topology"] = dict(faces=1, edges=12, vertices=12)
        truth["measures"] = dict(area=exact(86), perimeter=exact(58))
        removed_centroid = [F(36, 5), F(61, 10), F(5)]
        truth["centroid"] = [exact((88 * decode(v) - 2 * r) / 86)
                             for v, r in zip(truth["centroid"], removed_centroid)]
        actual, observed = self.invoke(data)
        self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
        accepted(observed, data, truth)
        self.assertEqual(3, len(observed["loop_mappings"]))

    def test_hole_checker_rejects_lost_or_wrong_occurrence_uv(self):
        data = (CORPUS / "cases/S203-P03/input.step").read_bytes()
        truth = next(c["truth"] for c in self.cases if c["case_id"] == "S203-P03")
        actual, original = self.invoke(data)
        self.assertEqual(0, actual.returncode)
        for defect in ("missing_hole", "wrong_loop", "wrong_use", "wrong_owner", "shifted_uv"):
            observed = deepcopy(original)
            if defect == "missing_hole":
                observed["loop_mappings"].pop()
            if defect == "wrong_loop":
                observed["loop_mappings"][1]["source_loop"] = "300"
            if defect == "wrong_use":
                observed["loop_mappings"][1]["uses"][0]["source_oriented_edge"] = "204"
            if defect == "wrong_owner":
                observed["loop_mappings"][1]["uses"][0]["uv_owner_edgeuse"] = "OPPOSITE"
            if defect == "shifted_uv":
                observed["loop_mappings"][1]["uses"][0]["uv_start_mm"][0] += 6
            with self.subTest(defect=defect), self.assertRaises(AssertionError):
                accepted(observed, data, truth, uv_bounds=(-6, -4, 6, 4))

    def test_all_16_legal_direction_chains(self):
        for reverse_curve, swap_edge, reverse_bound, reverse_face in itertools.product([False, True], repeat=4):
            with self.subTest(flags=(reverse_curve, swap_edge, reverse_bound, reverse_face)):
                data = self.p01
                if reverse_curve:
                    data = self.record(data, 200, "DIRECTION('',(-0.6,-0.8,0.))")
                sense = ".F." if reverse_curve ^ swap_edge else ".T."
                data = self.record(data, 203, f"EDGE_CURVE('',#{111 if swap_edge else 110},"
                                             f"#{110 if swap_edge else 111},#202,{sense})")
                if reverse_bound:
                    data = self.record(data, 300, "EDGE_LOOP('',(#234,#224,#214,#204))")
                    data = self.record(data, 301, "FACE_OUTER_BOUND('',#300,.F.)")
                for oe, ec in [(204, 203), (214, 213), (224, 223), (234, 233)]:
                    backward = reverse_bound ^ (swap_edge and oe == 204)
                    data = self.record(data, oe, f"ORIENTED_EDGE('',*,*,#{ec},{'.F.' if backward else '.T.'})")
                if reverse_face:
                    data = self.record(data, 131, "DIRECTION('',(0.,0.,-1.))")
                    data = self.record(data, 302, "ADVANCED_FACE('',(#301),#134,.F.)")
                observed = self.good(data, uv_bounds=(0, -8, 12, 0) if reverse_face else (0, 0, 12, 8))
                edge = next(e for e in observed["edge_mappings"] if e["source_edge"] == "203")
                self.assertEqual(str(111 if swap_edge else 110), edge["source_start"])
                self.assertEqual(str(111 if reverse_curve else 110), edge["native_min_vertex"])

    def test_source_parameters_are_not_replaced_with_native_01(self):
        data = self.record(self.p01, 200, "DIRECTION('',(3.,4.,0.))")
        edge = self.good(data)["edge_mappings"][0]
        self.assertEqual((F(0), F(5), F(25)), tuple(decode(edge[k]) for k in (
            "source_start_coefficient", "source_end_coefficient", "source_direction_norm_squared")))
        data = self.record(self.p01, 202, "LINE('',#101,#201)")
        edge = self.good(data)["edge_mappings"][0]
        self.assertEqual((F(-1), F(0)), tuple(decode(edge[k]) for k in (
            "source_start_coefficient", "source_end_coefficient")))

    def test_irrational_plane_frame_preserves_world_geometry(self):
        data = self.record(self.p01, 132, "DIRECTION('',(1.,1.,0.))")
        result, observed = self.invoke(data)
        self.assertEqual(0, result.returncode, result.stderr or result.stdout)
        self.assertAlmostEqual(96, observed["native_measurements"]["area_mm2"], delta=1e-6)
        vector(observed["native_measurements"]["centroid_mm"], list(map(decode, self.truth["centroid"])),
               F(1, 10**7), "irrational-frame centroid")

    def test_order_and_sparse_ids_are_not_fixture_dispatch(self):
        lines = self.p01.splitlines(keepends=True)
        rows = [line for line in lines if line.startswith(b"#")]
        indices = [i for i, line in enumerate(lines) if line.startswith(b"#")]
        for index, row in zip(indices, reversed(rows)):
            lines[index] = row
        self.good(b"".join(lines))
        data = re.sub(rb"#([0-9]+)", lambda m: b"#" + str(int(m[1]) + 100000).encode(), self.p01)
        observed = self.good(data)
        self.assertEqual({"100203", "100213", "100223", "100233"},
                         {e["source_edge"] for e in observed["edge_mappings"]})

    def test_exact_native_call_and_report_byte_boundaries(self):
        observed = self.good(self.p01)
        count = observed["native_calls"]
        result, exact = self.invoke(self.p01, "--max-native-calls", count)
        self.assertEqual(0, result.returncode)
        self.assertEqual("Accepted", exact["status"])
        result, failed = self.invoke(self.p01, "--max-native-calls", count - 1)
        self.assertEqual(3, result.returncode)
        self.assertIsNone(failed["model"])
        self.assertTrue(failed["candidate_discarded"])
        self.assertEqual("NativeCallLimit", failed["diagnostics"][0]["key"])
        result, _ = self.invoke(self.p01)
        size = len(result.stdout)
        result, _ = self.invoke(self.p01, "--max-report-bytes", size)
        self.assertEqual(0, result.returncode)
        self.assertEqual(size, len(result.stdout))
        result, failed = self.invoke(self.p01, "--max-report-bytes", size - 1)
        self.assertEqual(5, result.returncode)
        self.assertEqual(b"", result.stdout)
        self.assertIsNone(failed)

    def test_final_native_consistency_is_not_inferred_from_source_or_working_tolerance(self):
        # The new VP uses a separate CP; all defining LINEs keep their original CPs.
        # Distance to the first LINE is exactly 4/5 of the X displacement.
        for x, expected, code in [("3.00000005", "Accepted", 0), ("3.000000125", "Indeterminate", 8)]:
            with self.subTest(vertex_x=x):
                source_residual = (F(x) - 3) * F(4, 5)
                self.assertLessEqual(source_residual, F(1, 10**7))
                data = self.record(self.p01, 110, "VERTEX_POINT('',#140)")
                data = data.replace(b"ENDSEC;\nEND-ISO", f"#140=CARTESIAN_POINT('',({x},-2.,5.));\n"
                                    "ENDSEC;\nEND-ISO".encode())
                actual, observed = self.invoke(data)
                self.assertEqual(code, actual.returncode, actual.stderr or actual.stdout)
                self.assertEqual(expected, observed["status"])
                self.assertEqual("Checked", observed["source"]["planar"])
                self.assertEqual("None", observed["repair"])
                self.assertEqual({"pointer_reports": 0, "level2_reports": 0}, observed["validations"])
                if code:
                    self.assertIsNone(observed["model"])
                    self.assertTrue(observed["candidate_discarded"])
                    self.assertEqual("NativeConsistencyErrorBudget", observed["diagnostics"][0]["key"])
                    self.assertGreater(F(observed["consistency_bound_mm"]), F(1, 10**7))
                else:
                    self.assertIsNotNone(observed["model"])
                    self.assertLessEqual(F(observed["consistency_bound_mm"]), F(1, 10**7))

    def test_oracle_rejects_false_geometry_and_qualification(self):
        original = self.good(self.p01)
        for name in ("area", "domain", "normal", "uv", "consistency", "tolerance", "validator", "model", "source"):
            with self.subTest(tamper=name):
                observed = deepcopy(original)
                if name == "area":
                    observed["native_measurements"]["area_mm2"] = 95
                if name == "domain":
                    observed["native_measurements"]["window_mm"][2] = 11
                if name == "normal":
                    observed["native_measurements"]["normal"][2] = -1
                if name == "uv":
                    observed["uv_bound_mm"] = None
                if name == "consistency":
                    observed["consistency_bound_mm"] = 2e-7
                if name == "tolerance":
                    observed["tolerances"][12]["value_mm"] *= 2
                if name == "validator":
                    observed["validations"]["level2_reports"] = None
                if name == "model":
                    observed["model"] = None
                if name == "source":
                    observed["source"]["sha256"] = "0" * 64
                with self.assertRaises(AssertionError):
                    accepted(observed, self.p01, self.truth)


if __name__ == "__main__":
    unittest.main()
