---
change: fix
---

The synthetic STEP corpus generator reads its frozen design documents from
`tools/step_import_validation/contracts/design/v1/`, independently of current
documentation. The original document bytes, STEP inputs, and independent
mathematical definitions are retained.

## Scope

Maintenance; [validation inputs](../tools/step_import_validation/contracts/README.md)
and [documentation placement](../docs/README.md).

## Integrator action

Use the versioned contract directory when inspecting generator input documents.
Use module READMEs and `docs/design/` for current guidance. Session plans and
per-machine execution evidence belong under ignored `.local/` directories.

## Compatibility

Newly generated source manifests identify the new document locations. Existing
frozen corpus/evidence packages retain their recorded paths and hashes; offline
verification is unchanged. Geometry SDK APIs and native serialization are unchanged.
