---
change: feature
---

STEP inspection recognizes the published AP242 edition 1–4 identifiers with
exact registry identities. It retains the original name/OID spelling and does
not infer an edition from an unregistered or name-only declaration. A separate
AP242 Ed.2 transport catalog retains its source-specific field differences;
source and native AP242 import remain unsupported pending their own checks.

## Scope

[Source schema identity and catalog](../source/STEP_SEMANTICS/README.md).

## Integrator action

Rebuild matching semantic libraries and consumers. Treat a recognized schema
identity separately from a qualified source-check profile; no import option is
enabled by this change.

## Compatibility

ProfileKind appends a catalog-only AP242 value. Registry diagnostics distinguish
published AP242 versions from unregistered versions; existing qualified profiles,
strict admission, geometric budgets and repair defaults retain their behavior.
