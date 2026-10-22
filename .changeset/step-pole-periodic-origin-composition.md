---
change: fix
---

General STEP import preserves pending spherical-pole origins through an explicit
single-face periodic normalization. Unused periodic requests retain prior evidence;
actual splits rebuild target indices, verify pole positions and preserve checked
source-edge interval coverage instead of reusing pre-split edge handles.

## Scope

[Native STEP import](../source/STEP_IMPORT/README.md). Previously completed or
reordered inventories are refused before candidate detection. Composition with
support-domain restriction remains unqualified.

## Integrator action

Rebuild STEP consumers. No new option or report field is introduced.

## Compatibility

No kernel ABI or archive change. Source definitions, repair policy and quality
budgets remain unchanged; failure/cancellation never publishes a partial model.
