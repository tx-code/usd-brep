# Polar conversion endpoint contracts

Synthetic cylinders and ellipses retain their declared angular intervals when a
NURBS endpoint lies close to the periodic seam. The tests check lazy conversion
table construction, surface Level 2 validity, reversed curve direction, swapped
surface parameters, analytic copies, and point evaluations against independent
trigonometric equations. Sampling here is not a continuous geometric certificate.
Rebuilding the conversion table must retain every stored NURBS control
coefficient, weight and knot exactly.

The endpoint literals are constructed independently. Computing the second bound
as `start + sweep` can round away the failing configuration. A cache must not use
point-classification seam tolerance to change the real angular footprint of its
unchanged NURBS curve. Existing kernel tolerances remain unchanged.

The validation-enabled root SDK build registers `polar_conversion_endpoints`.
A matching standalone SDK can also be used:

```powershell
cmake -S tests/polar_conversion -B <build> -DSMLIB_LIBRARY_DIR=<sdk-release> -DSMLIB_RUNTIME_DIR=<sdk-release>
cmake --build <build> --config Release
ctest --test-dir <build> -C Release --output-on-failure
```

Use matching C++20, configuration, CRT and kernel layout macros. This native
regression establishes its stated cache contract, not STEP schema qualification.
