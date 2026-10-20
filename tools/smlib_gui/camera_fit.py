# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Orthographic CAD depth fitting without changing the user's screen-space zoom."""
import itertools
import numpy as np


def fit_depth(camera, bounds):
    corners = np.asarray(list(itertools.product(*[(bounds[i], bounds[i + 1]) for i in (0, 2, 4)])), dtype=float)
    position = np.asarray(camera.position, dtype=float)
    direction = np.asarray(camera.focal_point) - position
    distance = np.linalg.norm(direction)
    if not np.isfinite(distance) or distance <= 0 or not np.isfinite(corners).all():
        raise ValueError("Finite camera and geometry bounds required")
    direction /= distance
    span = max(float(np.linalg.norm(corners.max(axis=0) - corners.min(axis=0))), 1e-6)
    margin = max(span * .02, 1e-5)
    depths = (corners - position) @ direction
    # Moving an orthographic eye along its axis leaves every screen coordinate
    # unchanged. Keep it outside the complete model during zoom/pan operations.
    if camera.parallel_projection and depths.min() < margin * 2:
        position -= direction * (margin * 2 - depths.min())
        camera.position = tuple(position)
        depths = (corners - position) @ direction
    near = max(float(depths.min() - margin), span * 1e-6)
    camera.clipping_range = (near, max(float(depths.max() + margin), near + margin))
