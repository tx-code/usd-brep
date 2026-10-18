# Documentation

## Read by task

| Need | Owner |
|---|---|
| Build, install, package, or run the SDK | [Repository README](../README.md) |
| Contribution and formatting expectations | [Contributing](../CONTRIBUTING.md) |
| Current limitations | [Known issues](../KNOWN_ISSUES.md) |
| Modeling vocabulary | [Glossary](../GLOSSARY.md) |
| Operation usage and debugging | [Operation index](../.agents/operations/INDEX.md) |
| Planar-region API | [Planar regions](planar-regions.md) |
| STEP architecture and durable constraints | [STEP design](design/step-import.md) |
| Implemented STEP profiles, options, and limits | [STEP_IMPORT](../source/STEP_IMPORT/README.md), then the owning layer's README |
| Selected compile-time catalog design | [STEP catalog](design/step-catalog.md) |
| Release preparation | [Release process](release/RELEASE_PROCESS.md) |
| Upstream and fork maintenance | [Fork maintenance](../FORK_MAINTENANCE.md) |

## What belongs in Git

Keep user/developer guidance, current contracts, durable design decisions,
reusable tests/tools, required provenance/license materials, and release records.
Place a rule with its owner: module behavior in the module README or nearby code,
operation guidance in `.agents/operations/`, and cross-module design in `docs/design/`.
Link to that owner instead of copying build commands, API tables, or status lists.

Plans, session tickets, handoffs, investigation diaries, per-machine paths,
execution receipts, raw benchmark output, source inventories, and one-off probes
belong under ignored `.local/` directories. Promote a result into documentation
only when it establishes a durable contract or decision; retain its reusable
verification in the appropriate test/tool directory. Local run counts do not
become permanent capability claims.

Frozen source artifacts required by regression tools belong beside those tools.
The [AP203e2 fixture documents](../tools/step_import_validation/contracts/README.md)
are immutable test inputs, not current guidance; their historical links and wording
are part of the captured bytes. Preserve them and their versioned manifests.

Use `.changeset/` for pending caller-visible changes, `CHANGELOG.md` for published
engineering history, and root `RELEASE_NOTES.md` when preparing an actual release.
Avoid parallel ticket/acceptance archives inside published documentation.
