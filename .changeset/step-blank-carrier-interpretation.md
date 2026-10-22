---
change: feature
---

STEP reading adds explicit BlankLevelAsCorrectedEncoding for empty or ASCII-space-only
FILE_DESCRIPTION implementation declarations. It retains the original source and
refusal as unverified carrier evidence. General native and viewer paths expose the
choice while keeping all required semantic/geometric checks and strict defaults.

## Scope

[Carrier interpretation](../source/STEP_SOURCE/README.md).

## Integrator action

Rebuild STEP consumers and use --interpret-blank-level-as-corrected only explicitly.
The viewer spelling is --carrier-interpretation BlankLevelAsCorrectedEncoding and
requires a matching worker. Applied native results retain interpreted qualification.

## Compatibility

Existing declared/level1 policies retain their exact admissible declarations.
The new option does not trim nonblank levels, guess a schema/code page, enable
lowercase hex, raise budgets or repair physical model definitions.
