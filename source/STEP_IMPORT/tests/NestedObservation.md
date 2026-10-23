<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Nested import observations

`step_import_nested_observation_contracts` uses the public document import interface with
three independent definitions, including two bodies in the middle definition. The test
constructs new AP214 synthetic sources from the public box and polynomial-roof fixtures;
it does not modify an input model or invoke import repair implicitly.

The controls establish definition/body correspondence, request-cumulative accounting,
integration aggregation, owning source lifetime, cancellation before charging a later body,
native-budget exhaustion and exact report capacity. A repair-specific cancellation keeps
the original-attempt witness and its non-additive counter relationship. Reports and full
progress sequences are written to the CTest output directory for same-configuration
before/after comparison. No memory sampling is enabled in those deterministic controls.

Definition counters include their bodies. Body counters retain the existing leaf-observation
semantics, including the repair state machine's partial observations; the entire report tree
must not be summed. A completed member can remain `checksPassed=true` while its unpublished
candidate is discarded after a later failure. Unentered definitions have no report entry.

This is a focused synthetic contract, not complete STEP qualification or a production
tolerance profile. Build through the [root instructions](../../../README.md#build-it).
