# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""N916 labels cannot alter material role or analytic box measurements."""
import unittest

from tools.step_import_native_validation.tests import test_opaque_identity as native
from tools.step_semantic_validation.tests.test_n916_representation_caption import caption_source


class NativeN916CaptionTests(unittest.TestCase):
    check_box = native.NativeOpaqueIdentityTests.check_box

    def test_opaque_and_ap214_reserved_spellings_keep_n916_box(self):
        for name in (b'\x90', b'centre of mass', b'treatment result'):
            with self.subTest(name=name):
                self.check_box(caption_source(name), 1 if name == b'\x90' else 0)
