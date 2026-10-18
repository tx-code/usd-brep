// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalogBuilder.h"

namespace step_semantics::catalog_build::rational_spline
{
namespace
{
constexpr std::string_view Curve[] = {
    "BOUNDED_CURVE",           "B_SPLINE_CURVE",     "B_SPLINE_CURVE_WITH_KNOTS", "CURVE", "GEOMETRIC_REPRESENTATION_ITEM",
    "RATIONAL_B_SPLINE_CURVE", "REPRESENTATION_ITEM"
};
constexpr std::string_view Surface[] = {
    "BOUNDED_SURFACE",     "B_SPLINE_SURFACE", "B_SPLINE_SURFACE_WITH_KNOTS", "GEOMETRIC_REPRESENTATION_ITEM", "RATIONAL_B_SPLINE_SURFACE",
    "REPRESENTATION_ITEM", "SURFACE"
};
} // namespace

constexpr void AddRationalSplineDeclarations(Recipe& catalog)
{
    // Compared fixed N2635 / AP214E3_2010.exp RATIONAL_B_SPLINE_* transport:
    // only weights_data is explicit; weights is DERIVE and has no P21 slot.
    // Admit the external rational+explicit-knot mappings only, not other
    // bounded/uniform/quasi-uniform representations merely sharing a base type.
    Field weights{ "weights_data", "REAL", FieldKind::Aggregate };
    weights.minimum = 2;
    weights.elementKind = FieldKind::Real;
    catalog.declarations.push_back({ "RATIONAL_B_SPLINE_CURVE", { "B_SPLINE_CURVE" }, { weights } });
    weights.nestedAggregates.push_back({ "LIST", 2, {}, false });
    catalog.declarations.push_back({ "RATIONAL_B_SPLINE_SURFACE", { "B_SPLINE_SURFACE" }, { std::move(weights) } });
    catalog.complexSignatures.push_back(Curve);
    catalog.complexSignatures.push_back(Surface);
}
} // namespace step_semantics::catalog_build::rational_spline
