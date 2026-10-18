# Planar curve transfer contracts

The internal `SmPlanarCurveTransfer` module records actual curve attachment in
the kernel. Direct planar-face construction, planar-region construction,
OffsetProfile and Skin consume the same facts while retaining their different
input-preservation policies. Existing SM_API and virtual kernel signatures are
unchanged.

The record prepares its storage before construction. A scoped thread-local
observer matches the destination BRep and original curve identity; nested calls
restore the previous observer. Attachment and return update the pre-sized
primitive ownership report without allocating. This does not add concurrent
mutation support to the kernel.

An edge can own its curve before entering the BRep edge list. The construction
guard returns that curve and releases its unlinked edge/edgeuses/vertexuses if
pre-link preparation fails. After linking, the existing BRep owns cleanup.
This is not a general rollback mechanism for a retained, partially edited BRep.
Private failed candidates detach recorded inputs before destruction; a
successful consuming result requires evidence for every input. Partial Skin
transfers cannot fall back to interpreting the input as an open profile.

## Build and run

Use a separate C++20 build directory. This standalone project builds matching
SMLib and SM_API DLLs from source; it does not use a prebuilt native SDK.

```powershell
cmake -S source/SM_API/tests/planar_transfer -B <build>
cmake --build <build> --config Release --parallel 4 --target planar_transfer_contracts
ctest --test-dir <build> -C Release --output-on-failure
cmake --build <build> --config Debug --parallel 4 --target planar_transfer_contracts
ctest --test-dir <build> -C Debug --output-on-failure
```

Only this dedicated build defines `SM_PLANAR_TRANSFER_TESTING`. The injected
status/allocation failures occur after attachment but before edge-list insertion,
or after all construction has completed. The normal SDK has no injection controls.

Tests call the construction module and real SM_API entry points. They check
partial and complete attachment, returned inputs, private-copy destruction,
exception unwinding, duplicate identities, zero-area extra loops, and successful
regions with holes. Successful models also undergo pointer and Level-2 checks.
On Windows Debug, the shared CRT observer verifies repeated failures do not
retain native allocations and includes a positive allocation control.

To run existing Python regressions against the same native build, configure
`-DPLANAR_TRANSFER_BUILD_PYTHON=ON` with a Python 3.12 interpreter/development
installation and pybind11 (bundled headers are used when available). Build
`_omni_solid`, then place `<build>/bin/Release` and `source/SmPyLib/tests` on
`PYTHONPATH`; on Windows also add the runtime directory to `PATH`.

The zero-area case is a rejection regression. It does not establish that the
previous implementation successfully omitted a curve; no geometry/nesting
algorithm is changed on that assumption.
