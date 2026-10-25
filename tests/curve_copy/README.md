<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Native curve Copy contracts

`curve_copy_contracts` exercises the existing virtual `Copy` seam on
`SmSTEPSurface` and `SmCrvOnSurf`. Fixtures contain only public synthetic
constants. The tests do not import STEP models or use private model libraries.

With validation enabled, build and run the focused suite:

```powershell
cmake --build _build/step-uv-no-adjust --config Release --target curve_copy_contracts --parallel 2
ctest --test-dir _build/step-uv-no-adjust -C Release -R '^curve_copy_' --output-on-failure
```

Each mode is a separate CTest case with a 30-second timeout. To isolate a case,
run the executable with the mode below and the matching DLL runtime on `PATH`.
Archive modes also take a disposable output-directory argument.

| Modes | Observable contract |
| --- | --- |
| `step_surface`, `step_surface_borrowed` | The wrapper clones its base, keeps the requested context and same-parameter evaluation, and remains usable after source destruction. The copied wrapper independently owns its base even when the original wrapper borrows its base. |
| `wrapper_base_error`, `wrapper_base_candidate_error` | Preserve the base's non-generic returned error, leave the wrapper output null, retain source ownership and clean up an allocated candidate before returning. |
| `wrapper_base_null`, `wrapper_base_alias` | Reject success with null or a source alias, without deleting the source. The fault Copy must actually be invoked once. |
| `wrapper_base_wrong_context`, `wrapper_base_wrong_type` | Reject and release an independently allocated base candidate with the wrong context or runtime type. |
| `wrapper_base_throw` | A base that assigns a candidate and then throws `std::bad_alloc` keeps that exact exception type, publishes no wrapper and releases the candidate during unwinding. |
| `surface_error`, `uv_error` | Preserve the child's returned error, leave the output unpublished, retain source ownership and release pending candidates; a UV failure prevents further surface Copy. |
| `surface_candidate_error`, `uv_candidate_error` | A child that returns a new candidate followed by failure does not leak that candidate. |
| `surface_alias`, `uv_alias` | Success accompanied by an input alias is rejected without deleting or re-owning that input. |
| `surface_throw`, `uv_throw` | A child that assigns a candidate and then throws `std::bad_alloc` keeps that exact exception type, publishes no COS, and releases all pending UV/surface candidates before the caller catches. A UV throw prevents further surface Copy. |
| `ownership_0` through `ownership_3` | Preserve each constructor flag's borrowing/ownership policy, exact UV and surface data, explicit domains, attributes and analytic derivatives across contexts and source destruction. Borrowed inputs remain alive for the whole use. |
| `definition_cold`, `definition_warm` | Keep the exact composed definition before and after creating a source 3D approximation cache. Cache allocation identity and coefficients are not Copy requirements. |
| `wrapper_archive_binary`, `wrapper_archive_ascii` | Copy the composed STEP wrapper, destroy its source, persist without spline conversion, read the standalone curve through `SmNativeArchive`, restore both owned-child backlinks, pass full native LEVEL_2 validation, retain independent analytic parameter/derivative results and reproduce the complete stored-definition archive bytes. |

The `precision_archive_binary` and `precision_archive_ascii` modes independently
construct a near-axis frustum that fails the default pole-approximation check.
An explicit local carrier zone preserves its defining generator and UV, passes
LEVEL_2, leaves another carrier and the system zone unchanged, rejects duplicate
or shared policy attributes, survives independent Copy and uniform scale, and
round-trips an exactly encoded binary64 resolution through either archive type.

There are 27 independent modes. For a pre-fix red run of the original Copy controls, compile a matching
consumer from the same test sources using the baseline's `SmSTEPSurface`
declaration. An isolated include overlay can supply that original header before
the current include directory; retain the matching configuration, CRT and
layout macros, and verify that the executable's named imports exist in the
baseline DLL. Keep that consumer separate from the normal consumer built with
the new declaration and new DLL.

Wrapper Copy calls use a noinline `SmSurface&` virtual dispatch, but this does
not guarantee that a consumer compiled with the new header can load an old
DLL. Compiler-generated references can still import the newly declared
`SmSTEPSurface::Copy` symbol. A loader failure such as `0xC0000139` does not
execute the assertions and is not a red test result. Count a red run only after
the matching consumer loads, prints the intended failed contract checks and
returns its test failure status.

Before asserting, fault cases dismantle any returned duplicate ownership and
restore the original owners. They never deliberately execute a double free.
The wrapper/archive control likewise stops and safely cleans up if the old
library returns the original wrapper.
For an exception from the old COS path, a tracker records the leaked candidate
before the harness reclaims it. That test-only reclamation cannot make the
before-catch cleanup assertion pass. Wrapper fault cases require one nested
Copy invocation, so the old inherited wrapper error cannot vacuously pass null,
alias, context or type protocol tests.

The counted subclasses are confined to Copy failure and lifetime tests.
Persistence controls use standard kernel types. Binary UV coefficients require
exact identity; ASCII permits at most `1e-12` carrier rounding for these public
constants. The wrapper's position, tangent and second derivative expectations
come from the independent closed-form cylinder and degree chain rule. Sampling
these known values does not establish a continuous approximation error bound.

Excluded: direct C++ copy-constructor failure handling, UV-endpoint constructor
flag 2, malformed archives, concurrency, arbitrary user attribute exceptions,
allocator-level allocation-failure injection, and continuous approximation qualification. A
COS child returning success and null is also excluded from the old-library red
suite because the old constructor dereferences it before returning to the
test's cleanup boundary.
