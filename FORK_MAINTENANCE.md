# Maintaining this fork

Fork: <https://github.com/tx-code/usd-brep>

Upstream: <https://github.com/NVIDIA-Omniverse/usd-brep>

Use `origin` for this fork and `upstream` for the original repository. Keep
`main` as an upstream reference and maintain local enhancements on `maintained`.
Feature/fix branches retain independently reviewable changes. Do not force-push
the maintained branch. Before merging upstream changes, run focused regressions
and the repository's normal tests in a suitable build environment.

Current patch sequence:

1. `fix/assertvalid-seam-ownership`: seam-classifier lifetime regression and fix.
2. `refactor/classification-copy-ownership`: shared classification copy rules,
   context fallback, internal aliases, and curve-on-surface ownership fixes.
3. `feature/planar-regions`: general planar boundary-region construction and
   regression integration. No USD removal or STEP importer is included.

The copy-ownership tests currently use their independent CMake/CTest entry point
at `tests/assertvalid_ownership`. The planar-region suite is in the normal
Premake build and `tests.bat` / `tests.sh`, with a CMake entry point as well.
Keep passing evidence tied to exact commits, toolchains and runtime libraries.
Do not claim a full CI gate from a focused local test run. Hosted CI provisioning
is a separate task; no successful hosted CI run is implied by this document.

Preserve upstream copyright/license notices and the applicable third-party
license materials in distributions. Record local modifications and keep their
regressions with the code. Public reproductions must use synthetic models.
