---
change: breaking
---

Build, configure, test, and native SDK staging use CMake through the shell
wrappers and retained Repo commands. Premake projects and their download
dependency are removed. SDK archives consume the installed production layout.

## Scope

Maintenance

## Integrator action

Use `repo fetch` for Python/pybind11 dependencies. Replace Premake generation
and `SMLIB_WITH_*` switches with CMake options or the wrapper flags `--dev`,
`--validation`, and `--examples`. Select custom trees with `--build-dir` or
`SMLIB_CMAKE_BUILD_DIR`; use `SMLIB_RUNTIME_DIR` for an explicit GUI runtime.

## Compatibility

The historical Python package/module names and modeling APIs remain available.
Native consumers must use matching compiler, configuration, CRT, and kernel
layout macros. New STEP evidence records `execution-dependencies.v3`; historical
v1/v2 inventories remain frozen for offline rechecks.
