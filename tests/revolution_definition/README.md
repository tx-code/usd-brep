# Revolution defining geometry

`revolution_definition` exercises public `SmSurfOfRevolution::CreateCanonical`,
ordinary and STEP evaluation, actual parameter conversion, explicit sweep
parameterization, virtual Copy, and native binary/text archives. It requires no
STEP reader, external CAD file, mesh, repair or tolerance modification.

The original regression is a degree-one generator from `(1,0,1024)` to
`(2^-30,0,1025)`, with source knots `[-2,-2,8,8]`, revolved about +Z. At STEP
`(180,8)`, the independent point has X `-2^-30`. The old NURBS constructor repeated
the endpoint for its entire sweep row, returning X `+2^-30`. A budget relative to
the radial dimension prevents the large axial coordinate from hiding this error.

Controls cover polynomial and rational generators, nonunit source parameters,
swapped NURBS U/V, small first/last rows, all-small generators within the existing
factory's supported range, exact zero-radius endpoints, translated/scaled-coordinate
frames, oblique axes, and ordinary radii. A `2^-44` row exercises construction below
the circle factory's existing radius floor. Independent rational interpolation
and cardinal rotations check points, retained coefficients/knots, and generator
parameter round trips. Copy repeats these checks. Explicit sweep knots use `[-7,5]`;
a partial `[45,225]` degree sweep is also checked through its actual polar map.

Binary archive generator coefficients remain exact. The existing text geometry
codec uses 16 decimal places, so its controls retain the existing `1e-16` absolute
rounding budget; this does not change product precision. The radial point budget
still distinguishes a nonzero circle from a collapsed endpoint. Oblique frames
add a fixed numerical coordinate budget for floating frame construction.

The test deliberately does not require a near-pole surface to pass LEVEL2 at the
default zone. Geometric agreement is separate from the existing degeneracy/quality
policy, which remains active. It does not certify arbitrary revolution surfaces,
continuous source/native deviation, or complete STEP import correctness.

Enable the root SDK validation configuration, then run:

```shell
cmake --build <build> --config Release --target revolution_definition
ctest --test-dir <build> -C Release --output-on-failure -R '^revolution_definition$'
```

Use the matching Debug target/runtime for Debug. Complete STEP import, reports,
measurements, cancellation and publication are covered by the existing native
revolution, pole, projection and chart contract suites, in addition to the broader
SDK checks selected for this shared geometry-construction change.
