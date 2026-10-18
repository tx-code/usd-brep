<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# SMLib geometry SDK

This fork provides a BRep geometry foundation for CAM applications: SMLib,
SM_API, native STEP import, and Python bindings. OpenUSD, Hydra, BrepArray
schemas and the USD-based OCCT converters have been removed. Repository and
Python package names remain `usd-brep` / `usd_brep` for compatibility.

## Build it

CMake is the build system for all SDK, development, validation, and example
targets. Use CMake 3.24 or newer, a C++20 compiler, and Python 3.12 with development
headers plus pybind11 2.11.1. On Windows, install the Visual Studio C++ workload
and Windows SDK. No scene-format SDK or TBB runtime is required.
Microsoft VC++ Redistributable is needed on Windows runtime-only machines.

CMake reuses Python and pybind11 under `_build/target-deps` when available.
Fetch these with `repo.bat fetch` (`./repo.sh fetch` on Linux), without compiler
discovery. For caller-supplied dependencies use `-DPython3_ROOT_DIR=...` and
`-Dpybind11_DIR=...`. macOS uses a host Python 3.12; set `SMLIB_PYTHON_ROOT` for
the wrappers or pass `Python3_ROOT_DIR` directly to CMake.

```shell
cmake -S . -B _build/cam
cmake --build _build/cam --config Release --parallel 4
```

Default targets: `SMLib`, `SM_API`, `STEP_SOURCE`, `STEP_SEMANTICS`, `STEP_PLANAR`,
`STEP_IMPORT`, and `_omni_solid`. Use `-DSMLIB_BUILD_PYTHON=OFF` for a native-only
SDK. Multi-config generators put runtime files in `_build/cam/bin/Release`;
single-config generators use `_build/cam/bin` (set `-DCMAKE_BUILD_TYPE=Release`).
STEP static libraries are under `lib/` and the corresponding configuration.

The shell wrappers and `repo build` use the same CMake driver:

```shell
build.bat --fetch
prebuild.bat --dev --validation --examples
build.bat --dev --validation --examples
```

Use `./build.sh` and `./prebuild.sh` on Linux/macOS. `prebuild` configures only;
`build` configures, builds, then stages the production SDK in `_build/install/release`.
The default configuration is Release (`-d` selects Debug); `--build-dir` or
`SMLIB_CMAKE_BUILD_DIR` selects a different CMake tree. Feature flags omitted on
an existing tree retain its cached settings. `--target NAME` builds a selected
target without staging an incomplete SDK. Run `build.bat --help` for other options.

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
existing test defaults. Wrapper equivalents are `--dev`, `--validation`, and
`--examples`. `tests.bat` / `./tests.sh` run CTest in the selected CMake tree;
pass `--build-dir _build/cam-tools` for the independent tree. `--dev` additionally
requires that developer targets were enabled; CTest runs their suite when built.

GUI Python dependencies are separate from the SDK. Set `SMLIB_RUNTIME_DIR` to
the dev runtime directory, or `SMLIB_CMAKE_BUILD_DIR` to its CMake build tree:

```shell
uv run --locked --group gui python tools/scripts/smlib_gui.py --release
uv run --locked --group gui python tools/scripts/smlib_gui_smoke.py
```

The GUI loads `.smb` / `.smp` native files and runs geometry scripts. Native
STEP import remains a C++ library/optional CLI; see
[STEP_IMPORT](source/STEP_IMPORT/README.md) for supported profiles and limitations.

## Python and packaging

Set `PYTHONPATH` to the runtime directory above, then:

```python
import usd_brep as sm
box = sm.create_box((0, 0, 0), 10, 20, 30)
print(box.volume())
mesh = sm.tessellate(box)
points = mesh.to_mesh_arrays()
```

The compiled `_omni_solid` name is retained. The `usd` submodule and schema plugin
initialization are removed. Copy inputs when preserving them through consuming
operations; follow each operation's ownership and mutation contract.

`repo.bat py_package -c release` builds a wheel from the selected CMake runtime.
For an explicit runtime use `--build-dir _build/cam-tools/bin/Release`. Packaging
rejects extensions that still link the retired scene runtime.

`repo.bat package -c release` archives `_build/install/release`, staged by a full
wrapper build. Direct CMake users can stage the same layout with:

```shell
cmake --install _build/cam --config Release --prefix _build/install/release
```

The SDK archive contains production libraries, headers, Python bindings when
enabled, and license materials. Development modules, tests, and example binaries
stay in the build tree. Use `python/` on `PYTHONPATH` and `lib/` on the native
library search path when consuming an installed SDK.

## Maintenance and license

See [fork maintenance](FORK_MAINTENANCE.md), [planar regions](docs/planar-regions.md),
and [agent guidance](AGENTS.md). Upstream copyright/license notices remain intact.
SMLib is Apache-2.0; see [LICENSE](LICENSE) and
[third-party notices](THIRD_PARTY_NOTICES.md).
