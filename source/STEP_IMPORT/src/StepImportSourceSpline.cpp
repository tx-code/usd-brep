// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportGeometryConstruction.h"
#include "StepImportTerminalSpline.h"
namespace step_import::detail::construction
{
namespace ts = terminal_spline;
// Reuse checked typed source fields and the selected exact unit interpretation.
// This is the existing trimmed-curve reader, moved without changing its charge
// order or legacy diagnostic keys. No native reconstruction or second parser.
Result<ts::Source> GeometryConstruction::ReadExactSplineSource(Index basis, Index use, const ts::Limits& limits, const ts::Charge& charge)
{
    ts::Source source;
    source.lengthToMm = { std::to_string(m_report.units->lengthToMm.Numerator()), std::to_string(m_report.units->lengthToMm.Denominator()) };
    auto degree = Degree(basis, "degree");
    if (!degree)
    {
        return tl::unexpected(std::move(degree.error()));
    }
    source.u.degree = *degree;
    auto multiplicities = Multiplicities(basis, "knot_multiplicities");
    if (!multiplicities)
    {
        return tl::unexpected(std::move(multiplicities.error()));
    }
    for (ULONG i = 0; i < multiplicities->GetSize(); ++i)
    {
        source.u.multiplicities.push_back((*multiplicities)[i]);
    }
    for (const auto& knot : F(basis, "knots").children)
    {
        if (auto spent = charge(1); !spent)
        {
            return tl::unexpected(std::move(spent.error()));
        }
        source.u.knots.push_back(knot.text);
    }
    const auto controls = Refs(basis, "control_points_list");
    source.countU = controls.size();
    if (source.countU > limits.controlSlots)
    {
        return tl::unexpected(Fail("GeneralWireSplineSourceControlLimit", use, Status::ResourceLimit));
    }
    if (auto spent = charge(source.countU); !spent)
    {
        return tl::unexpected(std::move(spent.error()));
    }
    source.controls.reserve(source.countU);
    for (const auto control : controls)
    {
        const auto& coordinates = F(control, "coordinates").children;
        if (coordinates.size() != 3)
        {
            return tl::unexpected(Fail("GeneralWireSplineControlDimension", control, Status::Unsupported));
        }
        source.controls.push_back({ coordinates[0].text, coordinates[1].text, coordinates[2].text });
    }
    if (Is(basis, "RATIONAL_B_SPLINE_CURVE"))
    {
        for (const auto& weight : F(basis, "weights_data").children)
        {
            if (auto spent = charge(1); !spent)
            {
                return tl::unexpected(std::move(spent.error()));
            }
            source.weights.push_back(weight.text);
        }
    }
    return source;
}
} // namespace step_import::detail::construction
