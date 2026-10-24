<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Kernel Lifetime, Context, and Cache Reference

This is internal SMLib guidance. `SM_API` and `_omni_solid` deliberately abstract these mechanisms
from ordinary users.

## Context allocation

Persistent geometry and topology objects commonly derive from `SmObject` and are normally
allocated against an `SmContext`; the object stores a non-owning context pointer. This is not a
universal kernel rule: helpers such as `SmTess` and `SmGlobalSolver` do not derive from `SmObject`,
and ordinary `new` on an `SmObject` records a null context. Start with `SmObject.h` and
`SmContext.h` when debugging cross-context ownership, placement allocation, or operation state.

Do not carry forward the archived recommendation that applications create a global `SmContext`:

- `SM_API` provides a process-global default through `SmApiGetOrCreateContext()`. Selected
  operations use an input object's context or a dedicated output context instead.
- `_omni_solid` initializes that API context automatically.
- The current default context is process-global and has no public teardown contract.
- Python bindings commonly use `py::nodelete`; Python garbage collection is not the kernel object
  reclamation mechanism.

The context contains mutable operation state. Do not infer thread safety from historical
“multi-processor” claims.

## Ownership boundaries

Distinguish:

- **Standalone geometry:** may be consumed or transferred by an operation according to that
  operation's contract.
- **BRep-owned geometry:** surfaces and curves borrowed through face/edge handles remain owned by
  the BRep and must not be independently deleted or mutated.
- **Use objects and intrusive topology:** structural membership and ownership are represented by
  list-owner and reciprocal-list links; do not repair one pointer in isolation. Deletion and
  transfer rules remain class- and operation-specific. `SmContext` association is non-owning and
  supplies shared operation state. It is often established during placement construction but does
  not reliably record allocation provenance or make the context the object's owner.

For an API wrapper, trace ownership from the kernel signature through the `SmApi*` header and then
through Python return-value and keep-alive policy. Never infer ownership from pointer type alone.

Current starting points:

- `source/SMLib/inc/SmObject.h`
- `source/SMLib/inc/SmContext.h`
- `source/SM_API/src/SmApiGeneral.cpp`
- `source/SmPyLib/src/SmPyMain.cpp`
- `source/SmPyLib/src/SmPyTypes.cpp`

### Native composed-curve copies

Use the status-returning virtual `SmCurve::Copy` / `SmSurface::Copy` path and check its result
before consuming the output. `SmCrvOnSurf::Copy` clones children that the source owns and retains
borrowed children as borrowed: flag 0 borrows both, flag 1 copies only the UV curve, and flags 2/3
produce independently owned UV/support copies. A borrowed support therefore still needs to
outlive the copied curve. `SmSTEPSurface::Copy` always owns a deep copy of its underlying surface,
including when the original wrapper borrows that surface.

These paths publish a null output on a child-copy failure, preserve the child's error status, and
release unpublished independent candidates. They reject success with a null, known source alias,
different geometry type, wrong target context, or different UV dimension. An allocation exception
continues to propagate after candidate cleanup. Child implementations must still honor their own
copy contract; arbitrary alias graphs or leaks before writing the output are not repaired here.

The exact UV definition, support, parameter domain, and existing base attribute-copy rules are
retained. Curve breaks and the derived 3D approximation are rebuilt lazily; copying does not
convert the composition to an approximation or change its parameter convention. This does not
qualify a STEP source pcurve's parameter mapping or complete import acceptance.
Successful `SmCrvOnSurf::ReadFromDB` also restores its UV and support owner backlinks.
Standalone archive curves are returned directly, whereas BRep reconstruction subsequently
copies its edge curves; the standalone reader cannot rely on that later Copy to restore its links.
Cached gap diagnostics return to the uncomputed state, and optional approximation-break hints
are not carried into the copy; provide them again when requesting a new approximation.

The legacy public `SmCrvOnSurf` C++ copy constructor, `SetAll`, and UV-endpoints constructors do
not acquire this fallible-copy guarantee. In particular, avoid their copying modes when a child
can fail to copy; see [known issues](../../../../KNOWN_ISSUES.md).

## Caches and lazy state

Global context caches are disabled by default and deprecated in current configuration. Do not copy
archived cache-sizing recommendations.

Lazy per-object state still matters. Curves, surfaces, topology, and validators can materialize or
refresh bounding, trim, gap, tessellation, or tolerance data. Therefore:

- A logically validating or querying call may not be byte-for-byte read-only.
- Do not call state-refreshing validation concurrently on the same context/object graph.
- Reproduce cache-sensitive bugs with both absent and materialized derived data.
- Invalidate or rebuild through the owning API rather than editing cache members directly.

Use `SmConfig.h`, `SmCurve.cpp`, `SmSurface.cpp`, and the relevant object's validator to verify the
current cache path.

## NLib status

NLib is incorporated into SMLib. Legacy `NL_*`, `NL_CURVE`, and `NL_SURFACE` code under
`source/SMLib` is an implementation detail, not a separate supported interoperability layer.

Some B-spline curve and surface implementations can still borrow internal NURBS storage. Their
borrowed flags affect destruction and copying, so preserve those semantics when changing
`SmBSplineCurve` or `SmBSplineSurface`. Verify current constructors and destructors; do not reuse
the archived `N_CrvCopy` examples or product-boundary terminology.

## Tolerances

Do not revive obsolete direct tolerance setters or present one scalar as a universal kernel
tolerance. Use current `SmTol` accessors and the operation-specific contract:

- zone tolerance for local coincidence/modeling decisions;
- intersection tolerance for intersections and topology agreement;
- approximation tolerance for conversion and approximation.

Record active build macros and operation state when diagnosing legacy/new tolerance-model
differences.
