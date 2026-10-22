# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Guard the mutation experiment's scope and failure classifications."""
import unittest

import mutations


class MutationContracts(unittest.TestCase):
    def test_patch_refuses_missing_or_ambiguous_anchors(self):
        for data in (b"no anchor", b"needle needle"):
            with self.assertRaises(ValueError):
                mutations.replace_once(data, b"needle", b"replacement")
        self.assertEqual(mutations.replace_once(b"before needle after", b"needle", b"replacement"),
                         b"before replacement after")

    def test_multiline_patch_preserves_tracked_crlf_bytes(self):
        self.assertEqual(mutations.replace_once(b"a\r\nb\r\nc", b"a\nb", b"x\ny"), b"x\r\ny\r\nc")

    def test_infrastructure_errors_do_not_kill_a_mutant(self):
        for name in ("native_failure", "timeout", "native_protocol", "baseline_failure", "something_else"):
            self.assertFalse(mutations.is_detection("analytic_bounds", 1, {"property": name}))
        self.assertFalse(mutations.is_detection("analytic_bounds", 0, {"property": "analytic_bounds"}))
        self.assertTrue(mutations.is_detection("analytic_bounds", 1, {"property": "analytic_bounds"}))

    def test_required_fault_classes_are_distinct(self):
        self.assertEqual({item["expected"] for item in mutations.MUTATIONS},
                         {"analytic_bounds", "reverse_interval", "bernstein_d2", "spline_right_value_d1",
                          "seam_interval_count", "spline_reverse_d1"})

    def test_new_faults_use_their_feature_suites(self):
        extension = {item["name"]: item["suite"] for item in mutations.MUTATIONS if "suite" in item}
        self.assertEqual(extension, {"ignore_knot_side": "repeated", "drop_seam_interval": "seams",
                                     "partial_control_reversal": "multispan"})


if __name__ == "__main__":
    unittest.main()
