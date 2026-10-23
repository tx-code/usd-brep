# Torus analytic-domain validation

These synthetic native contracts check `SmTorus::AssertValid`, independently of
STEP parsing and native importer admission. Analytic U is rotation around the
revolution axis; analytic V follows the generating circle. Both use degrees,
including when the underlying NURBS axes are swapped.

For the public spindle `R=1, r=2`, the profile radius `R+r*cos(v)` vanishes at
120 and 240 degrees. Changing U cannot change whether the V interval enters
the existing forbidden spindle band. The controls exercise both false rejection
and missed rejection, periodic negative-U intervals, swapped NURBS axes, the
normal complete surface validator, and analytic/NURBS point agreement with the
independent defining equation. No tolerance or self-intersection allowance is
changed.

Deliberately corrupted stored domains separately check the declared local
diagnostic identities: 3 for U extent and 4 for V extent. Existing report macros
filter `SM_LEVEL_GIVEN` requests only in debug builds; release builds run all local
predicates. The tests account for that difference without changing macro policy.

With `SMLIB_BUILD_VALIDATION=ON`, build `torus_validation`, then run
`ctest --test-dir <build> -C Release -R torus_analytic_domain_contracts --output-on-failure`.
The directory also supports a standalone CMake build with matching
`SMLIB_LIBRARY_DIR` and `SMLIB_RUNTIME_DIR`. Use the same C++20, CRT, configuration,
and kernel layout macros as the SDK.

These native checks do not enable spindle/horn STEP import. The importer's
supported source scope and complete-result gates remain in
[STEP_IMPORT](../../source/STEP_IMPORT/README.md).
