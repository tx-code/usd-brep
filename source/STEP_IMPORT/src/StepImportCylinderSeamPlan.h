// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"
#include "StepImportExecutionCallbacks.h"

#include <array>
#include <optional>
#include <span>
#include <vector>

class SmCurve;
class SmCylinder;
class SmContext;
namespace step_import::detail
{
struct CylinderSeamCurve
{
    std::size_t key, firstVertex, lastVertex;
    const SmCurve* curve;
    std::array<double, 2> interval;
};
struct CylinderSeamUse
{
    std::size_t edge;
    bool forward;
};
struct CylinderSeamPlan
{
    std::size_t anchorEdge, anchorVertex, cutEdge;
    std::optional<std::size_t> cutVertex;
    double cutParameter = 0, angleRadians = 0; // Native curve t and physical chart phase are distinct parameters.
    Point first{}, last{};
};
// The caller owns NativeMutex, immutable prepared source geometry and the
// allocating context. A private single-face survey copies complete definitions;
// it never edits/adopts an input, changes tolerances or publishes a BRep.
// Caller selects one closed circle plus one mixed source loop. A unique
// meridian interval must connect that circle's original vertex to an existing
// outer vertex or an interior CIRCLE/ELLIPSE point, within definitionMm in
// physical mm. Native curve t and the cylinder's angular chart are not assumed
// to share an affine parameterization; re-evaluate the original conic at t.
// Numerical classification selects a plan, not a continuous source certificate.
// The caller must freeze global subdivisions before constructing ANY real face,
// retain source identity/interval coverage and run the complete original gates.
[[nodiscard]] Result<std::optional<CylinderSeamPlan>> PlanCylinderSeam(
    const SmCylinder& support,
    std::span<const Point> vertices,
    std::span<const CylinderSeamCurve> curves,
    const std::array<std::vector<CylinderSeamUse>, 2>& loops,
    const SmContext& context,
    double definitionMm,
    double workMm,
    std::size_t capacity,
    const ExecutionCheckpoint& checkpoint
);
} // namespace step_import::detail
