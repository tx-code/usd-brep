<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Contributing

USD BRep is currently not accepting external code contributions. Issues and
bug reports are welcome through the project issue tracker. Security issues
should be reported using `SECURITY.md`, not public issues.

This guide records the development expectations for maintainers and approved
contributors.

## Reporting Bugs

Check [KNOWN_ISSUES.md](KNOWN_ISSUES.md) and the
[public issue tracker](https://github.com/NVIDIA-Omniverse/usd-brep/issues) first.
Verify the API contract using the [operation guides](.agents/operations/INDEX.md)
or owning STEP component contract. Validate live BRep inputs at the completed
boundary immediately before the failing operation, retaining pointer and
`AssertValid` findings. Include the tested revision, command, expected behavior,
observed diagnostics, and a minimal reproduction using this repository's APIs.
Use synthetic reproductions for public issues; private models and identifying
inventories stay local.

## Getting Started

Install the prerequisites listed in `README.md`, then build and test from the
repository root:

```shell
cmake -S . -B _build/cam-tools -DSMLIB_BUILD_DEV=ON -DSMLIB_BUILD_VALIDATION=ON
cmake --build _build/cam-tools --config Release --parallel 4
ctest --test-dir _build/cam-tools -C Release --output-on-failure
```

See `README.md` for standalone Python dependency paths and shared CMake wrapper
entry points. Packman fetches Python, pybind11, and repository tools.
A host C++ toolchain is required.

## Before Submitting Changes

Run the repository formatter on the affected files:

```bash
./repo.sh format
```

For code changes, run the relevant focused tests first, then run the full suite
when the change affects shared behavior:

```shell
ctest --test-dir _build/cam-tools -C Release --output-on-failure
```

If you change Python bindings, include tests under `source/SmPyLib/tests/`.
If you change C++ kernel, SM_API, or native STEP behavior, include focused
coverage near the affected layer where practical.

## Pending Release Notes

Include a [changeset](.changeset/README.md) with each caller-visible fix, feature,
or compatibility change, together with the code and affected API/usage docs.
Internal refactors and documentation-only edits need no note. Validate pending
notes without building the SDK:

```shell
python tools/agent/changesets.py check
```

Validation-enabled CTest also runs this check and its contract tests. Keep daily
changes in pending notes; the [release process](docs/release/RELEASE_PROCESS.md)
describes reviewing and consuming them into release records. Note categories do
not select or bump versions.

## Code Style

- C++ uses the repository `.clang-format` configuration.
- Python follows PEP 8 with the repository 120-column line limit.
- Keep changes scoped. Avoid unrelated refactors, generated-file churn, and
  formatting-only edits mixed into behavioral changes.
- Prefer existing helper APIs and local patterns over new abstractions.

## Operations And Bindings

SMLib modeling operations span the C++ kernel, the C-style C++ `SM_API`,
and the `_omni_solid` Python bindings. Native STEP import uses its own layered
source, semantics, planar validation, and construction libraries. When adding or changing an operation:

1. Keep the implementation in the appropriate layer (`source/SMLib/`,
   `source/SM_API/`, `source/STEP_*/`, or `source/SmPyLib/`).
2. Preserve the documented mutation semantics for Python bindings.
3. Add or update focused tests for the affected API surface.
4. Update the relevant operation guide under `.agents/operations/`.
5. Update public documentation or type stubs when user-visible behavior changes.

See `.agents/operations/INDEX.md` and `AGENTS.md` for the operation map,
layering notes, and binding conventions.

## Documentation

Use [the documentation index and placement rules](docs/README.md). Keep durable
contracts and usage guidance in the repository; put investigation notes, session
tickets, execution receipts, and raw measurements under ignored `.local/` paths.

Keep docs in sync with behavior. When adding or changing an operation, update
the relevant operation guide and any public documentation that describes its
arguments, defaults, mutation semantics, or safety constraints.

`KNOWN_ISSUES.md` lists known bugs that users can hit. When a change fixes one,
remove its entry and add a `CHANGELOG.md` entry in the same change.

## Community Standards

Follow `CODE_OF_CONDUCT.md` in all project interactions. Report vulnerabilities
according to `SECURITY.md`.
