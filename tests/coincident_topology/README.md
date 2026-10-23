# Coincident topology contracts

Analytic boxes establish the coincidence predicate independently of its return
value or report list. Copying a second box graph with `MergeBrep` preserves valid
links; the coincident case intentionally overlaps geometry, without sewing or
breaking curve/vertex definitions. A separated pair is the negative control.

The cases check first/all witnesses, paired and independently optional output
arrays, explicit distance thresholds, report levels, and root validation test 15.
The geometric predicate must not depend on whether a report was requested. Root
LEVEL2 validation rejects coincident topology; LEVEL0 does not run test 15.
`SM_LEVEL_GIVEN` requests including and excluding 15 run the corresponding
predicate consistently in Release and Debug.
This is a regression contract for these checks, not complete BRep validity.

`coincident_partial` uses an exact affine planar support and a piecewise cubic
wire that leaves or approaches that plane. Its endpoint parameters, UV values
and recorded distances must describe the same sampled pairs, including an early
exit and a midpoint that misses the support. Left-only, right-only, fully
coincident and spatially separated controls also exercise the BRep caller.
The partial-screen interval remains heuristic between observed endpoints;
these controls do not grant it a continuous coincidence certificate.

The validation-enabled SDK build registers the cases. For an existing matching
SDK, the standalone entry point accepts `SMLIB_LIBRARY_DIR` and
`SMLIB_RUNTIME_DIR`, uses C++20 and the same CRT/layout macros, and runs with:

```shell
cmake -S tests/coincident_topology -B <build> -DSMLIB_LIBRARY_DIR=<libs> -DSMLIB_RUNTIME_DIR=<runtime>
cmake --build <build> --config Release
ctest --test-dir <build> -C Release --output-on-failure
```
