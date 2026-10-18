# Maintaining this fork

Fork: <https://github.com/tx-code/usd-brep>

Upstream: <https://github.com/NVIDIA-Omniverse/usd-brep>

Use `origin` for this fork and `upstream` for the original repository.
`origin/main` is the long-lived development and default branch, containing our
fixes and extensions. Track the original project through `upstream/main`; do not
maintain a second local development branch just to mirror upstream.

Use temporary feature/fix branches for development and review, then delete them
after their commits are merged into `main`. Do not force-push `main`. Create a
release branch only when a shipped version needs independent maintenance; use
release tags to identify published versions. Keep deployment branches, such as
`gh-pages`, separate from this development-branch cleanup policy.

Before merging upstream changes, run focused regressions and the repository's
normal tests in a suitable build environment. Record the upstream commit merged,
local patch commits and purpose, tests/toolchain/results, release tags, and any
patches subsequently absorbed upstream. Deleting merged branches does not delete
their commits or regression history.

Last incorporated upstream commit: `c9c5979f04cea8c38213004d4f04df0dcfb40d05`
(Release 1.0.0).

Local patch sequence (recorded by commit, independent of temporary branch names):

1. `f169b722918eefa62ba3dddab4eccf980c2f450c`: seam-classifier lifetime regression and fix.
2. `5c0f4b9398d4e6af2538767da37575304a7f1aec`: shared classification copy rules,
   context fallback, internal aliases, and curve-on-surface ownership fixes.
3. `2dd87a031ae2a6671b421b7a3e21ccba66abec56`: general planar boundary-region construction and
   regression integration. No USD removal or STEP importer is included.

The copy-ownership tests currently use their independent CMake/CTest entry point
at `tests/assertvalid_ownership`. The planar-region suite is in the root
CMake validation build and runs through CTest and `tests.bat` / `tests.sh`.
Keep passing evidence tied to exact commits, toolchains and runtime libraries.
Do not claim a full CI gate from a focused local test run. Hosted CI provisioning
is a separate task; no successful hosted CI run is implied by this document.

Preserve upstream copyright/license notices and the applicable third-party
license materials in distributions. Record local modifications and keep their
regressions with the code. Public reproductions must use synthetic models.

## Geometry SDK cleanup

The USD/Hydra/schema/OCCT-USD stack is retired. Python keeps its historical
package name; development tools and validation are opt-in (see README.md).
The implementation is recorded by these commits:

1. `83b82355d87f83e19ce2b569ea9bf55677e8c77e`: geometry SDK / Python decoupling and independent build targets.
2. `834d6a13323074af6d4d8b98947a0bff600ecdcc`: native GUI runtime and removal of scene-format workflows.
3. `ffbb918ea99f6e013237a74d4aa981ee73c1f4c7`: retired integration libraries, converters, validators and resources.

Local Windows x64 MSVC 19.50 Release validation built the SDK and optional
targets; 48 CTest cases passed. Python ran 380 cases (2 skipped), and debugger
bindings ran 22 cases (1 skipped), with no failures. The native GUI module
loading check passed. Full GUI smoke validation remains incomplete because
the locked Qt/VTK dependency downloads did not finish.

These runs used a working tree containing separate, uncommitted STEP semantic
changes. They are local integration evidence for that tree, rather than a
clean-checkout or hosted-CI gate for the cleanup commits alone.
