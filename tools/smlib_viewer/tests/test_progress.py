# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
import unittest
from tools.smlib_viewer.progress import ProgressReader


class ProgressTests(unittest.TestCase):
    def test_arbitrary_pipe_chunks_and_untrusted_output(self):
        reader = ProgressReader()
        raw = (b"kernel output\n@brep-view:capture,0,0\r\n@brep-view:mesh-faces,12,12\n"
               b"@brep-view:unknown,0,0\n@brep-view:mesh-faces,13,12\n@brep-view:mesh,-1,0\n")
        updates = []
        for byte in raw:
            updates.extend(reader.feed(bytes([byte])))
        self.assertEqual([("capture", 0, 0), ("mesh-faces", 12, 12)],
                         [(p.phase, p.completed, p.total) for p in updates])
        self.assertNotIn("%", updates[-1].text)
        reader.feed(b"x" * 100000)
        self.assertLessEqual(len(reader.pending), 256)
        self.assertEqual(1, len(reader.feed(b"\n@brep-view:mesh,0,0\n")))
