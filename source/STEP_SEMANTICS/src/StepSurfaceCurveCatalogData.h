// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalogBuilder.h"

namespace step_semantics::catalog_build::surface_curve
{
inline constexpr std::string_view AssociatedGeometry[] = { "PCURVE", "SURFACE" };
inline constexpr std::string_view PreferredRepresentation[] = { "CURVE_3D", "PCURVE_S1", "PCURVE_S2" };

constexpr void AppendDeclarations(Recipe& recipe)
{
    // Pinned AP214E3_2010.exp (71ab140f...), these exact entity/SELECT/enum
    // declarations. Append after existing IDs. LIST association order carries
    // pcurve_s1/s2 meaning; repeated references are not a SET. DERIVE basis_surface,
    // axis_line and placement.z have no transmitted P21 slot.
    Field associated{ "associated_geometry", "PCURVE_OR_SURFACE", FieldKind::Aggregate };
    associated.minimum = 1;
    associated.maximum = 2;
    recipe.declarations.insert(
        recipe.declarations.end(),
        {
            { "AXIS1_PLACEMENT", { "PLACEMENT" }, { { "axis", "DIRECTION", FieldKind::Reference, true } }, true },
            { "SWEPT_SURFACE", { "SURFACE" }, { { "swept_curve", "CURVE", FieldKind::Reference } } },
            { "SURFACE_OF_REVOLUTION", { "SWEPT_SURFACE" }, { { "axis_position", "AXIS1_PLACEMENT", FieldKind::Reference } }, true },
            { "DEFINITIONAL_REPRESENTATION", { "REPRESENTATION" }, {}, true },
            { "PCURVE",
              { "CURVE" },
              { { "basis_surface", "SURFACE", FieldKind::Reference }, { "reference_to_curve", "DEFINITIONAL_REPRESENTATION", FieldKind::Reference } },
              true },
            { "SURFACE_CURVE",
              { "CURVE" },
              { { "curve_3d", "CURVE", FieldKind::Reference },
                associated,
                { "master_representation", "PREFERRED_SURFACE_CURVE_REPRESENTATION", FieldKind::Enumeration } },
              true },
        }
    );
    recipe.referenceSelects.push_back({ "PCURVE_OR_SURFACE", AssociatedGeometry });
    recipe.enumerations.push_back({ "PREFERRED_SURFACE_CURVE_REPRESENTATION", PreferredRepresentation });
}
} // namespace step_semantics::catalog_build::surface_curve
