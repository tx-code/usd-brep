# B-spline validation regression

This standalone C++20 test exercises `SmBSplineCurve::AssertValid`, using only
synthetic rational quadratic quarter ellipses. It checks both ordinary LEVEL2
calls and selected radius diagnostics (13 in 2D, 14 in 3D), and verifies that an
unrelated selected test does not acquire a radius failure.

```text
cmake -S tests/curve_validation -B build/curve-validation -DSMLIB_LIBRARY_DIR=<library-directory> -DSMLIB_RUNTIME_DIR=<runtime-directory>
cmake --build build/curve-validation --config Release
ctest --test-dir build/curve-validation -C Release --output-on-failure
```

Use matching SDK configuration, CRT and layout macros. The standalone Windows
configuration targets Release `/MD`; the runtime directory contains the matching
SMLib DLL. It is a separate CTest entry point.

For a quarter ellipse with major radius 1 and minor radius .001, the endpoint
radius of curvature is `b*b/a = 1e-6`, with a nonzero tangent. The old validator
computed radius candidates but compared its unchanged sentinel to the threshold,
so it accepted this curve without a radius diagnostic.

The tests preserve the existing threshold `boundingBoxDiagonal / parameterLength
/ 10000`. That expression has speed units and is also historically used for the
radius comparison. It is parameter dependent; in UV it is not a physical length
criterion. These tests lock down the existing validator quality policy and its
diagnostics, not a universal definition of valid geometry or import correctness.
