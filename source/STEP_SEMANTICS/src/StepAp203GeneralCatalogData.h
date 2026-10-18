// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalogBuilder.h"
#include "StepDateTimeCatalogData.h"

namespace step_semantics::catalog_build::ap203_general
{
namespace
{
constexpr Field Text(const char* name, bool optional = false, const char* type = "LABEL")
{
    return { name, type, FieldKind::String, optional };
}
constexpr Field Ref(const char* name, const char* type)
{
    return { name, type, FieldKind::Reference };
}
constexpr Field Set(const char* name, const char* type)
{
    Field result{ name, type, FieldKind::Aggregate };
    result.minimum = 1;
    result.unique = Unique::Set;
    return result;
}
constexpr Field OptionalNames(const char* name)
{
    Field result{ name, "LABEL", FieldKind::Aggregate, true };
    result.elementKind = FieldKind::String;
    result.minimum = 1;
    return result;
}
// N2635 exact SELECT branches, restricted to admitted declarations. In particular
// APPROVED_ITEM does NOT contain PD/formation: legacy exporter violations must
// remain visible and require a later selected-part/nonphysical attachment proof.
constexpr std::string_view Approved[] = { "PRODUCT", "SECURITY_CLASSIFICATION" };
constexpr std::string_view PersonOrganization[] = { "PERSON", "ORGANIZATION", "PERSON_AND_ORGANIZATION" };
constexpr std::string_view DateTime[] = { "DATE", "DATE_AND_TIME", "LOCAL_TIME" };
constexpr std::string_view Classified[] = { "PRODUCT_DEFINITION_FORMATION" };
constexpr std::string_view PersonItems[] = { "PRODUCT", "PRODUCT_DEFINITION", "PRODUCT_DEFINITION_FORMATION", "SECURITY_CLASSIFICATION" };
constexpr std::string_view Layered[] = { "REPRESENTATION_ITEM" };
constexpr std::string_view ConversionAngle[] = { "CONVERSION_BASED_UNIT", "NAMED_UNIT", "PLANE_ANGLE_UNIT" };
constexpr std::string_view ShapeRelationship[] = { "REPRESENTATION_RELATIONSHIP", "SHAPE_REPRESENTATION_RELATIONSHIP" };
} // namespace

constexpr void AddAp203GeneralDeclarations(Recipe& catalog)
{
    // Fixed MBx N2635 (c68dd022...): transport fields and inverse attachment
    // declarations, not a general management WHERE/global-rule implementation.
    catalog.declarations.insert(
        catalog.declarations.end(),
        {
            { "VALUE_REPRESENTATION_ITEM", { "REPRESENTATION_ITEM" }, { { "value_component", "MEASURE_VALUE", FieldKind::Measure } }, true },
            { "PLANE_ANGLE_MEASURE_WITH_UNIT", { "MEASURE_WITH_UNIT" }, {}, true },
            { "PRESENTATION_LAYER_ASSIGNMENT", {}, { Text("name"), Text("description", false, "TEXT"), Set("assigned_items", "LAYERED_ITEM") }, true },
            { "PRE_DEFINED_ITEM", {}, { Text("name") } },
            { "PRE_DEFINED_COLOUR", { "PRE_DEFINED_ITEM", "COLOUR" }, {} },
            { "DRAUGHTING_PRE_DEFINED_COLOUR", { "PRE_DEFINED_COLOUR" }, {}, true },
            { "APPROVAL_STATUS", {}, { Text("name") }, true },
            { "APPROVAL", {}, { Ref("status", "APPROVAL_STATUS"), Text("level") }, true },
            { "APPROVAL_ASSIGNMENT", {}, { Ref("assigned_approval", "APPROVAL") } },
            { "CC_DESIGN_APPROVAL", { "APPROVAL_ASSIGNMENT" }, { Set("items", "APPROVED_ITEM") }, true },
            { "APPROVAL_ROLE", {}, { Text("role") }, true },
            { "APPROVAL_PERSON_ORGANIZATION",
              {},
              { Ref("person_organization", "PERSON_ORGANIZATION_SELECT"), Ref("authorized_approval", "APPROVAL"), Ref("role", "APPROVAL_ROLE") },
              true },
            { "APPROVAL_DATE_TIME", {}, { Ref("date_time", "DATE_TIME_SELECT"), Ref("dated_approval", "APPROVAL") }, true },
            { "PERSON",
              {},
              { Text("id", false, "IDENTIFIER"),
                Text("last_name", true),
                Text("first_name", true),
                OptionalNames("middle_names"),
                OptionalNames("prefix_titles"),
                OptionalNames("suffix_titles") },
              true },
            { "ORGANIZATION", {}, { Text("id", true, "IDENTIFIER"), Text("name"), Text("description", true, "TEXT") }, true },
            { "PERSON_AND_ORGANIZATION", {}, { Ref("the_person", "PERSON"), Ref("the_organization", "ORGANIZATION") }, true },
            { "PERSON_AND_ORGANIZATION_ROLE", {}, { Text("name") }, true },
            { "PERSON_AND_ORGANIZATION_ASSIGNMENT",
              {},
              { Ref("assigned_person_and_organization", "PERSON_AND_ORGANIZATION"), Ref("role", "PERSON_AND_ORGANIZATION_ROLE") } },
            { "CC_DESIGN_PERSON_AND_ORGANIZATION_ASSIGNMENT",
              { "PERSON_AND_ORGANIZATION_ASSIGNMENT" },
              { Set("items", "CC_PERSON_ORGANIZATION_ITEM") },
              true },
            { "SECURITY_CLASSIFICATION_LEVEL", {}, { Text("name") }, true },
            { "SECURITY_CLASSIFICATION",
              {},
              { Text("name"), Text("purpose", false, "TEXT"), Ref("security_level", "SECURITY_CLASSIFICATION_LEVEL") },
              true },
            { "SECURITY_CLASSIFICATION_ASSIGNMENT", {}, { Ref("assigned_security_classification", "SECURITY_CLASSIFICATION") } },
            { "CC_DESIGN_SECURITY_CLASSIFICATION", { "SECURITY_CLASSIFICATION_ASSIGNMENT" }, { Set("items", "CC_CLASSIFIED_ITEM") }, true },
        }
    );
    date_time::AddDateTimeDeclarations(catalog, ProfileKind::Ap203e2GeneralSource);
    catalog.referenceSelects.insert(
        catalog.referenceSelects.end(),
        { { "APPROVED_ITEM", Approved },
          { "PERSON_ORGANIZATION_SELECT", PersonOrganization },
          { "DATE_TIME_SELECT", DateTime },
          { "CC_CLASSIFIED_ITEM", Classified },
          { "CC_PERSON_ORGANIZATION_ITEM", PersonItems },
          { "LAYERED_ITEM", Layered } }
    );
    catalog.complexSignatures.push_back(ConversionAngle);
    catalog.complexSignatures.push_back(ShapeRelationship);
    SetSimple(catalog, "PRODUCT_CATEGORY");
    // VRI retains its typed value and obtains its unit context from its owning
    // representation; no implicit unit is invented or converted during binding.
    catalog.measureBranches.insert(
        catalog.measureBranches.end(),
        { "PLANE_ANGLE_MEASURE", "AREA_MEASURE", "VOLUME_MEASURE", "NUMERIC_MEASURE", "COUNT_MEASURE" }
    );
}
} // namespace step_semantics::catalog_build::ap203_general
