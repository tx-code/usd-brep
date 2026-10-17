# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class CensusTests(unittest.TestCase):
    def test_public_census_excludes_strings_comments_and_keeps_all_complex_facets(self):
        text = ("ISO-10303-21;HEADER;FILE_SCHEMA(('AP_TEST'));ENDSEC;DATA;\n"
                "#1=PLANE('',#2);\n#2=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.));\n"
                "#3=CARTESIAN_POINT('Fake #999=CYLINDRICAL_SURFACE();',(0.,0.,0.));\n"
                "/* #400=SPHERE(); */\nENDSEC;END-ISO-10303-21;\n")
        with tempfile.TemporaryDirectory(prefix="step-census-") as directory:
            root = Path(directory)
            (root/"part.step").write_text(text, encoding="ascii")
            output = root/"census.json"
            actual = subprocess.run([sys.executable, "-m", "tools.step_import_benchmark", "census", "--root", str(root),
                                     "--output", str(output)], capture_output=True, timeout=20)
            self.assertEqual(0, actual.returncode, actual.stderr.decode(errors="replace"))
            data = json.loads(output.read_bytes())
            row, = data["files"]
            self.assertEqual((3, 1, ["AP_TEST"]), (row["entity_records"], row["complex_records"], row["schemas"]))
            self.assertEqual({"PLANE": 1, "CARTESIAN_POINT": 1, "LENGTH_UNIT": 1, "NAMED_UNIT": 1, "SI_UNIT": 1},
                             row["entity_components"])
