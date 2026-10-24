// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportTerminalSpline.h"

namespace step_import::detail::terminal_spline
{
struct AffineLineDefinition
{
    std::array<double, 3> originMm{}, direction{};
    double scale = 1;
    std::array<double, 2> naturalInterval{}, stepAtNaturalEnds{};
    // The bounded native SmLine conversion has a tiny-domain branch s=t,
    // ignoring InsideOut. The adapter selects the ACTUAL native branch. The
    // other branch maps naturalInterval to stepAtNaturalEnds, which may reverse.
    bool directParameter = false;
};
// Prove the actual stored analytic definition origin+s(t)*scale*direction.
// Exact ratios form the SELECTED endpoints; no double EvaluatePoint result or
// rounded NURBS cache is substituted for them. The shared closed-ball proof,
// capacities and Charge contract are those of CheckPoleVisits. This does not
// certify floating evaluator rounding, native validity or STEP-source semantics.
[[nodiscard]] Result<PoleVisitProof> CheckLinePoleVisits(
    const AffineLineDefinition& line,
    const PoleVisits& visits,
    const Limits& limits,
    const Charge& charge
);
} // namespace step_import::detail::terminal_spline
