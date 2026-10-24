<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Healing status contracts

The `healing_status` validation target checks failure propagation through `SmApiHealBrep`,
`SmBrep::HealBrep`, the native healer sequence, topology-property refresh, and
`SmFace::SplitAtSeam`. Build with the root CMake validation option and run the CTest named
`healing_status`; see the [build instructions](../../README.md#build-it).

The synthetic fixture is a quarter-cylinder boundary on a full periodic NURBS support whose
seam crosses the face. Public virtual surface splitting or curve copying returns an error;
evaluation, parameter domains, classifications and topology operations remain real.
The tests assert the failed operation was reached, its status survives, and editing/tracking
scopes are released. Refresh follows a real `ReplaceSurface` notification, then checks that
the outer stage and recursion state are restored on failure.

The full-healer cases use fresh geometry for each entrypoint and both initial editing settings.
Successful controls also check Level 2 validity and repeated healing. The U/V insertion controls
check status, pointer safety and scope restoration; a successful low-level insertion alone does
not establish a fully healed periodic face. The test does not change geometric tolerances,
promise transaction rollback, or establish complete healing coverage. Remaining defects belong
in [known issues](../../KNOWN_ISSUES.md).

The `healing_uv_trim_failure`, `healing_uv_trim_disabled` and `healing_uv_trim_control`
CTest cases cover the optional native UV-trim stage. A real planar face uses a public virtual
surface solver that returns either nonconvergence or invalid input; projection retries and
copied-surface recovery preserve that failure. The failure test prepares real properties
through seam movement, then calls the public `Make_UVTrimCurves` stage with a retained target.
It checks the exact face error and pointer/scope safety; the disabled stage must not project.
This avoids mistaking an earlier unsupported-face deferral for a UV-stage failure, and does
not establish full-pipeline propagation for the injected solver failure. Normal full-entry
controls exercise `SmBrep::HealBrep` and `SmHealData::HealBrep` with both option and editing
settings, check requested trims before validation can create them, then check Level 2 validity
and repeated healing. The ordinary `SmApiHealBrep` has no UV-generation option.

The `healing_degen_face_*` cases exercise a real zero-area planar face bounded by two independent
NURBS lines with the same two vertices, traversed in opposite directions. Its public virtual
point evaluator fails only after real face deletion turns its edge into a wire. Construction,
degeneracy classification, and the attempted squeeze remain real; both curves share failure state
outside their lifetime so deletion cannot erase the evidence. Direct `SqueezeFace` delivers
`SM_ERR` after its internal stitching failure. The native sequence, `SmBrep::HealBrep`, and full
`SmApiHealBrep` must preserve that delivered status with both initial editing settings, restore
editing/tracking, and stop before recording the failed stage as complete. Reset/destruction of
the retained session must release stale properties safely. Remaining wire intervals keep their
original parameter domains; the failed operation is not rolled back or accepted as a repaired model.

Successful same-shape, vertex-only, and connected-wire controls check ordinary cleanup, retention
of vertices still used by other topology, connected-wire identities/coordinates/parameter interval,
pointer/geometry reports, Level 2 validity, and repeated
healing. `SmHealData` test sessions are heap allocated to keep nested Debug calls within the default
thread stack. Dynamic failure coverage concerns `SqueezeFace`; the two cleanup error guards are
source-audited, with their successful paths covered by controls. No damaged pointer graph is
introduced to manufacture `DeleteEdge` or `DeleteVertex` failure coverage.
