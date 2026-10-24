// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportTerminalSpline.h"

namespace step_import::detail
{
// Stored STEP evaluation frame versus exact normalized source axis. The bound
// covers every angle and the WHOLE positive generator net's new domain, in mm.
// It consumes no extra definition allowance: its caller adds this to the
// generator certificate within the same request quality budget.
[[nodiscard]] Result<double> TerminalRevolutionFrameBound(
    const std::array<std::string, 3>& sourceAxis,
    const Point& parsedSourceAxis,
    const std::array<std::string, 3>& sourceOrigin,
    const terminal_spline::IntegerRatio& lengthToMm,
    const Point& actualOrigin,
    const std::array<Point, 3>& actualFrame,
    const terminal_spline::BinaryNet& generator,
    const terminal_spline::Limits& limits,
    const terminal_spline::Charge& charge
);
} // namespace step_import::detail
