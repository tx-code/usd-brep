// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstddef>
#include <functional>
#include <optional>
class SmBSplineSurface;
class SmCurve;
namespace step_import::detail
{
struct SurfaceRestrictionProof
{
    bool established = false;
    bool workLimit = false;
    std::optional<double> maximumMm;
    std::size_t patches = 0;
};
// Compare the native binary64 definitions on the target's whole natural domain.
// Positive rational weights and clamped, compatible degrees are prerequisites.
// Outward interval common-knot refinement and Bernstein coefficient bounds are
// continuous evidence, not sampling. Uncertain arithmetic returns unestablished.
// Limits bound temporary controls and product terms; callbacks may cancel/throw.
SurfaceRestrictionProof ProveSurfaceRestriction(
    const SmBSplineSurface& original,
    const SmBSplineSurface& restricted,
    double maximumMm,
    std::size_t maximumControls,
    std::size_t maximumProducts,
    const std::function<void()>& checkpoint
);
struct SurfaceInjectivityProof
{
    bool established = false, workLimit = false;
    std::array<std::array<double, 3>, 2> projection{};
    std::array<double, 4> domain{};
    std::optional<double> firstDiagonalLower, secondDiagonalLower, symmetricDeterminantLower;
    std::size_t productsUsed = 0;
};
// Sufficient global proof: a fixed planar projection of the polynomial native
// surface has uniformly positive-definite symmetric Jacobian on its whole
// natural rectangle. Only the plain B-spline evaluator is admitted, not an
// analytic subclass exposing a differently parameterized NURBS cache.
// Strong monotonicity implies injectivity; failure to prove
// it does not establish a folded surface. No sampling or geometry mutation.
SurfaceInjectivityProof ProveSurfaceInjectivity(
    const SmBSplineSurface& surface,
    std::size_t maximumControls,
    std::size_t maximumProducts,
    const std::function<void()>& checkpoint
);
struct CurveLiftProof
{
    bool established = false, workLimit = false;
    std::optional<double> maximumMm;
    std::size_t cells = 0;
    std::size_t productsUsed = 0;
};
// Whole-interval native binary64 curve -> UV -> polynomial surface difference.
// Only identity-parameter B-spline/line evaluators are admitted. Subdivision and
// outward interval de Boor hulls bound every t, not just selected samples.
CurveLiftProof ProveCurveLift(
    const SmCurve& edge,
    const SmCurve& uv,
    const SmBSplineSurface& surface,
    double begin,
    double end,
    double maximumMm,
    std::size_t maximumControls,
    std::size_t maximumProducts,
    const std::function<void()>& checkpoint
);
} // namespace step_import::detail
