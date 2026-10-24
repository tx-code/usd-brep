<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Face-seeded stitching regressions

The public `SmApiStitch(brep, face)` overload passes one target Brep to `SmMerge`.
Its missing second Boolean operand is intentional. Default tolerance calculation
must use the target's edge sizes and tolerance without dereferencing that operand
or replacing it in the intersector. Two-Brep calculations retain both inputs.

The native contracts use only synthetic analytic primitives:

- `box` / `plane`: valid no-partner calls return success, retain the expected
  topology, area, working tolerance and solid/sheet role, and pass pointer and
  LEVEL2 validation. The box also retains its analytic volume.
- `adjacent`: two disconnected 10x20 planes are copied into one target. Stitching
  its newly added face shares one edge and two endpoints (two faces, seven edges,
  six vertices), retains area 400 and leaves the source copy valid.
- `foreign`: a face owned by another Brep and required null inputs are rejected;
  both actual owners retain their topology and validity.
- `boolean`: two overlapping boxes retain the analytic union/difference/
  intersection volumes and the documented consuming-operand ownership.

Configure the SDK with `SMLIB_BUILD_VALIDATION=ON`, then run matching native builds:

```shell
cmake --build _build/cam-tools --config Release --target stitch_face --parallel 3
ctest --test-dir _build/cam-tools -C Release --output-on-failure -R "^stitch_face_"
```

Repeat with Debug and its matching libraries/layout macros. The stable Python
`TestStitching` controls exercise the same public no-op and foreign-face contracts
through the real binding. Use the runtime built from the matching SDK:

```shell
python tools/agent/run_geometry_tests.py --runtime _build/cam-tools/bin/Release --target test_omni_solid.TestStitching
```

An isolated frozen old runtime can verify sensitivity: the no-partner/adjacent
contracts encounter the original null-operand access violation while the two-Brep
Boolean control still succeeds. This is the expected crash regression signal;
unrelated crashes and ABI/runtime mismatches do not establish detection. Keep
runtime copies, debugger stacks and per-run receipts local. These modeling tests
do not qualify STEP repair, full import correctness or a new tolerance policy.
