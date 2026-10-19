---
change: fix
---

Surface point solving reuses available evaluations when copying an iterate at
the same parameters. This reduces repeated surface evaluation during native
geometry queries and STEP validation, while retaining the requested precision
and the selected checks.

## Scope

Maintenance

## Integrator action

None.

## Compatibility

C++ signatures, public class layouts, parameter domains and ownership are
unchanged. Reuse stays within the existing local solve. Native STEP quality,
complete reports, cancellation and publication contracts are unchanged.
