---
change: feature
---

STEP inspection and explicit general interoperability can retain unknown-encoding
FILE_NAME text using `--preserve-opaque-file-name-text`, with original bytes and
per-value diagnostic paths. The prior filename-only policy remains unchanged.

## Scope

[Source reader](../source/STEP_SOURCE/README.md) and [native importer](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild consumers for the new HeaderNamePolicy enumerator. Native import also
requires explicit general/nongeometric interoperability. Choose one header policy.

## Compatibility

Existing defaults, field shapes, limits, schema/carrier selection and DATA policies
are unchanged. No code page or metadata value is invented. This does not add
general syntax recovery, partial-model publication or geometry repair.
