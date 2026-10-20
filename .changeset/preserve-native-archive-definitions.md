---
change: fix
---

STEP native archive verification preserves stored surface and curve classes and UV parameters instead of running analytic recognition or version-triggered healing. `SmNativeArchive` provides explicit file and database readers with complete-part publication and empty outputs on failure.

## Scope

[Native STEP import contract](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild and link consumers of `SmNativeArchive` and STEP archive verification with the updated SMLib SDK. Keep returned objects alive only within their associated context lifetime and run the applicable native and definition checks after reading.

## Compatibility

Adds C++ reader symbols without changing object layouts or native file formats. Legacy `SmBrepData` and `SmApiReadPartFromFile` readers retain analytic recognition and healing behavior. Native archives do not carry STEP source qualification or repair authorization.
