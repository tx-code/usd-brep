<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# SMLib geometry SDK

This fork provides a BRep geometry foundation for CAM applications: SMLib,
SM_API, native STEP import, and Python bindings. OpenUSD, Hydra, BrepArray
schemas and the USD-based OCCT converters have been removed. Repository and
Python package names remain `usd-brep` / `usd_brep` for compatibility.

## Build it

All repository-owned libraries, bindings, examples, and validation tools use C++20. Use a C++20-capable compiler.


Use CMake 3.24 or newer, a C++20 compiler, and Python 3.12 with development
headers plus pybind11 2.11.1. On Windows, install the Visual Studio C++ workload
and Windows SDK. No scene-format SDK or TBB runtime is required by this build.
Microsoft VC++ Redistributable is needed on Windows runtime-only machines.

CMake reuses Python and pybind11 from `_build/target-deps` when already fetched.
To fetch these headers/runtime with the legacy repo tools, run
`repo.bat build --fetch-only -r` (`./repo.sh` on Linux).
For a standalone build, supply `-DPython3_ROOT_DIR=...` and `-Dpybind11_DIR=...`.

```shell
cmake -S . -B _build/cam
cmake --build _build/cam --config Release --parallel 4
```

Default targets: `SMLib`, `SM_API`, `STEP_SOURCE`, `STEP_SEMANTICS`, `STEP_PLANAR`,
`STEP_IMPORT`, and `_omni_solid`. Use `-DSMLIB_BUILD_PYTHON=OFF` for a native-only
SDK. Multi-config generators put runtime files in `_build/cam/bin/Release`;
single-config generators use `_build/cam/bin` (set `-DCMAKE_BUILD_TYPE=Release`).
STEP static libraries are under `lib/` and the corresponding configuration.

The legacy Premake/Packman entry point remains `repo.bat build -r` or
`./repo.sh build -r`, with output in `_build/<platform>/release`. Its Windows
configuration still targets VS 2019/v142; use CMake with newer host toolchains.
`prebuild` only generates projects/stages Python now; no schema generation occurs.

## Independent development and validation builds

Enable optional components explicitly, preferably in a separate build tree:

```shell
cmake -S . -B _build/cam-tools -DSMLIB_BUILD_DEV=ON -DSMLIB_BUILD_VALIDATION=ON
cmake --build _build/cam-tools --config Release --parallel 4
ctest --test-dir _build/cam-tools -C Release --output-on-failure
```

- `SMLIB_BUILD_DEV`: `_smlib_dev`, `_smlib_tests`, and their `prog_test` dependency.
- `SMLIB_BUILD_VALIDATION`: STEP inspection CLIs and contract tests, planar-region
  regression, SM_API runner, kernel regression runner, and Python tests in CTest.
- `SMLIB_BUILD_EXAMPLES`: native SDK examples.

These options default to OFF. Standalone STEP CMake subprojects retain their
existing test defaults. The Premake equivalents are environment variables
`SMLIB_WITH_DEV=1`, `SMLIB_WITH_VALIDATION=1`, and `SMLIB_WITH_EXAMPLES=1`.
`tests.bat` / `tests.sh <platform>` require a validation-enabled build; set
`SMLIB_RUNTIME_DIR` to use a CMake runtime directory. Add `--dev` to test dev bindings.

GUI Python dependencies are separate from the SDK. Set `SMLIB_RUNTIME_DIR` to
your dev runtime directory, then launch:

```shell
uv run --locked --group gui python tools/scripts/smlib_gui.py --release
uv run --locked --group gui python tools/scripts/smlib_gui_smoke.py
```

The GUI loads `.smb` / `.smp` native files and runs geometry scripts. Native
STEP import remains a C++ library/optional CLI; no new STEP Python binding is
implied. See [STEP_IMPORT](source/STEP_IMPORT/README.md) for its supported source
profiles and current development-path limitations.

## Python

Set `PYTHONPATH` to the runtime directory above, then:

```python
import usd_brep as sm
box = sm.create_box((0, 0, 0), 10, 20, 30)
print(box.volume())
mesh = sm.tessellate(box)
points = mesh.to_mesh_arrays()
```

The compiled `_omni_solid` name is also retained. The `usd` submodule and schema
plugin initialization are removed. Kernel objects follow existing mutation and
ownership contracts; copy inputs when preserving them through consuming operations.

To stage a wheel from CMake output, use `repo.bat py_package --build-dir
_build/cam/bin/Release` (or the corresponding runtime directory). Packaging rejects
extensions that still link the retired scene runtime.

## Maintenance and license

See [fork maintenance](FORK_MAINTENANCE.md), [planar regions](docs/planar-regions.md),
and [agent guidance](AGENTS.md). Upstream copyright/license notices remain intact.
SMLib is Apache-2.0; see [LICENSE](LICENSE) and
[third-party notices](THIRD_PARTY_NOTICES.md).
