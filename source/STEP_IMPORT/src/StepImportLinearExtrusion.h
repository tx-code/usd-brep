// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <memory>
#include <span>
class SmCurve;
class SmSurface;
namespace step_import::detail::construction
{
class GeometryConstruction;
struct LinearExtrusionBoundary
{
    const SmCurve* curve;
    Bounds implicitBounds; // Existing positive carrier-net bound when the defining curve is implicit.
    std::array<double, 2> interval; // Actual retained source-edge interval, including affine LINE extrapolation.
};
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
