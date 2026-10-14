# AssertValid seam ownership regression

This standalone C++ regression uses only generated cylinders, spheres and tori.
It exercises `SmFace::HasSeamProblem` through both its returned classifiers and
`SmBrep::AssertValid(nullptr, SM_LEVEL_2, SM_WALK)`.

Configure against an existing SMLib/SM_API build:

```text
cmake -S tests/assertvalid_ownership -B build/assertvalid-ownership -DSMLIB_LIBRARY_DIR=<library-directory> -DSMLIB_RUNTIME_DIR=<runtime-directory>
cmake --build build/assertvalid-ownership --config Release
ctest --test-dir build/assertvalid-ownership -C Release --output-on-failure
```

`SMLIB_LIBRARY_DIR` contains the SMLib and SM_API import/static libraries. On
Windows, `SMLIB_RUNTIME_DIR` is prepended to the test process PATH and must contain
the matching DLLs. The executable prints the loaded SMLib DLL path. Separate
runtime directories can compare baseline and candidate DLLs without replacing
an existing build. The regression was validated on Windows x64, MSVC v145,
C++17, Release /MD.

- `seam-classification-aliases` checks that each returned UV alias belongs to the
  returned owned seam curve, after the function's temporary FaceProps is gone.
  It checks seam evaluation against the original face surface and repeats calls
  into the same output objects to exercise assignment and old-object cleanup.
- `assertvalid-allocation-balance` is Windows-only. After warmup, it measures
  outstanding process-heap bytes and blocks after 200 create/check/delete jobs
  for each primitive. A small allowance covers one-time runtime allocations;
  the original leak grows by hundreds of kilobytes and fails this test. It
  measures live heap allocations, not reserved pages or working set.

This test has its own CTest entry point; it is not automatically added to the
repository-wide `repo test` task. The existing `prog_test` topology, primitives,
section-advanced and silhouette-advanced suites provide complementary coverage.
