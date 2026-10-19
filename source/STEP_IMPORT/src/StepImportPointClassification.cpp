// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportPointClassification.h"

#include <SmBrep.h>
#include <SmPointClass.h>
#include <SmRegion.h>
#include <SmTopologySolver.h>
#include <gsl/assert>

namespace step_import::detail
{
SmRegion* RegionFromStableRays(const RegionRayCaster& castRay)
{
    Expects(bool(castRay));
    constexpr std::array<std::array<double, 3>, 7> directions{
        { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 1, 2, 3 } }
    };
    for (const auto& direction : directions)
    {
        const auto answer = castRay(direction);
        if (answer.region && (answer.contact == RegionRayContact::ClearMiss || answer.contact == RegionRayContact::Face))
        {
            return answer.region;
        }
    }
    return nullptr;
}
SmStatus ClassifySnapshotPoint(const SmBrep& brep, const SmPoint3d& point, double tolerance, SmPointClassification& output)
{
    output.SetClassObject(SM_PC_UNKNOWN, nullptr);
    output.SetSrcZoneTol3d(tolerance);
    // SmBrep::Point3DClassify's boundary phase: preserve the lowest-dimensional
    // point intersection before interpreting ANY ray. A point ON an edge or
    // vertex is different from a remote ray merely hitting that feature.
    SmSolution storage[4];
    SmSolutionArray solutions(4, storage);
    const auto status = SmTopologySolver::BrepPointSolve(&brep, point, SM_SO_INTERSECT, SM_SR_SINGLE, tolerance, 0., nullptr, solutions);
    if (status != SM_SUCCESS)
    {
        return status;
    }
    if (solutions.GetSize())
    {
        const auto& solution = solutions[0];
        return output.LoadFromIntersection(tolerance, solution.m_apObjects[0], solution.m_vStart, 0, SM_BIG_DOUBLE);
    }
    const auto region = RegionFromStableRays(
        [&](const auto& direction)
        {
            SmVector3d unit(direction[0], direction[1], direction[2]);
            if (unit.Unitize() != SM_SUCCESS)
            {
                return RegionRayResult{};
            }
            SmRegion* found = nullptr;
            SmPointClassification hit(tolerance, brep.GetContext());
            if (brep.RayFire(point, unit, tolerance, found, hit) != SM_SUCCESS || !found)
            {
                return RegionRayResult{};
            }
            // RayFire maps a successful empty solution set to the infinite region
            // (SmBrep.cpp). Do not reinterpret an unknown/null/mismatched answer.
            const auto contact = hit.GetPointClass();
            const auto kind = contact == SM_PC_UNKNOWN && found == brep.GetInfiniteRegion() ? RegionRayContact::ClearMiss :
                              contact == SM_PC_FACE                                         ? RegionRayContact::Face :
                              contact == SM_PC_EDGE                                         ? RegionRayContact::Edge :
                              contact == SM_PC_VERTEX                                       ? RegionRayContact::Vertex :
                                                                                              RegionRayContact::Failed;
            return RegionRayResult{ kind, found };
        }
    );
    if (region)
    {
        output.SetClassObject(SM_PC_REGION, region);
        output.SetGap3d(0.);
        output.SetPreSnapParam(SM_BIG_DOUBLE);
    }
    // Exhausted/degenerate ray evidence remains UNKNOWN, mapped by the facade
    // to Indeterminate. No geometry, working tolerance or region is changed.
    return SM_SUCCESS;
}
} // namespace step_import::detail
