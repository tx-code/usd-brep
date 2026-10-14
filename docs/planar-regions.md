# Planar regions

`SmApiPlanarRegions.h` provides general planar geometry operations. The module
has no dependency on a machining strategy, stock simulation, USD, or STEP.

## Interface

- `SmApiBuildPlanarRegions`: selected boundary curves to owned planar BRep faces.
- `SmApiSectionPlanarRegions`: read-only section query followed by the same builder.
- `SmPlanarRegionOptions`: explicit absolute distances in model units and work limits.
- `SmPlanarRegionReport`: diagnostics, ordered input-curve indices and directions,
  sampled loop coordinates, immediate parent, nesting depth, signed sample area,
  material-region and hole counts. Even depth is material; odd depth is a hole.

Inputs are borrowed and must remain alive for the duration of the call. They are
not consumed or edited. The output BRep owns cloned exact curves and can outlive
all inputs. Delete it normally. Start with a null output pointer; an existing
non-null pointer is rejected without being deleted. On ordinary failure there
is no partial result. Empty boundary input/section is successful with null output.
Report coordinates are in its returned right-handed frame relative to the
requested origin. Its sampled area is approximate, not the exact face area.

## Tolerances and conservative rejection

`joinTolerance` is the largest permitted endpoint gap. Each endpoint must have
exactly one mate; there is no transitive welding or scale-dependent widening.
Sample endpoints are snapped halfway across accepted gaps for the report only;
the resulting BRep retains cloned input curves and represents tolerated joins.

`planeTolerance` is checked with the native whole-curve plane predicate and
again on samples. `chordTolerance` controls native curve tessellation.
`separationTolerance + 2*chordTolerance + joinTolerance` is a conservative
uncertainty band for rejecting non-adjacent boundary segments. Narrow features
may require smaller tolerances. Crossing, touching, duplicate or branched
boundaries are rejected; the builder does not split or heal a sketch arrangement.

The existing native nested-face builder is reused. Its curve partition and
hole parentage are checked against the preflight result. A disagreement returns
`TopologyMismatch`, rather than silently accepting the native builder's more
permissive joining rules. A final `AssertValid` check rejects invalid output as
`InvalidRegion`. The old `SmApiCreatePlanarFaces` contract is unchanged.

`maxPoints` caps retained samples after native tessellation; it does not bound
allocations inside that tessellator. `maxPairTests` limits endpoint and segment
pair checks. No global tolerance setting is changed. This initial implementation
has quadratic pair checking and is intended for bounded contour collections.
As elsewhere in the SDK, calls using the global context are not newly guaranteed
to be concurrently reentrant by this module.

## Projection is not a footprint definition

`SmApiProjectBrepOntoPlane` returns visible linework, including possible creases
and blind-pocket contours. Those cannot automatically be interpreted as holes
in the object's footprint. Select the intended closed boundaries before calling
`SmApiBuildPlanarRegions`. The module constructs an even/odd planar region from
those boundaries; it does not decide which projected lines express a material
footprint, engraving contour, pocket region or other application intent.

## Validation

See `tests/planar_regions`. Routine repository tests now invoke that suite.
The initial local validation covers Windows x64 MSVC Release. Other platforms,
full repository tests and sanitizers require their normal build environments;
the existence of a test entry point is not evidence those runs have passed.
