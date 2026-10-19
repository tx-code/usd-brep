# Face property integration controls

`face_property_integration` exercises the real SDK at the unchanged `1e-4`
requested relative accuracy, plus the API's default `1e-3` and loose `1e-1`
requests. Its independent planar annulus reference is
`pi*(R*R-r*r)`. The thin `R=30,r=29` case exposes an adaptive step that crosses
UV B-spline knots and returns an inaccurate area. Translation and rigid rotation
also check the area and its physical centroid.

The private partition checks use actual polynomial C0 knots, clipped and shifted
native intervals, and a short positive span. A positive rational C0 curve with a
repeated knot also exercises reversal in its actual parameter domain. They verify the current span's
one-sided derivative at each endpoint, the unit-interval Jacobian, exact knot
deduplication, conservative allocation of the existing numerical control
fraction, and fallback for a registered derived evaluator.

Plane, sphere and torus queries cover polynomial, polar and periodic controls;
the curved controls retain the existing native API suite's `1e-3` comparison
budget. Invalid property masks must leave outputs unchanged. The broader
`sm_api` CTest checks property-group equivalence, independent reference frames,
offset surfaces and failure propagation.

These comparisons establish their synthetic regression scope. Requested
accuracy and adaptive error estimates are not certified continuous error
bounds. No private model or per-run performance receipt belongs here.
