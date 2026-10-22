---
change: feature
---

Native STEP file/document import and the standalone viewer support explicit,
source-hash-bound PRODUCT_DEFINITION selection after complete source checks.
Selected definitions publish atomically; unselected native failures do not cause
an implicit fallback or acquire native acceptance. Owning snapshots, reports and
the viewer preserve the selection scope.

## Scope

[Native STEP selection contract](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild the native importer, CLI/viewer worker and C++ consumers together. Retain
the selection receipt when exporting a selected native document.

## Compatibility

Internal C++ Options/Report/DocumentSnapshot layouts and execution phases expand.
Existing requests without selection keep their results and report bytes. Selected
single definitions return a document alternative. Kernel ABI and native archive
format are unchanged. Source errors remain whole-file failures; this does not
add syntax recovery, assembly selection or production quality defaults.
