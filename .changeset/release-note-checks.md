---
change: feature
---

Maintainers can check pending release notes with
`python tools/agent/changesets.py check`, without building the SDK or installing
additional Python packages. Validation-enabled CTest runs the note check and
its contract tests. Notes describe observable changes, integrator actions, and
compatibility impact; they do not select or bump versions.

## Scope

Maintenance; [pending release notes](README.md) and
[release preparation](../docs/release/RELEASE_PROCESS.md).

## Integrator action

Include a pending note with caller-visible fixes, features, or compatibility
changes. Native-only builds that enable `SMLIB_BUILD_VALIDATION` now need a host
Python 3.10+ interpreter for these checks.

## Compatibility

The default SDK build and C++/Python modeling APIs are unchanged. The additional
interpreter requirement applies to optional native-only validation. Existing
published changelog entries, native formats, and STEP qualification are unchanged.
