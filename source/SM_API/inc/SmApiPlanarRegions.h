// SPDX-License-Identifier: Apache-2.0
// Local planar-region extension: explicit boundary-region construction, 2026.
#ifndef SM_API_PLANAR_REGIONS_H
#define SM_API_PLANAR_REGIONS_H

#include <SmApiTypes.h>
#include <SmTArray.h>
#include <SmVector3d.h>
#include <array>
#include <cstddef>
#include <vector>

class SmBrep;
class SmCurve;

enum class SmPlanarRegionStatus
{
    Success,
    InvalidInput,
    OpenBoundary,
    AmbiguousJoin,
    NonPlanar,
    DegenerateBoundary,
    IntersectingOrCloseBoundaries,
    LimitExceeded,
    KernelFailure,
    TopologyMismatch,
    InvalidRegion
};

struct SmPlanarRegionOptions
{
    // Absolute distances in model units; no automatic scale-dependent widening.
    double joinTolerance = 1e-6;
    double planeTolerance = 1e-6;
    double chordTolerance = 1e-4;
    double separationTolerance = 1e-6;
    // Limits retained samples and our pair checks, not native tessellator allocations.
    std::size_t maxPoints = 20000;
    std::size_t maxPairTests = 5000000;
};

struct SmPlanarRegionLoop
{
    // Input curve indices in traversal order, with matching reversal flags.
    std::vector<ULONG> curves;
    std::vector<bool> reversed;
    // Closed implicitly. Samples in the returned right-handed plane frame.
    std::vector<std::array<double, 2>> points;
    int parent = -1;
    unsigned depth = 0; // even: material exterior/island; odd: hole
    double signedArea = 0; // sample area, CCW exterior / CW hole
};

struct SmPlanarRegionReport
{
    SmPlanarRegionStatus status = SmPlanarRegionStatus::Success;
    SmApiStatus kernelStatus = 0;
    long problemCurve = -1;
    double maxJoinGap = 0;
    double maxPlaneDeviation = 0;
    SmVector3d axisX = SmVector3d(0, 0, 0), axisY = SmVector3d(0, 0, 0);
    std::vector<SmPlanarRegionLoop> loops;
    std::size_t regions = 0;
    std::size_t holes = 0;
};

// Build even/odd material regions from explicitly selected boundary curves.
// Suitable for sections and SELECTED projection contours. All-visible HLR
// linework is NOT a footprint: blind-pocket contours/creases require selection.
// Curves are borrowed, never consumed or edited. Output retains exact cloned
// curves in a planar BRep; report points/area are tessellated approximations.
// Requires result == nullptr on entry; success transfers result to the caller
// (delete it normally). Empty input succeeds with nullptr and zero regions.
// Failure leaves result nullptr, no partial loops; report holds diagnostics.
// Invalid non-null result on entry is rejected without deleting/changing it.
// Preflight rejects intersections/touches within separationTolerance plus the
// sampling/join uncertainty band. This is deliberately conservative, not healing.
SMAPI_EXPORT SmPlanarRegionStatus SmApiBuildPlanarRegions(
    const SmTArray<SmCurve*>& boundaries,
    const SmPoint3d& origin,
    const SmVector3d& normal,
    const SmPlanarRegionOptions& options,
    SmBrep*& result,
    SmPlanarRegionReport& report
);

// Read-only section query followed by exactly the same region construction.
SMAPI_EXPORT SmPlanarRegionStatus SmApiSectionPlanarRegions(
    SmBrep* body,
    const SmPoint3d& origin,
    const SmVector3d& normal,
    const SmPlanarRegionOptions& options,
    SmBrep*& result,
    SmPlanarRegionReport& report
);

#endif
