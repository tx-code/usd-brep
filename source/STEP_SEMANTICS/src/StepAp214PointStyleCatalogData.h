// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepAp214PresentationCatalogData.h"

namespace step_semantics::catalog_build::ap214_point_style
{
constexpr std::string_view Markers[] = { "PRE_DEFINED_MARKER" };
constexpr std::string_view MarkerValues[] = { "DOT", "X", "PLUS", "ASTERISK", "RING", "SQUARE", "TRIANGLE" };
constexpr std::string_view Styles[] = { "SURFACE_STYLE_USAGE", "FILL_AREA_STYLE", "CURVE_STYLE", "POINT_STYLE" };
constexpr void AppendDeclarations(Recipe& recipe)
{
    // Fixed AP214E3_2010.exp (71ab140f...), POINT_STYLE has four required
    // slots. The marker SELECT also admits typed MARKER_TYPE; its enum RING
    // differs from the predefined symbol STRING name 'circle'. Shared diamond
    // PRE_DEFINED_ITEM.name occurs once in the predefined point-symbol leaf.
    recipe.declarations.insert(
        recipe.declarations.end(),
        { { "POINT_STYLE",
            { "FOUNDED_ITEM" },
            { { "name", "LABEL", FieldKind::String },
              { "marker", "MARKER_SELECT", FieldKind::MarkerSelect },
              { "marker_size", "SIZE_SELECT", FieldKind::SizeSelect },
              { "marker_colour", "COLOUR", FieldKind::Reference } },
            true },
          { "PRE_DEFINED_MARKER", { "PRE_DEFINED_ITEM" }, {} },
          { "PRE_DEFINED_SYMBOL", { "PRE_DEFINED_ITEM" }, {} },
          { "PRE_DEFINED_POINT_MARKER_SYMBOL", { "PRE_DEFINED_MARKER", "PRE_DEFINED_SYMBOL" }, {}, true } }
    );
    recipe.referenceSelects.push_back({ "MARKER_SELECT", Markers });
    recipe.enumerations.push_back({ "MARKER_TYPE", MarkerValues });
    ap214_presentation::ReplaceSelect(recipe, "PRESENTATION_STYLE_SELECT", Styles);
}
} // namespace step_semantics::catalog_build::ap214_point_style
