// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <functional>
#include <memory>
#include <span>
#include <vector>
class SmCurve;
class SmSurface;
class SmContext;
namespace step_import::detail::construction
{
class GeometryConstruction;
struct LinearExtrusionSession;
std::shared_ptr<LinearExtrusionSession> MakeLinearExtrusionSession();
bool HasLinearExtrusions(const LinearExtrusionSession& session) noexcept;
struct LinearExtrusionTarget
{
    std::size_t geometry = 0, use = 0;
    const SmSurface* surface = nullptr;
    std::optional<std::size_t> face;
};
// Native pointers are borrowed identity tags only. Temporary auxiliary carriers
// are never dereferenced after destruction; verify only pointers reached through
// the caller's still-live face/selected-curve inventory. Freeze value evidence.
[[nodiscard]] Result<std::vector<ParameterMapping>> VerifyLinearExtrusions(
    const LinearExtrusionSession& session,
    std::span<const LinearExtrusionTarget> targets,
    const SmContext& context,
    const std::function<Result<void>(std::size_t)>& native,
    const std::function<Result<void>(std::size_t, std::size_t)>& work
);
struct LinearExtrusionBoundary
{
    const SmCurve* curve;
    Bounds implicitBounds; // Existing positive carrier-net bound when the defining curve is implicit.
    std::array<double, 2> interval; // Actual retained source-edge interval, including affine LINE extrapolation.
};
// Caller supplies an already constructed source extrusion and two retained
// closed-cycle natural-boundary vertices. Bound the WHOLE straight cut against
// BOTH actual U-boundary isoparameters, without fitting, rephasing or moving a
// vertex. Only clamped degree-one V with equal positive V weights is admitted.
// This is a stored-chart cut bound in mm, not a source-surface certificate.
// Absence retains the ordinary seam refusal; callback/native errors propagate.
[[nodiscard]] Result<std::optional<double>> BoundLinearExtrusionSeam(
    const SmSurface& surface,
    const std::array<Point, 2>& endpoints,
    Fraction maximumMm,
    const std::function<Result<void>()>& native,
    const std::function<Result<void>(std::size_t)>& work,
    const SmCurve* actualCut = nullptr // Final stored degree-one [0,1] cut; includes constructor roundoff.
);
// Pure definition construction in the caller's NativeMutex/context. Original
// B-spline U and scalar V are retained: S(u,v)=C(u)+v*VECTOR, including magnitude
// and source length-unit normalization policy. V is never treated as mm itself.
// Native positive control hulls cover all boundary curves, not only vertices.
// A parameter-curve carrier may instead supply its complete V control hull.
[[nodiscard]] Result<std::unique_ptr<SmSurface>> MakeSourceLinearExtrusion(
    const GeometryConstruction& inputs,
    std::size_t geometry,
    std::size_t use,
    std::span<const LinearExtrusionBoundary> boundaries,
    std::optional<std::array<double, 2>> parameterV = {}
);
} // namespace step_import::detail::construction
