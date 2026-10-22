---
change: feature
---

The standalone viewer displays complete native material collections and labels
face/edge/vertex measurements with body ownership. Canonical ImportPartFile/Bytes
reads and checks source once, preserves single-body associated geometry, and
atomically publishes a typed single/collection result. Viewer payload adoption
requires every body; geometry points and topology identities remain separate.

## Scope

[Viewer collections](../tools/smlib_viewer/README.md#material-collections).

## Integrator action

Rebuild STEP consumers and distribute matching worker/viewer modules. Recognize
PartOutcome and scene v4; collections use shared whole-scene limits and report
encoding CompactV2. Existing single and collection SDK entry points remain available.

## Compatibility

Scene v1/v2/v3 and single-body workflows remain supported. The scene v4 body-local
labels are remapped into unique display IDs without merging points. Source
interpretations, geometry budgets and refusal of unsupported required content stay
unchanged; displayed geometry does not establish additional import qualification.
