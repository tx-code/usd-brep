// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAp214PresentationCatalog.h"

#include <algorithm>
#include <stdexcept>

namespace step_semantics
{
namespace
{
constexpr std::string_view Elements[] = { "SURFACE_STYLE_FILL_AREA", "SURFACE_STYLE_RENDERING" };
constexpr std::string_view Properties[] = { "SURFACE_STYLE_TRANSPARENT" };
constexpr std::string_view Methods[] = { "CONSTANT_SHADING", "COLOUR_SHADING", "DOT_SHADING", "NORMAL_SHADING" };
} // namespace

void AddAp214RenderingDeclarations(Catalog& catalog)
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
    const auto select = std::find_if(
        catalog.referenceSelects.begin(),
        catalog.referenceSelects.end(),
        [](const auto& entry)
        {
            return entry.name == "SURFACE_STYLE_ELEMENT_SELECT";
        }
    );
    if (select == catalog.referenceSelects.end())
    {
        throw std::logic_error("Rendering requires the presentation element SELECT");
    }
    // Replace only this catalog's admitted SELECT span. N2635 and P0 catalogs
    // remain independent; neither acquires these rendering types accidentally.
    select->branches = Elements;
    catalog.referenceSelects.push_back({ "RENDERING_PROPERTIES_SELECT", Properties });
    catalog.enumerations.push_back({ "SHADING_SURFACE_METHOD", Methods });
}
} // namespace step_semantics
