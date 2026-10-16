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
from tools.step_import_validation.oracle import decode
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
        self.assertEqual(Counter(Accepted=1, Rejected=25, Unsupported=17, Deferred=6), counts)

    def test_frozen_independent_closest_point(self):
        truth = self.truth["queries"]["off_face"]
        query = list(map(decode, truth["point_mm"]))
        actual, observed = self.invoke(self.p01, "--query", *map(float, query))
        self.assertEqual(0, actual.returncode, actual.stderr)
        accepted(observed["import"], self.p01, self.truth)
        vector(observed["query"]["closest_point_mm"], list(map(decode, truth["closest_point_mm"])),
               F(1, 10**7), "independent closest point")
        self.assertLessEqual(abs(F(observed["query"]["distance_mm"]) - 3), F(1, 10**7))

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
