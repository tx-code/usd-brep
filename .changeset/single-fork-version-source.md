---
change: fix
---

CMake reads the SDK numeric version from the root VERSION file instead of an independent
literal. Wheel packaging derives alpha, beta and release-candidate versions from that same
full fork version, while preserving the documented legacy packaging channel conversions.

## Scope

[SDK release preparation](../docs/release/RELEASE_PROCESS.md).

## Integrator action

Reconfigure existing CMake build trees. Use VERSION as the release identity; the CMake
project version is its numeric core and the wheel version uses Python's prerelease spelling.

## Compatibility

Build and package metadata change. Geometry, native layouts, Python operation signatures,
archive format and STEP qualification contracts are unchanged.
