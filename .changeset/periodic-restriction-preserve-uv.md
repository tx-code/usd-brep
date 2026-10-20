---
change: fix
---

Explicit periodic STEP normalization restricts cylinder/cone supports with their
original native parameters. It retains existing UV correspondence instead of
using the revolution trim operation that rebuilds the interior parameterization.

## Scope

[Native STEP periodic normalization](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild `STEP_IMPORT` and its C++ consumers. Review tight native work limits when
using periodic normalization: its additional extraction and analytic-helper
updates are charged and remain cancellable.

## Compatibility

Only the opt-in normalization path changes. Default modes, source definitions,
quality budgets and public C++ layouts stay unchanged. Native archives still
require their independent readback and definition checks; a successful
restriction does not establish archive or whole-model qualification.
