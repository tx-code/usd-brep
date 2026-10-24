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
