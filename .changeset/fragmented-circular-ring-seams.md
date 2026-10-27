---
change: fix
---

Native STEP general construction supports cylindrical bands whose circular
boundary rings contain multiple direct CIRCLE edges. It retains the original
vertices and curve definitions, shares necessary arc subdivisions across adjacent
faces, and records the derived meridian and complete source-use correspondence.
Geometric quality, cancellation and complete-publication checks still apply.

A closed circular ring can also bound a cylindrical face whose opposite boundary
contains lines and B-splines. A bounded private survey identifies one meridian
connection to an original vertex or circular-arc interior before any real face is
constructed. It uses retained pre-snap encounter parameters, preserves complete
definitions, and shares all necessary arc children across their original uses.
Ambiguous classifications and unsupported intersection families remain refused.

Finite conical rings can place their computational seam away from existing source
vertices. Original points and defining geometry remain unchanged. Newly introduced
seam vertices use the existing connection-consistency allowance, separately from
the unchanged source-circle definition accuracy; final residuals and budgets stay
in the owning snapshot and mandatory report. Native seam and validity checks still
run, and no repair or tolerance growth is introduced.

The fixed AP214 source catalogs also retain `VERTEX_LOOP` point boundaries and
check the required advanced-face vertex/loop types. A complete conical circle
and a declared apex can form a native cap through one derived straight seam,
used in opposite directions at the original vertex. The owning topology
correspondence records the original point-boundary use and target cut; other
point-boundary constructions remain unsupported until qualified.

Applications can separately request source-bound conical apex vertex correction
with explicit individual/cumulative movement and proof budgets. A retained strict
failure, unique actual native pole and conservative 1% local feature protection
must be established before an independent candidate is built. All native and
publication checks still run; source definitions, unrelated vertices and working
tolerances are compared against the original candidate. Successful results are
`AcceptedWithRepairs` with owning original/corrected-point history and a complete
request budget. Default `Repair=None` and all geometric acceptance budgets remain
unchanged. Native CLI and viewer carry the explicit intent and verify the repair
receipt before scene adoption.

## Scope

[Circular ring construction](../source/STEP_IMPORT/README.md#完整圆环的目标分段与圆柱圆锥图切口).

## Integrator action

Rebuild the native STEP library and its consumers together.

## Compatibility

The C++20 native STEP topology correspondence adds `vertexLoopCuts`; options,
reports and snapshots add explicit conical-apex repair requests and owning history.
Rebuild the library and its C++ consumers together. Kernel ABI and native-file
format are unchanged. Ordinary normalization retains `Repair=None`; the new repair
requires separate explicit intent and returns its distinct qualification. Source
representations and physical acceptance budgets remain unchanged. Reports add
point-boundary correspondence or repair evidence only for the applicable request.
