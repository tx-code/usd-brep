# Annular solid integral control

`annular_solid.step` is an independently authored synthetic AP214 input. It
defines coaxial cylinders of radii 30 and 29 mm between z=0 and z=7, with two
annular planar caps. No external model geometry is included. The circles retain
their source identities, the inner cylinder has opposite face sense, and each
cylinder's explicit seam is used twice in its boundary loop.

The exact independent observations are:

- Each planar cap: `pi*(30²-29²) = 59*pi` mm².
- Outer cylindrical face: `2*pi*30*7 = 420*pi` mm².
- Inner cylindrical face: `2*pi*29*7 = 406*pi` mm².
- Total area: `944*pi` mm²; material volume: `413*pi` mm³.
- Volume centroid: `(0,0,3.5)` mm.

`step_import_integral_contracts` uses the explicit general development quality
and nongeometric interoperability policy. It checks analytic model/per-face
observations at the unchanged `1e-4` relative integration request, source bytes
and face identities, complete reports, cancellation at every charged native
boundary, native/report limits, and a clean replay after interruption. Requested
precision remains distinct from a certified global error bound; this synthetic
regression does not grant general STEP conformance or production qualification.
