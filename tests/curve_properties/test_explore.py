# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Reproduction and reduction contracts for the exploration tool."""
import copy
import unittest

import explore
import harness


class ExplorationTests(unittest.TestCase):
    def test_seed_replays_complete_inputs(self):
        first = list(explore.generate(431, 64))
        self.assertEqual(first, list(explore.generate(431, 64)))
        self.assertNotEqual(first, list(explore.generate(432, 64)))
        self.assertEqual({case["kind"] for case in first}, {"bezier", "circle", "ellipse"})
        for case in first:
            explore.validate(case)

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


if __name__ == "__main__":
    unittest.main()
