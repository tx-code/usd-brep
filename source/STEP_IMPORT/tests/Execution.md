<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Execution results

`step_import_execution_contracts` exercises the private request ledger directly,
without constructing native geometry or establishing source/repair qualification.
The public lifecycle and nested-import controls independently cover complete model
and mandatory-report publication.

Commit controls require explicit results for cancellation, an expired final deadline,
and (on Windows) the process-private memory guard. The final deadline/memory guards
precede the one terminal CAS; regular checkpoints have their own observer/cancellation
order. Commit adds no callback, checkpoint or native charge. A successful terminal
decision survives destruction and rejects late cancellation; every failed request
closes its control. Repair completion separately refuses unclaimed orientation or
cylindrical-band entries without inventing native work.

Regular checkpoints preserve unsupported hard-memory admission, observer failure,
pending cancellation, deadline and process-memory order. Native admission checks
before charging; root aliases are charged once and refused work changes neither
root nor leaf counts. Planar quota diagnostics retain their own message and D5
stage. Repair claims retain the accepted prefix on a conflicting entry, proof
spending remains request cumulative, and cylindrical-band proof preserves its
charge-before-check cadence. Observer `bad_alloc` escapes; an unknown observer
exception becomes `ExecutionCallbackFailure`.

Execution errors propagate as results through native construction, exact unit
arithmetic, UV, ring subdivision, periodic restriction and surface proofs. Proof
controls inject cancellation and resource refusal at every reached checkpoint;
these must not become unestablished mathematics or permit later work. Returned
errors during partial UV/support installation retain owning cleanup. Public
repair controls stop the strict original attempt before and during construction,
checking its status, diagnostics, overlapping native count and discard flag.
Public lifecycle controls distinguish context-only planar allocation from a held
BRep candidate. None of these controls relaxes geometric acceptance or precision.

Directed isolated mutants should detect omitted CAS arbitration, omitted final
deadline/memory guards, and bypassed repair completion. Compile and run an unchanged
control using the same frozen objects and matching Release/Debug native libraries.
A compile failure does not count as runtime detection. Keep mutated copies and raw
receipts under ignored local directories.

Also mutate checkpoint propagation, native/proof spending, strict-attempt error
classification and returned-error cleanup. The unchanged controls must pass and
the mutated programs must reach a contract assertion. Ignored-result diagnostics
are a separate compile-time safeguard, not runtime mutation evidence.

This is not a global exception-free claim. One-shot control construction and public
option admission retain their existing invalid-configuration contract until their
own compatibility migration. STL allocation errors must not become configured quota
failures. See [nested observations](NestedObservation.md) for full report/progress
baseline comparison and [root build guidance](../../../README.md#build-it).
