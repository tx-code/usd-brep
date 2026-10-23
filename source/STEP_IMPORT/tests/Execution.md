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

Directed isolated mutants should detect omitted CAS arbitration, omitted final
deadline/memory guards, and bypassed repair completion. Compile and run an unchanged
control using the same frozen objects and matching Release/Debug native libraries.
A compile failure does not count as runtime detection. Keep mutated copies and raw
receipts under ignored local directories.

This is not a global exception-free claim. One-shot control construction and public
option admission retain their existing invalid-configuration contract until their
own compatibility migration. STL allocation errors must not become configured quota
failures. See [nested observations](NestedObservation.md) for full report/progress
baseline comparison and [root build guidance](../../../README.md#build-it).
