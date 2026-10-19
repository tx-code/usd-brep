// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalogBuilder.h"

namespace step_semantics::catalog_build::ap214_presentation
{
namespace
{
constexpr std::string_view Elements[] = { "SURFACE_STYLE_FILL_AREA", "SURFACE_STYLE_RENDERING" };
constexpr std::string_view Properties[] = { "SURFACE_STYLE_TRANSPARENT" };
constexpr std::string_view Methods[] = { "CONSTANT_SHADING", "COLOUR_SHADING", "DOT_SHADING", "NORMAL_SHADING" };
constexpr std::string_view CurveFonts[] = { "PRE_DEFINED_CURVE_FONT" };
constexpr std::string_view PresentationStyles[] = { "SURFACE_STYLE_USAGE", "FILL_AREA_STYLE", "CURVE_STYLE" };

constexpr void ReplaceSelect(Recipe& catalog, std::string_view name, std::span<const std::string_view> branches)
{
    if (!RequireTarget(catalog, catalog.referenceSelects, name))
    {
        return;
    }
    const auto select = std::find_if(
        catalog.referenceSelects.begin(),
        catalog.referenceSelects.end(),
        [&](const auto& entry)
        {
            return entry.name == name;
        }
    );
    if (select == catalog.referenceSelects.end())
    {
        catalog.issue = Error::OverlayTarget;
        return;
    }
    // Change only this catalog's span. The generic declarations are also used
    // by N2635; changing their shared branch array would leak AP214-only types.
    select->branches = branches;
}
} // namespace

constexpr void AddAp214RenderingDeclarations(Recipe& catalog)
{
    // Fixed AP214E3_2010.exp SURFACE_STYLE_RENDERING[_WITH_PROPERTIES] and
    // SURFACE_STYLE_TRANSPARENT. They are not RI/geometry definitions. The
    // rendering method is required: '$' remains an error, never a default.
    Field properties{ "properties", "RENDERING_PROPERTIES_SELECT", FieldKind::Aggregate };
    properties.minimum = 1;
    properties.maximum = 2;
    properties.unique = Unique::Set;
    catalog.declarations.insert(
        catalog.declarations.end(),
        {
            { "SURFACE_STYLE_RENDERING",
              {},
              { { "rendering_method", "SHADING_SURFACE_METHOD", FieldKind::Enumeration }, { "surface_colour", "COLOUR", FieldKind::Reference } },
              true },
            { "SURFACE_STYLE_RENDERING_WITH_PROPERTIES", { "SURFACE_STYLE_RENDERING" }, { std::move(properties) }, true },
            { "SURFACE_STYLE_TRANSPARENT", {}, { { "transparency", "REAL", FieldKind::Real } }, true },
        }
    );
    ReplaceSelect(catalog, "SURFACE_STYLE_ELEMENT_SELECT", Elements);
    catalog.referenceSelects.push_back({ "RENDERING_PROPERTIES_SELECT", Properties });
    catalog.enumerations.push_back({ "SHADING_SURFACE_METHOD", Methods });
}

constexpr void AddAp214CurveStyleDeclarations(Recipe& catalog)
{
    // Pinned AP214E3_2010.exp, not the merged AP schema: all four style
    // attributes are REQUIRED. The outer font SELECT has no scaling branch.
    // This profile admits its predefined branch; custom/external fonts still
    // fail the closed type guard. Parents retain their field provenance but
    // only the draughting leaf is an admitted simple font mapping.
    catalog.declarations.insert(
        catalog.declarations.end(),
        {
            { "PRE_DEFINED_ITEM", {}, { { "name", "LABEL", FieldKind::String } } },
            { "PRE_DEFINED_CURVE_FONT", { "PRE_DEFINED_ITEM" }, {} },
            { "DRAUGHTING_PRE_DEFINED_CURVE_FONT", { "PRE_DEFINED_CURVE_FONT" }, {}, true },
            { "CURVE_STYLE",
              { "FOUNDED_ITEM" },
              { { "name", "LABEL", FieldKind::String },
                { "curve_font", "CURVE_FONT_OR_SCALED_CURVE_FONT_SELECT", FieldKind::Reference },
                { "curve_width", "SIZE_SELECT", FieldKind::SizeSelect },
                { "curve_colour", "COLOUR", FieldKind::Reference } },
              true },
        }
    );
    // Flatten only the admitted leaf of the nested font SELECT for matching;
    // the explicit attribute above retains the original outer declared type.
    catalog.referenceSelects.push_back({ "CURVE_FONT_OR_SCALED_CURVE_FONT_SELECT", CurveFonts });
    ReplaceSelect(catalog, "PRESENTATION_STYLE_SELECT", PresentationStyles);
}

constexpr void AddAp214PredefinedColourDeclarations(Recipe& catalog)
{
    // Pinned AP214E3_2010.exp (71ab140f...), PRE_DEFINED_COLOUR inherits
    // PRE_DEFINED_ITEM before COLOUR. Reuse the font chain's original LABEL
    // slot exactly once; this branch has no RGB channels or derived slots.
    // Only the draughting leaf is admitted. No arbitrary complex mapping or
    // extra SELECT branch follows from this multiple inheritance.
    catalog.declarations.insert(
        catalog.declarations.end(),
        {
            { "PRE_DEFINED_COLOUR", { "PRE_DEFINED_ITEM", "COLOUR" }, {} },
            { "DRAUGHTING_PRE_DEFINED_COLOUR", { "PRE_DEFINED_COLOUR" }, {}, true },
        }
    );
}
} // namespace step_semantics::catalog_build::ap214_presentation
