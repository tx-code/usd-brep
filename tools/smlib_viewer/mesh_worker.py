# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Isolated mesh I/O. Does not establish CAD units, topology or source qualification."""
from pathlib import Path
import hashlib
import json
import sys


def run(filename, directory):
    output = Path(directory)
    result = {"format": "brep-view-scene.v1", "status": "DisplayFailure", "key": "", "diagnostics": []}
    try:
        if sys.stdin.readline().rstrip("\r\n") != "start":
            raise ValueError("StartHandshakeRequired")
        print("@brep-view:capture,0,0", flush=True)
        import numpy as np
        import pyvista as pv
        source = Path(filename)
        with source.open("rb") as stream:
            data = stream.read(64 * 1024 * 1024 + 1)
        if len(data) > 64 * 1024 * 1024:
            raise ValueError("InputByteLimit")
        # The reader sees the exact captured version, even if the original is
        # edited during a load. Neither source file nor mesh is repaired.
        snapshot = output / ("input" + source.suffix.lower())
        snapshot.write_bytes(data)
        print("@brep-view:mesh,0,0", flush=True)
        mesh = pv.read(snapshot)
        if not isinstance(mesh, pv.PolyData) or not mesh.n_cells or not np.isfinite(mesh.points).all():
            raise ValueError("UnsupportedOrEmptyMesh")
        if mesh.n_cells > 2000000 or mesh.n_points > 6000000:
            raise ValueError("DisplayMeshLimit")
        print("@brep-view:publish,0,0", flush=True)
        mesh.save(output / "mesh.vtp")
        boundary = mesh.extract_feature_edges(feature_angle=30, boundary_edges=True,
                                             non_manifold_edges=True, feature_edges=True, manifold_edges=False)
        boundary.save(output / "edges.vtp")
        result.update(status="Ready", mesh="mesh.vtp", boundary="edges.vtp", role="Mesh", units="unknown",
                      points=mesh.n_points,
                      triangles=mesh.n_cells, source_sha256=hashlib.sha256(data).hexdigest())
    except Exception as error:
        result["key"] = str(error)
    pending = output / "scene.json.tmp"
    pending.write_text(json.dumps(result), encoding="utf-8")
    pending.replace(output / "scene.json")
    return 0 if result["status"] == "Ready" else 1
