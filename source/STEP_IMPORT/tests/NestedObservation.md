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

`step_import_observation_contracts` exercises the agreed private observation interface
with qualified public source snapshots. Member callbacks return cancellation, unsupported
scope and resource-limit errors; finalization returns a postcheck error or throws an
unexpected exception. The controls require prefix cleanup on both error returns and
exception exits, unchanged error identities, inherited source context, integration facts
retained before finalization, and definition accounting settled from the sole request
ledger. The legacy adapter must convert only `Failure`, preserving allocation/configuration
exceptions and the orientation-repair witness.

Compile-time controls reject member callbacks without the exact `Result<void>` contract,
the wrong member role or error type, candidate references or potentially throwing moves,
and legacy adapters around an operation that already returns expected. The project uses
the vendored expected type's `nodiscard` diagnostic under its warnings-as-errors policy.

For a refactor acceptance run, compare deterministic JSON, progress and generated source
bytes against an unchanged same-configuration baseline. Run Release and Debug separately
with matching native libraries. Isolated directed mutants should demonstrate detection of
omitted return-error cleanup, omitted definition counter settlement, overwritten repair
leaf counters, ignored member/finalizer errors, erased publication error categories, and
omitted integration aggregation, and premature definition-batch completion before final
snapshot sealing. Both batch roles retain completed integration observations on sealing
failure, including an allocation exception. Compile and run the unchanged control with the same
object set; a compilation error alone is not a detected runtime mutant. Keep mutated
source copies, raw receipts and timing data under ignored local work directories.

Performance acceptance measures complete public document imports and candidate release,
using the same input, options, compiler and runtime. Warm both executables and alternate
their order across paired batches; collect process CPU and wall time separately.
The goal for this internal refactor is absence of a stable regression within the measured
scope, without changing numerical precision or claiming a new geometric capability.
