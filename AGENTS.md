<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# AGENTS.md

## Project direction

This fork develops a BRep geometry SDK for CAM: the SMLib kernel, SM_API, native STEP import, and Python bindings.
Analytic and NURBS definitions carry the geometry; tessellation is derived output.
OpenUSD, Hydra, BrepArray schemas, and USD-based OCCT converters have been removed. The repository and Python package
retain the names `usd-brep` / `usd_brep` for compatibility. The default SDK has no USD or TBB dependency.

## Working scope and completion

- Carry the requested change through implementation, relevant validation, and fixes for failures it introduces.
  Local builds, focused tests, and disposable synthetic reproductions are part of that work; proceed without
  requesting approval at each step. Report the result, validation scope, and any remaining blocker.
- Read the references that match the task. Start in the owning layer below and follow callers, dependencies,
  and relevant tests when needed. Tests under `tests/`, component tests, and tools are available for this purpose.
- Keep broad searches out of generated trees (`_build/`, `_compiler/`, `_repo/`, `_builtpackages/`), binary fixture
  collections, and `.git/`. Use explicit paths for required build outputs, logs, or fixtures; inspect Git through Git.
- Current task instructions determine the authorized scope. Historical design-stage pauses apply to their recorded
  phase; a design approval alone does not authorize a new implementation phase. Preserve unrelated working changes.

## Architecture and task routing

| Task | Owning code and entry reference |
|---|---|
| Geometry, topology, numerical algorithms | `source/SMLib/`; [kernel workflow](.agents/skills/smlib-kernel-operations/SKILL.md) for operation failures |
| Native `SmApi*` wrappers | `source/SM_API/`; [API maintenance](.agents/skills/smlib-sm-api-maintain/SKILL.md) for wrapper changes, [API use](.agents/skills/smlib-sm-api-use/SKILL.md) for callers |
| Stable Python bindings | `source/SmPyLib/`, including its stubs and tests; [binding maintenance](.agents/skills/smlib-python-api-maintain/SKILL.md) |
| Developer API (`_smlib_dev`) | `source/SmPyDevLib/`, including its stubs and tests; [dev binding maintenance](.agents/skills/smlib-python-dev-api-maintain/SKILL.md) |
| Native STEP import | `source/STEP_SOURCE/`, `source/STEP_SEMANTICS/`, `source/STEP_PLANAR/`, `source/STEP_IMPORT/`; [import contract and current capabilities](source/STEP_IMPORT/README.md), then the owning component's README |
| Python GUI and rendering | `tools/smlib_gui/`, `tools/scripts/smlib_gui*.py`; [GUI workflow](.agents/skills/usd-brep-tools-python-gui/SKILL.md) |

- For operation usage or tuning, use the matching guide from [.agents/operations/INDEX.md](.agents/operations/INDEX.md).
  For kernel failures localized to booleans, fillets, tessellation, drop-curve, healing, or solvers, select the matching
  `smlib-kernel-*` skill under `.agents/skills/`; use the general kernel workflow for cross-cutting failures.
- For `SmBrep` structure, topology validity, or `SmBrepData` transfer, use
  [smlib-brep-model](.agents/skills/smlib-brep-model/SKILL.md) and its canonical format reference.
- For `_omni_solid` `RuntimeError`, consult [.agents/docs/errors.md](.agents/docs/errors.md) to interpret the status
  and kernel trace before tracing the failing implementation.
- For broader architecture and the native header map, use [.agents/docs/OVERVIEW.md](.agents/docs/OVERVIEW.md).
  Use [GLOSSARY.md](GLOSSARY.md) when defining domain terms and [planar regions](docs/planar-regions.md) for that API.

## Native STEP contracts

`STEP_SOURCE` owns lossless input; `STEP_SEMANTICS` owns typed source interpretation; `STEP_PLANAR` owns qualified
planar source checks; `STEP_IMPORT` owns native construction and publication, including the opt-in general BRep
development path. Each layer owns distinct evidence; consult its README for the exact scope.

- Keep product reading and conversion independent of OCCT and Truck/ruststep. Their documentation/source and
  independent test comparisons can inform the implementation. Public reproductions use synthetic models;
  private model libraries and their identifying inventories stay local.
- Preserve immutable source bytes, entity identities, units, parameter conventions, orientation, and source-to-native
  correspondence. Reuse shared semantic bindings and ownership/publication mechanisms rather than rebuilding
  parallel interpretations. Keep equivalent representation normalization distinct from repair.
- Qualify capabilities by source schema/profile, explicit options, quality configuration, and checks actually run.
  Schema recognition, a successful native validator, or a rendered mesh cannot establish full import correctness.
  Surface closure alone does not establish material-solid semantics. Keep source rejection, unsupported scope,
  unestablished numerical evidence, and native failure distinct.
- Keep source uncertainty, acceptance error budgets, native working tolerances, and repair allowances distinct.
  Existing strict paths retain `Repair=None`; interoperability and development paths require their explicit options.
  Interoperability exceptions stay within proven nongeometric cases and retain the original diagnostics.
  Source uncertainty must not silently enlarge a tolerance or select a quality configuration. Sampling is not a
  continuous error bound, and fixture/development values are not production defaults.
- Publish the complete selected model and required report only after the applicable checks and final execution
  decision succeed. Failures and cancellation release unpublished candidates without partial publication.
  Acceptance evidence belongs to the owning read-only snapshot; modeling copies need their own validation.
  Separate contexts alone do not establish kernel thread safety.

For STEP design changes, consult the [design contract](docs/design/step-import.md). For implementation and validation
claims, use current component contracts, code, and reusable contract tests; per-run receipts stay local.
Use [benchmark guidance](tools/step_import_benchmark/README.md) for import profiling and the
[catalog design](docs/design/step-catalog.md) for the selected compile-time migration.

## Python Quick Start and ownership

For runnable packaged `usd_brep` examples, use [the package README](tools/pyproject/README.md); for scripts using
`_omni_solid`, use [Python API usage](.agents/skills/smlib-python-api-use/SKILL.md) and the matching operation guide.

- Stable modeling APIs belong in `_omni_solid`. **Developer API (`_smlib_dev`)** owns topology-debugger walks/picking,
  kernel Draw extraction, viewport helpers, and the tests facade. Import `_omni_solid` before `_smlib_dev`.
  Native STEP import currently exposes a C++ library and optional CLI; it does not imply a Python STEP binding.
- Follow each operation's docstring: `Pure` leaves inputs unchanged; `Consumes` transfers the named inputs;
  `Mutates` returns the same handle, or `(handle, ...diagnostics)`. Use `brep.copy()` when preserving an input.
  Operation names alone do not establish mutation or ownership behavior.
- `Face.surface()` and `Edge.curve()` borrow Brep-owned geometry. Release affected views before representation-changing
  `turn_to_nurbs()` or scale, then reacquire them, or mutate a copy. Pointer-preserving rigid transforms, positive
  uniform scales, and transforms of base NURBS geometry retain those views. Kernel memory is context-managed;
  honor the operation's context, transfer, and failure-cleanup contracts.

<a name="build--test-commands"></a>

## Build and validation

[README.md](README.md#build-it) owns build commands, prerequisites, runtime paths, and optional target flags.
Use the default CMake SDK build; enable development, validation, or examples explicitly as needed.
C++20, configuration, CRT, and kernel layout macros must match across libraries and consumers.
Keep root CMake and standalone compiler settings consistent when changing build requirements.

When validation scope is unclear, use
[usd-brep-test-selection](.agents/skills/usd-brep-test-selection/SKILL.md) and the owning component's test guidance.
Start with checks that exercise the affected contract; broaden for shared ownership, numerical, ABI, or build risk.
For STEP changes, include the corresponding source/semantic/planar/native contract or evidence checks.
Documentation-only edits need link/path and whitespace checks. Record the tested configuration and limitations;
a focused local pass establishes its stated scope, not a full CI or production qualification.

## Code and documentation conventions

- Follow `.clang-format`, `.editorconfig`, and `.gitattributes`: C++20, four-space indentation, UTF-8;
  Python uses PEP 8 with a 120-column limit. Preserve vendored upstream bytes and license/provenance notices.
- Critical code needs nearby rationale: explain invariants for nontrivial algorithms, schema interpretation,
  tolerances, ownership/mutation, cleanup/cancellation, concurrency, and resource boundaries. State relevant units,
  orientation/parameter conventions, and failure behavior; update stale comments with the code.
- For standards-derived behavior, cite the fixed source artifact and entity/clause or canonical repository contract.
  Distinguish source requirements, project restrictions, derived evidence, and checks still unimplemented.
- Agent guidance lives in `AGENTS.md` and `.agents/`. Keep this file focused on shared constraints and task pointers;
  place operation details in the matching guide and link new guides from `.agents/operations/INDEX.md`.
  Follow [documentation placement](docs/README.md) for durable docs, frozen test inputs, and local work records.
  Remove a `KNOWN_ISSUES.md` entry when the change fixes it. For upstream synchronization, use
  [the merge skill](.agents/skills/usd-brep-merge-upstream/SKILL.md); fork policy and release routing live in
  [FORK_MAINTENANCE.md](FORK_MAINTENANCE.md).
- Include a [changeset](.changeset/README.md) with caller-visible fixes, features, or compatibility changes;
  internal refactors and documentation-only edits need none. Use the
  [release process](docs/release/RELEASE_PROCESS.md) when preparing release records or a version update.
