# Conic bounds integration controls

These public synthetic STEP inputs freeze the bytes returned by
`cylinder_source()` and `bulged_box_source()` in
[`test_general.py`](../../../../tools/step_import_native_validation/tests/test_general.py).
They contain no external model data. Regenerate with those functions; do not
replace the geometry with a rendered or tessellated approximation.

`conic_bounds_cylinder.step` has radius 3 mm and height 7 mm: area `60*pi` mm²,
volume `63*pi` mm³, bounds `[-3,-3,0]..[3,3,7]`. Unique boundary curves have
total length `12*pi+7` mm, including the shared seam once.

`conic_bounds_bulged_box.step` has an exact quadratic top patch
`z=35+80*u*(1-u)*v*(1-v)` over a 10 by 20 mm rectangle. All boundary curves
remain below or at 35 mm, while the surface reaches 40 mm internally. Its volume
is `6000+4000/9` mm³. This control rejects a bounds shortcut that omits the
independent surface-interior extrema search.

The `step_import_conic_bounds_contracts` CTest uses the explicit development
quality configuration. It checks independent geometry, complete source/report
retention, per-face observations, late cancellation and native/report limits.
It does not establish general source conformance or production qualification.
