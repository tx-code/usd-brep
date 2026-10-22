# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Reproduction and reduction contracts for the exploration tool."""
import copy
from pathlib import Path
import unittest

import explore
import harness
import spline_oracle as spline


class ExplorationTests(unittest.TestCase):
    def test_seed_replays_complete_inputs(self):
        first = list(explore.generate(431, 64))
        self.assertEqual(first, list(explore.generate(431, 64)))
        self.assertNotEqual(first, list(explore.generate(432, 64)))
        self.assertEqual({case["kind"] for case in first}, {"bezier", "circle", "ellipse", *spline.KINDS})
        for case in first:
            explore.validate(case)

    def test_spline_generator_covers_degrees_multiplicities_and_periodic_windows(self):
        cases = list(explore.generate(431, 128, "multispan"))
        self.assertTrue(all(case["kind"] in spline.KINDS for case in cases))
        self.assertEqual({case["degree"] for case in cases if case["kind"] == "nurbs"}, {1, 2, 3})
        self.assertEqual({len(set(case["knots"])) - 1 for case in cases if case["kind"] == "nurbs"}, set(range(2, 9)))
        self.assertTrue(any(case["kind"] == "nurbs" and case["knots"].count(.5) > 1 for case in cases))
        self.assertTrue(any(case["kind"] == "periodic_nurbs" and case["clip"][1] > 1 for case in cases))
        for case in cases:
            explore.validate(case)

    def test_structural_reductions_keep_repeated_knots_and_seam_class(self):
        cases = [spline.fixed_cases()[6], spline.fixed_cases()[-1]]
        for case in cases:
            before = explore.features(case)
            proposals = list(explore.reductions(case))
            self.assertTrue(proposals)
            for candidate in proposals:
                explore.validate(candidate)
                self.assertEqual(explore.features(candidate), before)
            reduced, attempts = explore.minimize(case, lambda candidate: True, budget=100)
            self.assertLess(explore.complexity(reduced), explore.complexity(case))
            self.assertEqual(explore.features(reduced), before)
            self.assertLessEqual(attempts, 100)

    def test_repeated_suite_requires_internal_c0_multiplicity_above_one(self):
        for case in explore.fixed_cases("repeated") + list(explore.generate(431, 32, "repeated")):
            explore.validate(case)
            self.assertGreater(case["degree"], 1)
            self.assertTrue(all(case["knots"].count(t) == case["degree"]
                                for t in sorted(set(case["knots"]))[1:-1]))

    def test_reduction_keeps_a_failure_and_valid_geometry_domain(self):
        original = harness.fixed_cases()[0]
        before = copy.deepcopy(original)
        visited = []

        def fails(case):
            explore.validate(case)
            visited.append(case)
            return case["clip"][1] < 1 and case["points"][0] != case["points"][-1]

        reduced, attempts = explore.minimize(original, fails, budget=80)
        self.assertTrue(fails(reduced))
        self.assertLess(explore.complexity(reduced), explore.complexity(original))
        self.assertEqual(original, before)
        self.assertLessEqual(attempts, 80)
        self.assertTrue(visited)

    def test_reduction_obeys_budget_without_accepting_unrelated_failures(self):
        original = harness.fixed_cases()[3]
        reduced, attempts = explore.minimize(original, lambda case: False, budget=3)
        self.assertEqual(reduced, original)
        self.assertEqual(attempts, 3)

    def test_invalid_replay_inputs_are_rejected_before_native_calls(self):
        for field, value in (("weights", [0, 1]), ("domain", [1, 1]), ("clip", [.8, .2]),
                             ("points", [[float("nan"), 0, 0], [1, 0, 0]])):
            case = copy.deepcopy(harness.fixed_cases()[0])
            case[field] = value
            with self.assertRaises(ValueError):
                explore.validate(case)

    def test_frozen_source_does_not_claim_a_parent_checkout_revision(self):
        self.assertEqual(explore.repository_context(Path(__file__).resolve().parent),
                         {"revision": None, "working_changes": None})


if __name__ == "__main__":
    unittest.main()
