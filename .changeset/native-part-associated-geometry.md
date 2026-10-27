---
change: feature
---

Native STEP material collections retain linked wireframe and constructive
geometry once at part level. Owning collection views expose those roles without
attaching them to an arbitrary body; documents retain each definition's own
associated data. Single-model and collection paths share source bindings,
construction and per-item checks. Incomplete requests publish no partial result.

Collection/document native archives include all detached curves/surfaces and
explicit point adapters, with complete readback checks. Viewer v9/v10 scenes
require associated payloads and source-owner identities before scene adoption;
older viewers reject these scene versions. Source bytes, geometry, role semantics,
physical budgets and default repair selection retain their contracts.

## Scope

[Part-level associated geometry](../source/STEP_IMPORT/README.md#多实体零件的附属几何).

## Integrator action

Rebuild native STEP libraries and consumers together. Read the complete native
part arrays: ordered material BReps precede detached point adapters, and archive
qualification/source identities remain in the owning STEP snapshot. Deploy the
display worker and viewer scene readers together.

## Compatibility

The C++ interface adds owning collection queries; internal candidate/ownership
layouts change. Existing body-only scene versions and source/catalog identities
are retained. Native archives with associated geometry contain additional arrays
and point adapters within the existing native-file format. Assembly occurrence
transport and repair combinations for collection-associated geometry remain
unsupported rather than silently dropping required content.
