# STEP import documentation

## Current contracts

- [STEP_SOURCE](../../source/STEP_SOURCE/README.md): captured bytes, Part21 input and report/location contracts.
- [STEP_SEMANTICS](../../source/STEP_SEMANTICS/README.md): schema/profile selection, typed fields and source queries.
- [STEP_PLANAR](../../source/STEP_PLANAR/README.md): qualified planar source checks and exact numeric limits.
- [STEP_IMPORT](../../source/STEP_IMPORT/README.md): implemented native models, options, quality and ownership.
- [Long-term design](../design/step-import.md): completeness, normalization, repair, publication and verification.
- [Catalog design](../design/step-catalog.md): the selected compile-time metadata migration and acceptance requirements.

The qualified planar profile and opt-in general BRep development path have
separate capabilities and evidence. Component READMEs describe their current
limits; a design, schema label, or native validator pass does not grant broader
support.

## Reusable verification

- [Fixture construction and independent truth](../../tools/step_import_validation/README.md).
- [Source checks](../../tools/step_inspect_validation/README.md).
- [Semantic checks and declaration witnesses](../../tools/step_semantic_validation/README.md).
- [Planar source checks](../../tools/step_planar_validation/README.md).
- [Native import checks](../../tools/step_import_native_validation/README.md).
- [Evidence package protocol](../../tools/step_evidence/README.md).
- [Profiling and model cohorts](../../tools/step_import_benchmark/README.md).

The original AP203e2 profile, rule/field tables, sample records, and mathematical
anchors are [versioned fixture inputs](../../tools/step_import_validation/contracts/README.md).
Their bytes remain frozen; historical wording and links are not current guidance.
Session tickets, implementation diaries, execution receipts and raw measurements
belong under ignored `.local/` paths, following [documentation placement](../README.md).
