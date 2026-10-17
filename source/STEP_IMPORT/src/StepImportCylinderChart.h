// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

namespace step_import::detail
{
struct CylinderGauge
{
    Point originMm, x, y, z;
    double radiusMm, startAngleRad, pitchMm, lowerMm;
    std::array<double, 6> widthsMm;
};
struct CylinderGaugeNet
{
    // U-major, two V control points per U row. Degree (3,1), U knots
    // {0,.25,.5,.75,1,1.25} with multiplicities {4,3,3,3,3,4}; V is [0,1].
    std::vector<Point> pointsMm;
    std::vector<double> weights;
};
// Exact rational cylinder support; z = lower + pitch*u + width(u)*v,
// with width piecewise linear at quarter knots. No fitting or source mutation.
CylinderGaugeNet MakeCylinderGaugeNet(const CylinderGauge& gauge);
} // namespace step_import::detail
