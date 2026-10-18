# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Contracts for the historical package name wrapping the geometry SDK."""
import importlib
import os
import subprocess
import sys
import unittest
import usd_brep

_CORE = sys.modules.get("usd_brep._omni_solid") or sys.modules["_omni_solid"]

class TestGeometryPackage(unittest.TestCase):
    def test_reexports_and_submodule_imports(self):
        self.assertIs(usd_brep.Brep, _CORE.Brep)
        for name in ("primitives", "curves", "surfaces", "booleans", "sweeps"):
            self.assertIs(importlib.import_module(f"usd_brep.{name}"), getattr(_CORE, name))
        self.assertNotIn("usd", usd_brep.__all__)
        self.assertFalse(hasattr(_CORE, "usd"))

    def test_import_without_scene_runtime(self):
        # A subprocess catches import-time dependencies that an already loaded
        # extension or plugin could hide in this test process.
        env = os.environ.copy()
        env.pop("OMNISOLID_PLUGIN_PATH", None)
        env.pop("PXR_PLUGINPATH_NAME", None)
        subprocess.run([sys.executable, "-c", """
import importlib.abc
import sys
class RejectSceneRuntime(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname == 'pxr' or fullname.startswith('pxr.'):
            raise AssertionError('geometry package tried to import OpenUSD')
sys.meta_path.insert(0, RejectSceneRuntime())
import usd_brep as sm
box = sm.create_box((0, 0, 0), 2, 3, 4)
assert abs(box.volume() - 24.0) < 1e-8
assert not hasattr(sm, 'usd')
assert 'pxr' not in sys.modules
"""], env=env, check=True, timeout=30)

    def test_geometry(self):
        box = usd_brep.create_box((0, 0, 0), 2, 3, 4)
        self.assertEqual(box.face_count(), 6)
        self.assertAlmostEqual(box.volume(), 24)
        self.assertGreater(len(usd_brep.tessellate(box).to_mesh_arrays()["faces"]), 0)
