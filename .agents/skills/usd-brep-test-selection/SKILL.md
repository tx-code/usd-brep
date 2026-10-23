---
name: usd-brep-test-selection
description: "Choose validation for SMLib changes or CI failures; map changed files to focused build/test commands. Do not use for implementation design."
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# SMLib Test Selection

Use this skill whenever validation scope is unclear.

## Purpose

Select the smallest credible build, test, or sanity-check path for an SMLib change, then decide when broader validation is justified.

## Prerequisites

- Inspect changed files or the failing CI job before selecting tests.
- Know whether the task is docs-only, Python binding, `SM_API`, kernel, native STEP, GUI/headless rendering, build, or tooling.
- Use implementation/debug skills first when the code behavior is not understood.

## Example Requests

```text
Use $usd-brep-test-selection to choose tests for a change in source/SmPyLib/src/SmPyBooleans.cpp.
Use $usd-brep-test-selection to triage a failing prog_test booleans CI job.
Use $usd-brep-test-selection to decide whether .agents-only edits need kernel tests.
```

## First Inspect Impact

Run:

```bash
git status --short
git diff --name-only
```

Classify touched files by layer before choosing tests.

Convenience helper:

```bash
python3 tools/agent/suggest_tests.py
python3 tools/agent/suggest_tests.py --paths source/SmPyLib/src/SmPyBooleans.cpp
```

## Test Map

| Changed area | First validation | Broader validation |
|---|---|---|
| `source/SmPyLib/` (stable bindings) | `cmake --build _build/cam-tools --config Release --parallel 4`, then the stable Python CTest command below | `ctest --test-dir _build/cam-tools -C Release --output-on-failure` |
| `source/SmPyDevLib/`, including its `stubs/smlib_dev/` | `cmake --build _build/cam-tools --config Release --parallel 4`, then `run_geometry_tests.py --dev` against the selected runtime | `ctest --test-dir _build/cam-tools -C Release --output-on-failure` plus `tools/scripts/smlib_gui_smoke.py` when pick/draw/tests change |
| `tools/smlib_gui/`, `tools/scripts/smlib_gui*.py` | `uv run --locked --group gui python tools/scripts/smlib_gui_smoke.py` | Run `smlib_gui_render.py --jobs N` on a focused recursive fixture tree; add SmPyLib tests if bindings changed |
| `source/SM_API/` or `source/SM_API/inc/` | `cmake --build _build/cam-tools --config Release --parallel 4`, focused Python repro or API-level test for the operation | `./tests/SM_API_test/SM_API_test.sh linux-x86_64`, then `ctest --test-dir _build/cam-tools -C Release --output-on-failure` |
| `source/SMLib/` | `cmake --build _build/cam-tools --config Release --parallel 4`, minimal repro through `SM_API` or `_omni_solid`, then a focused `prog_test` suite when mapped below | Relevant C++ suite plus `ctest --test-dir _build/cam-tools -C Release --output-on-failure` |
| `tests/prog_test/` | `cmake --build _build/cam-tools --config Release --target prog_test_app --parallel 4`, then focused `prog_test` suites | `./tests/prog_test/prog_test.sh linux-x86_64` |
| `source/STEP_SOURCE/`, `source/STEP_SEMANTICS/`, `source/STEP_PLANAR/`, `source/STEP_IMPORT/`, `tools/step_*` | Build the affected CMake target and run its contract tests (`ctest ... -R step_`); use the owning README for focused evidence-tool tests | Broader STEP contract/evidence checks when shared interfaces change |
| `.agents/`, `AGENTS.md`, docs only | markdown/path sanity checks; no kernel tests | none required |
| `tools/agent/` | run/lint the changed script or a targeted tool check | relevant script validation or CI check |
| Build files, packaging, `tools/scripts/smlib_build.py`, `repo.toml` | `cmake --build _build/cam-tools --config Release --parallel 4` | `ctest --test-dir _build/cam-tools -C Release --output-on-failure` |

## SmPyLib Focus Commands

The CMake validation build registers the stable Python suite:

```shell
ctest --test-dir _build/cam-tools -C Release --output-on-failure -R python_geometry
```

For a focused target or debugger tests, use `tools/agent/run_geometry_tests.py`
with the selected runtime directory from README.md. For example, on Windows:

```shell
python tools/agent/run_geometry_tests.py --runtime _build/cam-tools/bin/Release --target test_omni_solid.TestBooleans
python tools/agent/run_geometry_tests.py --runtime _build/cam-tools/bin/Release --dev
```

On Linux, `tools/agent/run_smpylib_tests.sh` remains a focused convenience wrapper.
Set `SMLIB_RUNTIME_DIR` for CMake output and pass `--dev` to include dev tests.

## prog_test Focus Commands

`prog_test_app` supports focused C++ kernel regression suites:

```bash
./tests/prog_test/prog_test.sh linux-x86_64 --list-suites
./tests/prog_test/prog_test.sh linux-x86_64 --suite booleans
./tests/prog_test/prog_test.sh linux-x86_64 --suite fillets
./tests/prog_test/prog_test.sh linux-x86_64 --suite tessellation
./tests/prog_test/prog_test.sh linux-x86_64 --suite healing
```

For changes to `prog_test` itself, do not rely on process success alone. The
historical validation signal is the generated debug output logs. Build the debug
app, run the default suite from `tests/prog_test` on trunk and the branch, then
compare the generated `OutputFiles/progTest_*_long.txt` and `progTest_*.txt`
logs. Treat pointer addresses, wall-clock timings, and tiny last-digit floating
point drift as volatile; added, removed, reordered, or renamed test output is a
real behavior change unless it was intended. Full debug logs are noisy enough
that broad geometry, polygon-output, or warning/message ordering diffs need a
control check: rerun the same branch or compare focused suites before
attributing the difference to the code change.

Use these defaults for obvious `source/SMLib/` changes:

| Change area | Focused suite |
|---|---|
| merge, booleans, classification | `booleans` |
| fillet, blend, chamfer | `fillets`, then `local-ops` |
| offset, shell | `offset` |
| sweeps | `sweeps` |
| primitive creation, analytic primitives | `primitives` |
| tessellation, polybrep, polygon operations | `tessellation` |
| healer stages, repair properties, repeated healing | `healing`; add `topology` for classification or mutation tracking |
| topology, trimming, BRep import | `topology`, `healing`, `trimmed-surfaces`, `brep-import` |
| SSI / surface intersectors | `ssi-analytic`, `ssi-advanced` |
| CCI / curve intersectors | `cci-advanced` |
| section / silhouette | `section-advanced`, `silhouette-advanced` |

`topology` includes `smFaceTest.cpp` classification and derived-UV tests as well as
the direct topology tests in `smTopoTest.cpp`. `healing` runs `smHealTest.cpp` and
is included in the default run immediately after `topology`. Keep each standalone
suite independent; do not rely on another suite to prepare its fixtures. See
[prog_test organization](../../../tests/prog_test/README.md) for placement guidance.

## Troubleshooting

- If a focused test cannot import `_omni_solid`, run `cmake --build _build/cam-tools --config Release --parallel 4` or report the missing build output.
- If `prog_test` output differs broadly, rerun a focused suite or compare against a same-branch control before assigning causality.
- If CI failed outside the changed layer, inspect logs before expanding to full `ctest --test-dir _build/cam-tools -C Release --output-on-failure`.
- If docs or `.agents/` changed only, prefer path/format validation and report that kernel tests were not run.

## Limitations

- Do not use this skill to design the code change itself.
- Do not run broad tests by default when a focused validation covers the changed behavior.

## Selection Rules

- Start with the smallest test that exercises the changed behavior.
- Add broader tests when the change touches shared ownership, tolerance handling, public API signatures, build files, or object lifetime.
- If a test cannot run because the build output is missing, run `cmake --build _build/cam-tools --config Release --parallel 4` before declaring the test blocked.
- Always report tests that were not run and why.

For build options and runtime locations use README.md. Native CMake validation
requires `SMLIB_BUILD_VALIDATION=ON`; dev bindings require `SMLIB_BUILD_DEV=ON`.
