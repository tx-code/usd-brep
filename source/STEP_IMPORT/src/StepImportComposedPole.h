// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportTerminalSpline.h"

namespace step_import::detail
{
struct RevolutionCurveDefinition
{
    terminal_spline::BinaryNet uv, generator;
    std::array<double, 3> originMm{};
    std::array<std::array<double, 3>, 3> frame{}; // Stored X, Y, Z, not assumed exactly orthonormal.
    std::array<double, 4> stepDomain{}; // Umin, Vmin, Umax, Vmax; U degrees, V actual generator t.
    double angleNumerator = 0; // Actual native binary64 PI constant; theta=U*angleNumerator/180.
};
struct ComposedPoleProof
{
    bool established = false;
    std::size_t regions = 0; // Outward interval regions, not exact Bernstein patches or samples.
};
// Prove endpoint-connected visits for the actual stored composition
// O+cos(theta)*(XX^T+YY^T)*(g(V(t))-O)
//   +sin(theta)*(YX^T-XY^T)*(g(V(t))-O)+ZZ^T*(g(V(t))-O).
// Both curves must be finite, clamped, C0+ positive-weight NURBS, degree 1..17.
// No rational 3D cache, idealized frame, pole-axis coincidence or trigonometric
// sample substitutes for the defining data. Whole control-hull containment
// excludes native domain clamping. Failed bounds remain unestablished.
// Single-span positive rational Bezier subcurves additionally use outward
// de Casteljau control hulls to tighten values; original derivative enclosures
// remain. Recursion reuses successful exact-t endpoint bounds on its fixed stack.
//
// Outward binary64 interval bounds require SurfaceProofNumericEnvironment.
// They prove ideal stored defining geometry, not floating evaluator roundoff,
// source exact poles, native validity or publication. Limits/Charge are shared
// with other request-wide certified geometry work; no new geometric allowance.
[[nodiscard]] Result<ComposedPoleProof> CheckComposedPoleVisits(
    const RevolutionCurveDefinition& curve,
    const terminal_spline::PoleVisits& visits,
    const terminal_spline::Limits& limits,
    const terminal_spline::Charge& charge
);
} // namespace step_import::detail
