// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalogBuilder.h"

namespace step_semantics::catalog_build::ap214_value
{
constexpr void AppendDeclarations(Recipe& recipe)
{
    // Pinned AP214E3_2010.exp (71ab140f...), VALUE_REPRESENTATION_ITEM:
    // inherited RI.name plus value_component:measure_value; no unit slot.
    // The owning using_representations contexts supply the source unit role.
    recipe.declarations.push_back(
        { "VALUE_REPRESENTATION_ITEM", { "REPRESENTATION_ITEM" }, { { "value_component", "MEASURE_VALUE", FieldKind::Measure } }, true }
    );
    // Complete fixed MEASURE_VALUE transport. Seven previously registered
    // alternatives retain their order. Binding these scalars does not grant
    // dimensional interpretation to every MEASURE_WITH_UNIT branch: that
    // existing consumer keeps its explicit unsupported-dimension guard.
    recipe.measureBranches.insert(
        recipe.measureBranches.end(),
        { "AMOUNT_OF_SUBSTANCE_MEASURE",
          "AREA_MEASURE",
          "CELSIUS_TEMPERATURE_MEASURE",
          "CONTEXT_DEPENDENT_MEASURE",
          "COUNT_MEASURE",
          "DESCRIPTIVE_MEASURE",
          "ELECTRIC_CURRENT_MEASURE",
          "LUMINOUS_INTENSITY_MEASURE",
          "NUMERIC_MEASURE",
          "PARAMETER_VALUE",
          "POSITIVE_PLANE_ANGLE_MEASURE",
          "SOLID_ANGLE_MEASURE",
          "THERMODYNAMIC_TEMPERATURE_MEASURE",
          "TIME_MEASURE",
          "VOLUME_MEASURE" }
    );
}
} // namespace step_semantics::catalog_build::ap214_value
