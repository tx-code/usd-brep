<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Known issues

Known bugs in the SMLib geometry SDK.

## Modeling

### `remove_fillet` removes nothing and reports success

`remove_fillet` (`SmApiRemoveFillet`) returns success without removing the fillet.

**Workaround:** none. Check the result after the call, for example its face count or `volume()`.

### `heal_brep` can report success when a repair failed

`heal_brep` (`SmApiHealBrep`) does not report failures from its repair stages, including seam insertion.

**Workaround:** check the healed Brep with `is_manifold_solid()` and, in development builds, `_smlib_dev.assert_valid()`. These checks cover their respective topology contracts; they do not establish complete geometric validity.

### Healing closed surfaces with missing or coincident seams

`heal_brep`, and the healing run on import, can mis-repair faces on periodic and closed surfaces such as tori, spheres and cylinders. These faces mostly come from other CAD systems.

- A missing seam on a torus face is not detected when the face boundary lies partly along the other seam, or when the face has only interior holes.
- Inserting the first seam on an uncut torus splits it into two faces.
- A sphere bounded by two coincident seam edges is treated as collapsed and deleted.
- A cylindrical face with a pocket can be left with two outer loops after its seam is repaired.
- A torus whose one seam edge is used by two loops is treated as collapsed.
- A face with a missing seam can be skipped by seam insertion, because its loops are classified too early.
- Rebuilding a closed face inside a hollow solid can merge the cavity into the solid.
- A boundary edge that passes through a surface pole is not split at the pole.
- An edge-to-face gap near a seam can be measured on the wrong side of the surface, so healing tightens the edge tolerance too far.
- A face in which one degenerate edge is used by two of its loops is left with invalid topology.

**Workaround:** none. Check healed Breps as described above.

### `drop_curve_to_surface` near a surface pole

Near a surface pole, for example the apex of a cone, dropping a curve onto a surface can miss a seam that the curve starts on or runs along within tolerance, and so classify the curve's boundary incorrectly.

### Derived UV projection can increase edge tolerance despite a face-level no-adjust request

`SmFace::CreateUVTrimCurves(FALSE, ...)` calls an edgeuse projection path whose
retry-success branches can increase the edge's working tolerance. This also
affects callers of `SmEdgeuse::CreateUVTrimCurve`, including derived-UV rebuilding.
An exact cylinder with circular rims split at non-axis-aligned angles reproduces
`GeneralUnexpectedToleranceGrowth` under the native import development quality.
See the [public analytic regression](tools/step_import_native_validation/tests/test_projection_tolerance.py).

**Workaround:** retain the final native working-tolerance budget check and reject
an excessive candidate. Do not reset the stored tolerance or enlarge the budget
to hide a projection error. The public test checks safe rejection; it does not
establish that the projection defect is fixed.

## Tessellation

### Inside-out triangles next to fillets

On solids filleted with `circular_fillet`, `tessellate()` can wind some triangles against their face's normal, so they render inside out. Seen on cylinder caps and on cones next to fillets.

**Workaround:** a `surface_angle_tolerance_deg` of 15° avoided every case found.
