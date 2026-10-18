// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepDateTimeCatalog.h"

#include <stdexcept>

namespace step_semantics
{
namespace
{
constexpr std::string_view AheadBehind[] = { "AHEAD", "EXACT", "BEHIND" };
constexpr std::string_view Ap203DateItems[] = { "APPROVAL_PERSON_ORGANIZATION", "PRODUCT_DEFINITION", "SECURITY_CLASSIFICATION" };
// AP214E3_2010.exp DATE_AND_TIME_ITEM: the admitted branches, not the complete SELECT.
// Inverse assignment ownership is proved separately; SELECT membership grants no root.
constexpr std::string_view Ap214DateItems[] = { "PRODUCT",
                                                "PRODUCT_DEFINITION",
                                                "PRODUCT_DEFINITION_FORMATION",
                                                "PROPERTY_DEFINITION",
                                                "SHAPE_REPRESENTATION",
                                                "MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION" };
} // namespace

void AddDateTimeDeclarations(Catalog& catalog, ProfileKind profile)
{
    const bool ap203 = profile == ProfileKind::Ap203e2GeneralSource;
    if (!ap203 && !IsAp214SourceProfile(profile))
    {
        throw std::logic_error("Date transport requires an explicit general source profile");
    }
    // Compared N2635 (c68dd022...) and AP214E3_2010.exp (71ab140f...): these
    // explicit field orders are shared. AP214 DERIVE descriptions/offsets are
    // not transport slots. This preserves source calendar/clock/zone values;
    // no epoch conversion, timezone guess or calendar/time WHERE claim follows.
    catalog.declarations.insert(
        catalog.declarations.end(),
        {
            { "DATE", {}, { { "year_component", "YEAR_NUMBER", FieldKind::Integer } } },
            { "CALENDAR_DATE",
              { "DATE" },
              { { "day_component", "DAY_IN_MONTH_NUMBER", FieldKind::Integer }, { "month_component", "MONTH_IN_YEAR_NUMBER", FieldKind::Integer } },
              true },
            { "DATE_AND_TIME",
              {},
              { { "date_component", "DATE", FieldKind::Reference }, { "time_component", "LOCAL_TIME", FieldKind::Reference } },
              true },
            { "LOCAL_TIME",
              {},
              { { "hour_component", "HOUR_IN_DAY", FieldKind::Integer },
                { "minute_component", "MINUTE_IN_HOUR", FieldKind::Integer, true },
                { "second_component", "SECOND_IN_MINUTE", FieldKind::Real, true },
                { "zone", "COORDINATED_UNIVERSAL_TIME_OFFSET", FieldKind::Reference } },
              true },
            { "COORDINATED_UNIVERSAL_TIME_OFFSET",
              {},
              { { "hour_offset", "INTEGER", FieldKind::Integer },
                { "minute_offset", "INTEGER", FieldKind::Integer, true },
                { "sense", "AHEAD_OR_BEHIND", FieldKind::Enumeration } },
              true },
            { "DATE_TIME_ROLE", {}, { { "name", "LABEL", FieldKind::String } }, true },
            { "DATE_AND_TIME_ASSIGNMENT",
              {},
              { { "assigned_date_and_time", "DATE_AND_TIME", FieldKind::Reference }, { "role", "DATE_TIME_ROLE", FieldKind::Reference } } },
        }
    );
    Field items{ "items", ap203 ? "DATE_TIME_ITEM" : "DATE_AND_TIME_ITEM", FieldKind::Aggregate };
    items.minimum = 1;
    items.unique = Unique::Set;
    catalog.declarations.push_back(
        { ap203 ? "CC_DESIGN_DATE_AND_TIME_ASSIGNMENT" : "APPLIED_DATE_AND_TIME_ASSIGNMENT",
          { "DATE_AND_TIME_ASSIGNMENT" },
          { std::move(items) },
          true }
    );
    if (ap203)
    {
        catalog.referenceSelects.push_back({ "DATE_TIME_ITEM", Ap203DateItems });
    }
    else
    {
        catalog.referenceSelects.push_back({ "DATE_AND_TIME_ITEM", Ap214DateItems });
    }
    catalog.enumerations.push_back({ "AHEAD_OR_BEHIND", AheadBehind });
}
} // namespace step_semantics
