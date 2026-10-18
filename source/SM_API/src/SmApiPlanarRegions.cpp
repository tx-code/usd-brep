// SPDX-License-Identifier: Apache-2.0
// Local planar-region extension: explicit boundary-region construction, 2026.
#include "StdAfx.h"

#include <SmApiGeneral.h>
#include <SmApiIntersectors.h>
#include <SmApiPlanarRegions.h>
#include <SmBrep.h>
#include <SmPlanarCurveTransfer.h>
#include <SmCurve.h>
#include <SmEdge.h>
#include <SmFace.h>
#include <SmLoop.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>

namespace
{
using Point = std::array<double, 2>;
using Polygon = std::vector<Point>;
double distance(Point a, Point b)
{
    return std::hypot(a[0] - b[0], a[1] - b[1]);
}
long double cross(Point a, Point b, Point c)
{
    return (static_cast<long double>(b[0]) - a[0]) * (static_cast<long double>(c[1]) - a[1]) -
           (static_cast<long double>(b[1]) - a[1]) * (static_cast<long double>(c[0]) - a[0]);
}
double area(const Polygon& p)
{
    long double sum = 0;
    for (std::size_t i = 1; i + 1 < p.size(); ++i)
    {
        sum += cross(p[0], p[i], p[i + 1]);
    }
    return static_cast<double>(sum / 2);
}
bool inside(Point p, const Polygon& loop)
{
    bool result = false;
    for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++)
    {
        const auto a = loop[i], b = loop[j];
        if ((a[1] > p[1]) != (b[1] > p[1]))
        {
            const long double x = a[0] + (static_cast<long double>(p[1]) - a[1]) * (b[0] - a[0]) / (b[1] - a[1]);
            if (p[0] < x)
            {
                result = !result;
            }
        }
    }
    return result;
}
double pointSegment(Point p, Point a, Point b)
{
    const double length = distance(a, b);
    if (length == 0)
    {
        return distance(p, a);
    }
    const double dx = (b[0] - a[0]) / length, dy = (b[1] - a[1]) / length;
    const double t = std::max(0.0, std::min(length, (p[0] - a[0]) * dx + (p[1] - a[1]) * dy));
    return distance(p, { a[0] + t * dx, a[1] + t * dy });
}
bool closeSegments(Point a, Point b, Point c, Point d, double tolerance)
{
    if (std::max(a[0], b[0]) + tolerance < std::min(c[0], d[0]) || std::max(c[0], d[0]) + tolerance < std::min(a[0], b[0]) ||
        std::max(a[1], b[1]) + tolerance < std::min(c[1], d[1]) || std::max(c[1], d[1]) + tolerance < std::min(a[1], b[1]))
    {
        return false;
    }
    const auto abC = cross(a, b, c), abD = cross(a, b, d), cdA = cross(c, d, a), cdB = cross(c, d, b);
    if (((abC > 0 && abD < 0) || (abC < 0 && abD > 0)) && ((cdA > 0 && cdB < 0) || (cdA < 0 && cdB > 0)))
    {
        return true;
    }
    return std::min({ pointSegment(a, c, d), pointSegment(b, c, d), pointSegment(c, a, b), pointSegment(d, a, b) }) <= tolerance;
}
bool finite(const SmVector3d& p)
{
    return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
}
bool positive(double value)
{
    return std::isfinite(value) && value > 0;
}
SmPlanarRegionStatus fail(SmPlanarRegionReport& r, SmPlanarRegionStatus status, long curve = -1)
{
    r.status = status;
    r.problemCurve = curve;
    r.loops.clear();
    r.regions = r.holes = 0;
    return status;
}
bool validOptions(const SmPlanarRegionOptions& o)
{
    return positive(o.joinTolerance) && positive(o.planeTolerance) && positive(o.chordTolerance) && positive(o.separationTolerance) &&
           std::isfinite(o.separationTolerance + 2 * o.chordTolerance + o.joinTolerance) && o.maxPoints >= 3 && o.maxPairTests > 0;
}
} // namespace

SmPlanarRegionStatus SmApiBuildPlanarRegions(
    const SmTArray<SmCurve*>& boundaries,
    const SmPoint3d& origin,
    const SmVector3d& normal,
    const SmPlanarRegionOptions& o,
    SmBrep*& result,
    SmPlanarRegionReport& r
)
{
    r = SmPlanarRegionReport();
    if (result != nullptr || !validOptions(o) || !finite(origin) || !finite(normal))
    {
        return fail(r, SmPlanarRegionStatus::InvalidInput);
    }
    SmVector3d n = normal;
    if (n.Unitize() != SM_SUCCESS)
    {
        return fail(r, SmPlanarRegionStatus::InvalidInput);
    }
    // Deterministic right-handed frame, avoiding a nearly parallel seed axis.
    const SmVector3d seed = std::abs(n[0]) < 0.8 ? SmVector3d(1, 0, 0) : SmVector3d(0, 1, 0);
    const double dot = seed[0] * n[0] + seed[1] * n[1] + seed[2] * n[2];
    r.axisX = seed - n * dot;
    if (r.axisX.Unitize() != SM_SUCCESS)
    {
        return fail(r, SmPlanarRegionStatus::InvalidInput);
    }
    r.axisY = SmVector3d(n[1] * r.axisX[2] - n[2] * r.axisX[1], n[2] * r.axisX[0] - n[0] * r.axisX[2], n[0] * r.axisX[1] - n[1] * r.axisX[0]);
    const std::size_t count = boundaries.GetSize();
    if (!count)
    {
        return SmPlanarRegionStatus::Success;
    }
    if (count > o.maxPoints / 2)
    {
        return fail(r, SmPlanarRegionStatus::LimitExceeded);
    }
    std::vector<Polygon> samples(count);
    std::vector<SmPoint3d> ends;
    std::size_t total = 0, pairTests = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (boundaries[static_cast<ULONG>(i)] == nullptr)
        {
            return fail(r, SmPlanarRegionStatus::InvalidInput, static_cast<long>(i));
        }
        SmTArray<SmPoint3d> points;
        auto* curve = boundaries[static_cast<ULONG>(i)];
        SmBoolean onPlane = FALSE;
        double planeDeviation = 0;
        r.kernelStatus = curve->IsOnPlane(origin, n, o.planeTolerance, onPlane, planeDeviation);
        if (r.kernelStatus != SM_SUCCESS)
        {
            return fail(r, SmPlanarRegionStatus::KernelFailure, static_cast<long>(i));
        }
        if (!std::isfinite(planeDeviation))
        {
            return fail(r, SmPlanarRegionStatus::InvalidInput, static_cast<long>(i));
        }
        r.maxPlaneDeviation = std::max(r.maxPlaneDeviation, planeDeviation);
        if (!onPlane)
        {
            return fail(r, SmPlanarRegionStatus::NonPlanar, static_cast<long>(i));
        }
        r.kernelStatus = curve->Tessellate(curve->GetNaturalInterval(), o.chordTolerance, 0, 0, nullptr, &points, nullptr);
        if (r.kernelStatus != SM_SUCCESS)
        {
            return fail(r, SmPlanarRegionStatus::KernelFailure, static_cast<long>(i));
        }
        if (points.GetSize() < 2)
        {
            return fail(r, SmPlanarRegionStatus::DegenerateBoundary, static_cast<long>(i));
        }
        total += points.GetSize();
        if (total > o.maxPoints)
        {
            return fail(r, SmPlanarRegionStatus::LimitExceeded);
        }
        ends.push_back(points[0]);
        ends.push_back(points[points.GetSize() - 1]);
        for (ULONG j = 0; j < points.GetSize(); ++j)
        {
            if (!finite(points[j]))
            {
                return fail(r, SmPlanarRegionStatus::InvalidInput, static_cast<long>(i));
            }
            const SmVector3d p = points[j] - origin;
            const double deviation = std::abs(p[0] * n[0] + p[1] * n[1] + p[2] * n[2]);
            r.maxPlaneDeviation = std::max(r.maxPlaneDeviation, deviation);
            if (deviation > o.planeTolerance)
            {
                return fail(r, SmPlanarRegionStatus::NonPlanar, static_cast<long>(i));
            }
            Point q = { p[0] * r.axisX[0] + p[1] * r.axisX[1] + p[2] * r.axisX[2], p[0] * r.axisY[0] + p[1] * r.axisY[1] + p[2] * r.axisY[2] };
            if (!std::isfinite(q[0]) || !std::isfinite(q[1]))
            {
                return fail(r, SmPlanarRegionStatus::InvalidInput);
            }
            if (samples[i].empty() || distance(q, samples[i].back()) > 0)
            {
                samples[i].push_back(q);
            }
        }
        if (samples[i].size() < 2)
        {
            return fail(r, SmPlanarRegionStatus::DegenerateBoundary, static_cast<long>(i));
        }
    }
    // Every endpoint must have exactly one mate within the requested distance.
    // No transitive clustering or model-size-based tolerance widening.
    std::vector<std::size_t> mate(ends.size(), ends.size());
    for (std::size_t i = 0; i < ends.size(); ++i)
    {
        for (std::size_t j = i + 1; j < ends.size(); ++j)
        {
            if (++pairTests > o.maxPairTests)
            {
                return fail(r, SmPlanarRegionStatus::LimitExceeded);
            }
            const double gap = ends[i].DistanceBetween(ends[j]);
            if (gap <= o.joinTolerance)
            {
                if (mate[i] != ends.size() || mate[j] != ends.size())
                {
                    return fail(r, SmPlanarRegionStatus::AmbiguousJoin, static_cast<long>(i / 2));
                }
                mate[i] = j;
                mate[j] = i;
                r.maxJoinGap = std::max(r.maxJoinGap, gap);
            }
        }
    }
    for (std::size_t i = 0; i < mate.size(); ++i)
    {
        if (mate[i] == ends.size())
        {
            return fail(r, SmPlanarRegionStatus::OpenBoundary, static_cast<long>(i / 2));
        }
    }

    std::vector<SmPlanarRegionLoop> loops;
    std::vector<bool> used(count, false);
    for (std::size_t first = 0; first < count; ++first)
    {
        if (used[first])
        {
            continue;
        }
        SmPlanarRegionLoop loop;
        std::size_t entry = 2 * first;
        do
        {
            const auto index = entry / 2;
            if (used[index])
            {
                return fail(r, SmPlanarRegionStatus::AmbiguousJoin);
            }
            used[index] = true;
            loop.curves.push_back(static_cast<ULONG>(index));
            loop.reversed.push_back(entry % 2 != 0);
            auto points = samples[index];
            if (entry % 2)
            {
                std::reverse(points.begin(), points.end());
            }
            // Midpoint snap bounds each endpoint's displacement by joinTolerance/2.
            for (int end = 0; end < 2; ++end)
            {
                const auto endpoint = end ? (entry ^ 1) : entry;
                const auto other = mate[endpoint];
                const Point q = other % 2 ? samples[other / 2].back() : samples[other / 2].front();
                auto& p = end ? points.back() : points.front();
                p = { (p[0] + q[0]) * 0.5, (p[1] + q[1]) * 0.5 };
            }
            loop.points.insert(loop.points.end(), points.begin(), points.end() - 1);
            entry = mate[entry ^ 1];
        } while (entry != 2 * first);
        if (loop.points.size() < 3)
        {
            return fail(r, SmPlanarRegionStatus::DegenerateBoundary);
        }
        const double a = area(loop.points);
        if (!std::isfinite(a) || std::abs(a) <= o.joinTolerance * o.joinTolerance)
        {
            return fail(r, SmPlanarRegionStatus::DegenerateBoundary);
        }
        loop.signedArea = a;
        loops.push_back(std::move(loop));
    }
    const double guard = o.separationTolerance + 2 * o.chordTolerance + o.joinTolerance;
    for (std::size_t a = 0; a < loops.size(); ++a)
    {
        for (std::size_t i = 0; i < loops[a].points.size(); ++i)
        {
            for (std::size_t b = a; b < loops.size(); ++b)
            {
                for (std::size_t j = b == a ? i + 1 : 0; j < loops[b].points.size(); ++j)
                {
                    if (++pairTests > o.maxPairTests)
                    {
                        return fail(r, SmPlanarRegionStatus::LimitExceeded);
                    }
                    const auto& p = loops[a].points;
                    const auto& q = loops[b].points;
                    if (a == b && ((i + 1) % p.size() == j || (j + 1) % p.size() == i))
                    {
                        continue;
                    }
                    if (closeSegments(p[i], p[(i + 1) % p.size()], q[j], q[(j + 1) % q.size()], guard))
                    {
                        return fail(r, SmPlanarRegionStatus::IntersectingOrCloseBoundaries);
                    }
                }
            }
        }
    }
    // Containment here supplies diagnostics and an independent postcondition;
    // the existing kernel still constructs the exact nested BRep faces.
    for (std::size_t i = 0; i < loops.size(); ++i)
    {
        for (std::size_t j = 0; j < loops.size(); ++j)
        {
            if (i != j && std::abs(loops[j].signedArea) > std::abs(loops[i].signedArea) && inside(loops[i].points[0], loops[j].points))
            {
                if (loops[i].parent < 0 || std::abs(loops[j].signedArea) < std::abs(loops[loops[i].parent].signedArea))
                {
                    loops[i].parent = static_cast<int>(j);
                }
            }
        }
    }
    for (auto& loop : loops)
    {
        for (int p = loop.parent; p >= 0; p = loops[p].parent)
        {
            ++loop.depth;
        }
        if ((loop.signedArea > 0) != (loop.depth % 2 == 0))
        {
            std::reverse(loop.points.begin(), loop.points.end());
            std::reverse(loop.curves.begin(), loop.curves.end());
            std::reverse(loop.reversed.begin(), loop.reversed.end());
            for (std::size_t i = 0; i < loop.reversed.size(); ++i)
            {
                loop.reversed[i] = !loop.reversed[i];
            }
            loop.signedArea = -loop.signedArea;
        }
    }

    const auto* context = SmApiGetOrCreateContext();
    std::vector<std::unique_ptr<SmCurve>> clones;
    SmTArray<SmCurve*> inputs;
    std::map<const SmCurve*, ULONG> index;
    for (ULONG i = 0; i < boundaries.GetSize(); ++i)
    {
        SmCurve* curve = nullptr;
        r.kernelStatus = boundaries[i]->Copy(*context, curve);
        std::unique_ptr<SmCurve> owned(curve);
        if (r.kernelStatus != SM_SUCCESS || !curve)
        {
            return fail(r, SmPlanarRegionStatus::KernelFailure, i);
        }
        inputs.Add(curve);
        index[curve] = i;
        clones.push_back(std::move(owned));
    }
    std::unique_ptr<SmBrep> brep(new (*context) SmBrep());
#ifndef SM_USE_NEWTOL
    brep->SetTolerance(o.joinTolerance, FALSE, FALSE);
#endif
    SmTArray<SmFace*> faces;
    SmPlanarCurveTransfer transfer(inputs);
    try { r.kernelStatus = transfer.Create(*brep, nullptr, o.planeTolerance, faces); }
    catch (...)
    {
        // Native attachment precedes ownership handoff from the clone guards.
        transfer.DetachInputs();
        throw;
    }
    if (r.kernelStatus != SM_SUCCESS)
    {
        transfer.DetachInputs();
        return fail(r, SmPlanarRegionStatus::KernelFailure);
    }
    // The successful native call consumes all input curves. Transfer immediately,
    // before any allocating postcondition checks, to avoid two RAII owners.
    for (auto& c : clones)
    {
        c.release();
    }
    // Verify native loop partition and hole grouping using cloned-curve identity.
    // This detects the legacy builder silently widening joins or changing nesting.
    std::vector<bool> seen(loops.size(), false);
    bool matches = true;
    for (ULONG f = 0; f < faces.GetSize(); ++f)
    {
        SmTArray<SmLoop*> nativeLoops, outer;
        faces[f]->GetLoops(nativeLoops);
        faces[f]->GetOuterLoops(outer);
        int outerIndex = -1;
        std::vector<int> holeIndices;
        for (ULONG l = 0; l < nativeLoops.GetSize(); ++l)
        {
            SmTArray<SmEdge*> edges;
            nativeLoops[l]->GetEdges(edges);
            std::set<ULONG> ids;
            for (ULONG e = 0; e < edges.GetSize(); ++e)
            {
                const auto found = index.find(edges[e]->GetCurve());
                if (found == index.end())
                {
                    matches = false;
                }
                else
                {
                    ids.insert(found->second);
                }
            }
            int match = -1;
            for (std::size_t k = 0; k < loops.size(); ++k)
            {
                if (ids == std::set<ULONG>(loops[k].curves.begin(), loops[k].curves.end()))
                {
                    match = static_cast<int>(k);
                }
            }
            if (match < 0 || seen[match])
            {
                matches = false;
                continue;
            }
            seen[match] = true;
            if (outer.GetSize() == 1 && nativeLoops[l] == outer[0])
            {
                outerIndex = match;
            }
            else
            {
                holeIndices.push_back(match);
            }
        }
        if (outerIndex < 0 || loops[outerIndex].depth % 2)
        {
            matches = false;
        }
        for (int hole : holeIndices)
        {
            if (loops[hole].parent != outerIndex || !(loops[hole].depth % 2))
            {
                matches = false;
            }
        }
    }
    if (!matches || std::find(seen.begin(), seen.end(), false) != seen.end())
    {
        return fail(r, SmPlanarRegionStatus::TopologyMismatch);
    }
    if (brep->AssertValid(nullptr, SM_LEVEL_2, SM_WALK) != TRUE)
    {
        return fail(r, SmPlanarRegionStatus::InvalidRegion);
    }
    for (const auto& loop : loops)
    {
        if (loop.depth % 2)
        {
            ++r.holes;
        }
        else
        {
            ++r.regions;
        }
    }
    r.loops = std::move(loops);
    result = brep.release();
    return r.status = SmPlanarRegionStatus::Success;
}

SmPlanarRegionStatus SmApiSectionPlanarRegions(
    SmBrep* body,
    const SmPoint3d& origin,
    const SmVector3d& normal,
    const SmPlanarRegionOptions& options,
    SmBrep*& result,
    SmPlanarRegionReport& report
)
{
    report = SmPlanarRegionReport();
    if (body == nullptr || result != nullptr || !validOptions(options) || !finite(origin) || !finite(normal))
    {
        return fail(report, SmPlanarRegionStatus::InvalidInput);
    }
    SmVector3d n = normal;
    if (n.Unitize() != SM_SUCCESS)
    {
        return fail(report, SmPlanarRegionStatus::InvalidInput);
    }
    SmTArray<SmCurve*> curves;
    // Use a local native result array even on failure so every returned curve
    // can be reclaimed; the old SM_API wrapper only appends on success.
    report.kernelStatus = body->CreatePlanarSectionCurves(*SmApiGetOrCreateContext(), origin, n, NULL, NULL, &curves, NULL, NULL, false);
    std::vector<std::unique_ptr<SmCurve>> owners;
    for (ULONG i = 0; i < curves.GetSize(); ++i)
    {
        owners.emplace_back(curves[i]);
    }
    if (report.kernelStatus != SM_SUCCESS)
    {
        return fail(report, SmPlanarRegionStatus::KernelFailure);
    }
    return SmApiBuildPlanarRegions(curves, origin, n, options, result, report);
}
