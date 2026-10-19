# Surface point solver controls

Build the validation-enabled SDK as described in the root README, then run:

```shell
cmake --build _build/cam-tools --config Release --target surface_point_solve
ctest --test-dir _build/cam-tools -C Release --output-on-failure -R '^surface_point_solve$'
```

The test calls the real local solver through a registered derived B-spline
surface that records physical evaluations and forwards to the original NURBS
evaluator. An affine plane requires only its initial and proposed evaluations
for one exact Newton step; the regression detects reevaluation caused by losing
an available copied iterate. Repeated requests must still evaluate independently.

Geometry controls cover minimize, normalize and intersect requests; actual
shifted and clipped domains; polynomial and positive rational surfaces; a C0
knot; periodic seams and poles; and evaluator interruption followed by a fresh
request. Independent plane answers and points on the defining surfaces check
geometry separately from evaluation counts. Existing STEP integral/lifecycle
contracts own cancellation, complete reports and atomic publication checks.

These synthetic controls do not qualify every input, numerical conditioning or
arbitrary malformed evaluator. Timing and private model inventories remain local.
