// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAp203GeneralCatalog.h"

namespace step_semantics
{
namespace
{
Field Text(const char* name, bool optional = false, const char* type = "LABEL")
{
    return { name, type, FieldKind::String, optional };
}
Field Ref(const char* name, const char* type)
{
    return { name, type, FieldKind::Reference };
}
Field Integer(const char* name, const char* type = "INTEGER", bool optional = false)
{
    return { name, type, FieldKind::Integer, optional };
}
Field Set(const char* name, const char* type)
{
    Field result{ name, type, FieldKind::Aggregate };
    result.minimum = 1;
    result.unique = Unique::Set;
    return result;
}
Field OptionalNames(const char* name)
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
constexpr std::string_view DateItems[] = { "APPROVAL_PERSON_ORGANIZATION", "PRODUCT_DEFINITION", "SECURITY_CLASSIFICATION" };
constexpr std::string_view Classified[] = { "PRODUCT_DEFINITION_FORMATION" };
constexpr std::string_view PersonItems[] = { "PRODUCT", "PRODUCT_DEFINITION", "PRODUCT_DEFINITION_FORMATION", "SECURITY_CLASSIFICATION" };
constexpr std::string_view Layered[] = { "REPRESENTATION_ITEM" };
constexpr std::string_view AheadBehind[] = { "AHEAD", "EXACT", "BEHIND" };
constexpr std::string_view ConversionAngle[] = { "CONVERSION_BASED_UNIT", "NAMED_UNIT", "PLANE_ANGLE_UNIT" };
constexpr std::string_view ShapeRelationship[] = { "REPRESENTATION_RELATIONSHIP", "SHAPE_REPRESENTATION_RELATIONSHIP" };
} // namespace

void AddAp203GeneralDeclarations(Catalog& catalog)
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
            { "DATE", {}, { Integer("year_component", "YEAR_NUMBER") } },
            { "CALENDAR_DATE",
              { "DATE" },
              { Integer("day_component", "DAY_IN_MONTH_NUMBER"), Integer("month_component", "MONTH_IN_YEAR_NUMBER") },
              true },
            { "DATE_AND_TIME", {}, { Ref("date_component", "DATE"), Ref("time_component", "LOCAL_TIME") }, true },
            { "LOCAL_TIME",
              {},
              { Integer("hour_component", "HOUR_IN_DAY"),
                Integer("minute_component", "MINUTE_IN_HOUR", true),
                { "second_component", "SECOND_IN_MINUTE", FieldKind::Real, true },
                Ref("zone", "COORDINATED_UNIVERSAL_TIME_OFFSET") },
              true },
            { "COORDINATED_UNIVERSAL_TIME_OFFSET",
              {},
              { Integer("hour_offset"), Integer("minute_offset", "INTEGER", true), { "sense", "AHEAD_OR_BEHIND", FieldKind::Enumeration } },
              true },
            { "DATE_TIME_ROLE", {}, { Text("name") }, true },
            { "DATE_AND_TIME_ASSIGNMENT", {}, { Ref("assigned_date_and_time", "DATE_AND_TIME"), Ref("role", "DATE_TIME_ROLE") } },
            { "CC_DESIGN_DATE_AND_TIME_ASSIGNMENT", { "DATE_AND_TIME_ASSIGNMENT" }, { Set("items", "DATE_TIME_ITEM") }, true },
        }
    );
    catalog.referenceSelects.insert(
        catalog.referenceSelects.end(),
        { { "APPROVED_ITEM", Approved },
          { "PERSON_ORGANIZATION_SELECT", PersonOrganization },
          { "DATE_TIME_SELECT", DateTime },
          { "DATE_TIME_ITEM", DateItems },
          { "CC_CLASSIFIED_ITEM", Classified },
          { "CC_PERSON_ORGANIZATION_ITEM", PersonItems },
          { "LAYERED_ITEM", Layered } }
    );
    catalog.enumerations.push_back({ "AHEAD_OR_BEHIND", AheadBehind });
    catalog.complexSignatures.push_back(ConversionAngle);
    catalog.complexSignatures.push_back(ShapeRelationship);
    for (auto& declaration : catalog.declarations)
    {
        if (declaration.name == "PRODUCT_CATEGORY")
        {
            declaration.simple = true;
        }
    }
    // VRI retains its typed value and obtains its unit context from its owning
    // representation; no implicit unit is invented or converted during binding.
    catalog.measureBranches.insert(
        catalog.measureBranches.end(),
        { "PLANE_ANGLE_MEASURE", "AREA_MEASURE", "VOLUME_MEASURE", "NUMERIC_MEASURE", "COUNT_MEASURE" }
    );
}
} // namespace step_semantics
