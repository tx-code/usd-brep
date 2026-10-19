// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepAp203N916Profile.h"
#include "StepCatalogBuilder.h"

namespace step_semantics::catalog_build::ap203_n916
{
// Fixed modified N916/STEPcode87e1f994..., SHA020b4d25... . Common geometry
// transport was compared with N2635; source restrictions below remain distinct.
// These114 names are the complete admitted intersection of the shared recipe
// with N916. The20 foreign descriptors are diagnostic transport only. A future
// shared registration cannot silently become a legal legacy declaration.
inline constexpr std::string_view Declarations[] = { "REPRESENTATION_ITEM",
                                                     "GEOMETRIC_REPRESENTATION_ITEM",
                                                     "TOPOLOGICAL_REPRESENTATION_ITEM",
                                                     "POINT",
                                                     "CARTESIAN_POINT",
                                                     "DIRECTION",
                                                     "VECTOR",
                                                     "PLACEMENT",
                                                     "AXIS2_PLACEMENT_3D",
                                                     "CURVE",
                                                     "LINE",
                                                     "SURFACE",
                                                     "ELEMENTARY_SURFACE",
                                                     "PLANE",
                                                     "VERTEX",
                                                     "VERTEX_POINT",
                                                     "EDGE",
                                                     "EDGE_CURVE",
                                                     "ORIENTED_EDGE",
                                                     "LOOP",
                                                     "PATH",
                                                     "EDGE_LOOP",
                                                     "FACE_BOUND",
                                                     "FACE_OUTER_BOUND",
                                                     "FACE",
                                                     "FACE_SURFACE",
                                                     "ADVANCED_FACE",
                                                     "CONNECTED_FACE_SET",
                                                     "OPEN_SHELL",
                                                     "CLOSED_SHELL",
                                                     "SOLID_MODEL",
                                                     "MANIFOLD_SOLID_BREP",
                                                     "SHELL_BASED_SURFACE_MODEL",
                                                     "REPRESENTATION",
                                                     "SHAPE_REPRESENTATION",
                                                     "ADVANCED_BREP_SHAPE_REPRESENTATION",
                                                     "MANIFOLD_SURFACE_SHAPE_REPRESENTATION",
                                                     "APPLICATION_CONTEXT",
                                                     "APPLICATION_CONTEXT_ELEMENT",
                                                     "APPLICATION_PROTOCOL_DEFINITION",
                                                     "PRODUCT_CONTEXT",
                                                     "MECHANICAL_CONTEXT",
                                                     "PRODUCT_DEFINITION_CONTEXT",
                                                     "DESIGN_CONTEXT",
                                                     "PRODUCT",
                                                     "PRODUCT_DEFINITION_FORMATION",
                                                     "PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE",
                                                     "PRODUCT_DEFINITION",
                                                     "PROPERTY_DEFINITION",
                                                     "PRODUCT_DEFINITION_SHAPE",
                                                     "PROPERTY_DEFINITION_REPRESENTATION",
                                                     "SHAPE_DEFINITION_REPRESENTATION",
                                                     "PRODUCT_CATEGORY",
                                                     "PRODUCT_RELATED_PRODUCT_CATEGORY",
                                                     "REPRESENTATION_CONTEXT",
                                                     "GEOMETRIC_REPRESENTATION_CONTEXT",
                                                     "GLOBAL_UNIT_ASSIGNED_CONTEXT",
                                                     "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT",
                                                     "NAMED_UNIT",
                                                     "SI_UNIT",
                                                     "LENGTH_UNIT",
                                                     "PLANE_ANGLE_UNIT",
                                                     "SOLID_ANGLE_UNIT",
                                                     "CONVERSION_BASED_UNIT",
                                                     "DIMENSIONAL_EXPONENTS",
                                                     "MEASURE_WITH_UNIT",
                                                     "LENGTH_MEASURE_WITH_UNIT",
                                                     "UNCERTAINTY_MEASURE_WITH_UNIT",
                                                     "MASS_UNIT",
                                                     "FOUNDED_ITEM",
                                                     "MASS_MEASURE_WITH_UNIT",
                                                     "REPRESENTATION_RELATIONSHIP",
                                                     "SHAPE_REPRESENTATION_RELATIONSHIP",
                                                     "CONIC",
                                                     "CIRCLE",
                                                     "ELLIPSE",
                                                     "CYLINDRICAL_SURFACE",
                                                     "CONICAL_SURFACE",
                                                     "BOUNDED_CURVE",
                                                     "B_SPLINE_CURVE",
                                                     "B_SPLINE_CURVE_WITH_KNOTS",
                                                     "BOUNDED_SURFACE",
                                                     "B_SPLINE_SURFACE",
                                                     "B_SPLINE_SURFACE_WITH_KNOTS",
                                                     "PLANE_ANGLE_MEASURE_WITH_UNIT",
                                                     "APPROVAL_STATUS",
                                                     "APPROVAL",
                                                     "APPROVAL_ASSIGNMENT",
                                                     "CC_DESIGN_APPROVAL",
                                                     "APPROVAL_ROLE",
                                                     "APPROVAL_PERSON_ORGANIZATION",
                                                     "APPROVAL_DATE_TIME",
                                                     "PERSON",
                                                     "ORGANIZATION",
                                                     "PERSON_AND_ORGANIZATION",
                                                     "PERSON_AND_ORGANIZATION_ROLE",
                                                     "PERSON_AND_ORGANIZATION_ASSIGNMENT",
                                                     "CC_DESIGN_PERSON_AND_ORGANIZATION_ASSIGNMENT",
                                                     "SECURITY_CLASSIFICATION_LEVEL",
                                                     "SECURITY_CLASSIFICATION",
                                                     "SECURITY_CLASSIFICATION_ASSIGNMENT",
                                                     "CC_DESIGN_SECURITY_CLASSIFICATION",
                                                     "DATE",
                                                     "CALENDAR_DATE",
                                                     "DATE_AND_TIME",
                                                     "LOCAL_TIME",
                                                     "COORDINATED_UNIVERSAL_TIME_OFFSET",
                                                     "DATE_TIME_ROLE",
                                                     "DATE_AND_TIME_ASSIGNMENT",
                                                     "CC_DESIGN_DATE_AND_TIME_ASSIGNMENT",
                                                     "RATIONAL_B_SPLINE_CURVE",
                                                     "RATIONAL_B_SPLINE_SURFACE",
                                                     "TOROIDAL_SURFACE",
                                                     "TRIMMED_CURVE" };
inline constexpr std::string_view Unit[] = { "NAMED_UNIT" };
inline constexpr std::string_view Approved[] = { "PRODUCT_DEFINITION_FORMATION", "PRODUCT_DEFINITION", "SECURITY_CLASSIFICATION" };
inline constexpr std::string_view Persons[] = { "PRODUCT", "PRODUCT_DEFINITION_FORMATION", "PRODUCT_DEFINITION", "SECURITY_CLASSIFICATION" };
inline constexpr std::string_view Classified[] = { "PRODUCT_DEFINITION_FORMATION" };
inline constexpr std::string_view Dated[] = { "PRODUCT_DEFINITION", "APPROVAL_PERSON_ORGANIZATION", "SECURITY_CLASSIFICATION" };
inline constexpr std::string_view AheadBehind[] = { "AHEAD", "BEHIND" };
inline constexpr std::string_view Measures[] = { "LENGTH_MEASURE", "POSITIVE_LENGTH_MEASURE", "MASS_MEASURE", "PLANE_ANGLE_MEASURE",
                                                 "AREA_MEASURE",   "VOLUME_MEASURE",          "COUNT_MEASURE" };

constexpr void SetField(Recipe& recipe, std::string_view owner, std::string_view name, std::string_view type, bool optional)
{
    if (!RequireTarget(recipe, recipe.declarations, owner))
    {
        return;
    }
    for (auto& declaration : recipe.declarations)
    {
        if (declaration.name != owner)
        {
            continue;
        }
        if (!RequireTarget(recipe, declaration.fields, name))
        {
            return;
        }
        for (auto& field : declaration.fields)
        {
            if (field.name == name)
            {
                field.type = type;
                field.optional = optional;
            }
        }
    }
}
constexpr void SetSelect(Recipe& recipe, std::string_view previous, std::string_view name, std::span<const std::string_view> branches)
{
    if (!RequireTarget(recipe, recipe.referenceSelects, previous))
    {
        return;
    }
    for (auto& select : recipe.referenceSelects)
    {
        if (select.name == previous)
        {
            select.name = name;
            select.branches = branches;
        }
    }
}
constexpr Recipe Apply(Recipe recipe)
{
    const auto foreign = std::span(step_semantics::ap203_n916::ForeignDeclarations);
    if (recipe.declarations.size() != std::size(Declarations) + foreign.size())
    {
        recipe.issue = Error::OverlayTarget;
    }
    for (const auto name : Declarations)
    {
        RequireTarget(recipe, recipe.declarations, name);
    }
    for (const auto name : foreign)
    {
        RequireTarget(recipe, recipe.declarations, name);
    }
    SetField(recipe, "APPLICATION_CONTEXT", "application", "TEXT", false);
    for (const auto name : { "PRODUCT",
                             "PRODUCT_DEFINITION_FORMATION",
                             "PRODUCT_DEFINITION",
                             "PROPERTY_DEFINITION",
                             "UNCERTAINTY_MEASURE_WITH_UNIT",
                             "REPRESENTATION_RELATIONSHIP",
                             "ORGANIZATION" })
    {
        SetField(recipe, name, "description", "TEXT", false);
    }
    SetField(recipe, "PROPERTY_DEFINITION_REPRESENTATION", "definition", "PROPERTY_DEFINITION", false);
    SetField(recipe, "CC_DESIGN_PERSON_AND_ORGANIZATION_ASSIGNMENT", "items", "PERSON_ORGANIZATION_ITEM", false);
    SetField(recipe, "CC_DESIGN_SECURITY_CLASSIFICATION", "items", "CLASSIFIED_ITEM", false);
    SetField(recipe, "COORDINATED_UNIVERSAL_TIME_OFFSET", "hour_offset", "HOUR_IN_DAY", false);
    SetField(recipe, "COORDINATED_UNIVERSAL_TIME_OFFSET", "minute_offset", "MINUTE_IN_HOUR", true);
    SetSelect(recipe, "UNIT", "UNIT", Unit);
    SetSelect(recipe, "APPROVED_ITEM", "APPROVED_ITEM", Approved);
    SetSelect(recipe, "CC_PERSON_ORGANIZATION_ITEM", "PERSON_ORGANIZATION_ITEM", Persons);
    SetSelect(recipe, "CC_CLASSIFIED_ITEM", "CLASSIFIED_ITEM", Classified);
    SetSelect(recipe, "DATE_TIME_ITEM", "DATE_TIME_ITEM", Dated);
    if (RequireTarget(recipe, recipe.enumerations, "AHEAD_OR_BEHIND"))
    {
        for (auto& entry : recipe.enumerations)
        {
            if (entry.name == "AHEAD_OR_BEHIND")
            {
                entry.branches = AheadBehind;
            }
        }
    }
    recipe.measureBranches.assign(std::begin(Measures), std::end(Measures));
    // No CBU derived override is introduced: N916 transmits explicit dimensions.
    // Full management/date TYPE predicates and global rules are not implemented.
    return recipe;
}
} // namespace step_semantics::catalog_build::ap203_n916
