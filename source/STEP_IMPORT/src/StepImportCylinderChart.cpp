// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportCylinderChart.h"

#include <cmath>
#include <numbers>

namespace step_import::detail
{
CylinderGaugeNet MakeCylinderGaugeNet(const CylinderGauge& g)
{
    CylinderGaugeNet result;
    const double w = std::sqrt(0.5);
    for (unsigned quarter = 0; quarter < 5; ++quarter)
    {
        const double a = g.startAngleRad + quarter * std::numbers::pi / 2, b = g.startAngleRad + (quarter + 1) * std::numbers::pi / 2;
        const std::array<double, 3> wx{ g.radiusMm * std::cos(a), g.radiusMm * (std::cos(a) + std::cos(b)) * w, g.radiusMm * std::cos(b) };
        const std::array<double, 3> wy{ g.radiusMm * std::sin(a), g.radiusMm * (std::sin(a) + std::sin(b)) * w, g.radiusMm * std::sin(b) };
        const std::array<double, 3> ww{ 1, w, 1 };
        for (unsigned k = 0; k < 4; ++k)
        {
            if (quarter && k == 0)
            {
                continue;
            }
            const double alpha = double(k) / 3;
            const auto elevate = [&](const std::array<double, 3>& v)
            {
                return k == 0 ? v[0] : k == 3 ? v[2] : alpha * v[k - 1] + (1 - alpha) * v[k];
            };
            const double weight = elevate(ww), x = elevate(wx) / weight, y = elevate(wy) / weight;
            // Degree elevation retains the homogeneous circle polynomials. For
            // degree-2 W, the degree-3 coefficients of s*W are k/3*W[k-1].
            // Both axial gauges use that product; X/Y and radius stay unchanged.
            const double term = k ? alpha * ww[k - 1] / weight : 0;
            const double z = g.lowerMm + g.pitchMm * quarter / 4 + g.pitchMm * term / 4;
            const double width = g.widthsMm[quarter] + (g.widthsMm[quarter + 1] - g.widthsMm[quarter]) * term;
            Point point = g.originMm;
            for (std::size_t i = 0; i < 3; ++i)
            {
                point[i] += g.x[i] * x + g.y[i] * y + g.z[i] * z;
            }
            result.pointsMm.push_back(point);
            for (std::size_t i = 0; i < 3; ++i)
            {
                point[i] += g.z[i] * width;
            }
            result.pointsMm.push_back(point);
            result.weights.push_back(weight);
            result.weights.push_back(weight);
        }
    }
    return result;
}
} // namespace step_import::detail
