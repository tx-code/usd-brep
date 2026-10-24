// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSchemaSelection.h"

#include <algorithm>
#include <array>
#include <gsl/assert>

namespace step_semantics
{
namespace
{
bool Space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}
std::vector<std::string> Words(std::string_view text)
{
    std::vector<std::string> words;
    std::size_t position = 0;
    while (position < text.size())
    {
        if (Space(text[position]))
        {
            ++position;
            continue;
        }
        const auto begin = position;
        while (position < text.size() && !Space(text[position]))
        {
            ++position;
        }
        words.emplace_back(text.substr(begin, position - begin));
    }
    return words;
}
bool Letter(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
bool Digit(char c)
{
    return c >= '0' && c <= '9';
}
struct Version
{
    std::string_view name, oid, id;
};
constexpr std::array versions{ Version{ SchemaName, "1 0 10303 403 2 1 2", "ap203e2-n2635" },
                               Version{ "AUTOMOTIVE_DESIGN", "1 0 10303 214 1 1 1", "ap214-edition-1" },
                               Version{ "AUTOMOTIVE_DESIGN", "1 0 10303 214 2 1 1", "ap214-edition-2" },
                               Version{ "AUTOMOTIVE_DESIGN", "1 0 10303 214 3 1 1", "ap214-edition-3" },
                               // MBx-IF's EXPRESS Schemas table binds this exact eight-arc identifier
                               // to AP214E3_2010.zip. The linked file's own header instead declares
                               // version(3), so give the published binding a distinct identity. Never
                               // drop an arc, infer an edition, or rewrite the captured HEADER.
                               Version{ "AUTOMOTIVE_DESIGN", "1 0 10303 214 1 1 1 1", "ap214-mbx-2010" } };

SchemaIdentifier Identify(const step_source::Value& value)
{
    SchemaIdentifier result;
    result.literal = value.text;
    result.span = value.span;
    const auto& literal = value.text;
    const auto opening = literal.find('{'), closing = literal.find('}');
    const bool hasOid = opening != std::string::npos || closing != std::string::npos;
    auto names = Words(std::string_view(literal).substr(0, hasOid ? opening : literal.size()));
    if (names.size() != 1)
    {
        return result;
    }
    result.name = std::move(names[0]);
    if (!Letter(result.name.front()) || !std::all_of(
                                            result.name.begin(),
                                            result.name.end(),
                                            [](char c)
                                            {
                                                return Letter(c) || Digit(c);
                                            }
                                        ))
    {
        return result;
    }
    if (hasOid)
    {
        if (opening == std::string::npos || closing == std::string::npos || opening >= closing ||
            std::count(literal.begin(), literal.end(), '{') != 1 || std::count(literal.begin(), literal.end(), '}') != 1 ||
            !Words(std::string_view(literal).substr(closing + 1)).empty())
        {
            return result;
        }
        result.oid = Words(std::string_view(literal).substr(opening + 1, closing - opening - 1));
        if (result.oid.empty() || !std::all_of(
                                      result.oid.begin(),
                                      result.oid.end(),
                                      [](const auto& arc)
                                      {
                                          return std::all_of(arc.begin(), arc.end(), Digit);
                                      }
                                  ))
        {
            return result;
        }
    }
    if (result.name == SchemaName)
    {
        result.protocol = Protocol::Ap203e2;
    }
    else if (result.name == "CONFIG_CONTROL_DESIGN")
    {
        result.protocol = Protocol::Ap203e1;
    }
    else if (result.name == "AUTOMOTIVE_DESIGN" || result.name == "AUTOMOTIVE_DESIGN_CC2")
    {
        result.protocol = Protocol::Ap214;
    }
    else if (result.name == "AP242_MANAGED_MODEL_BASED_3D_ENGINEERING_MIM_LF")
    {
        result.protocol = Protocol::Ap242;
    }
    result.match = result.protocol == Protocol::Unknown ? SchemaMatch::UnknownName :
                   hasOid                               ? SchemaMatch::UnregisteredVersion :
                                                          SchemaMatch::NameOnly;
    if (!hasOid && result.name == "AUTOMOTIVE_DESIGN_CC2")
    {
        result.match = SchemaMatch::LegacyAlias;
        result.registryId = "ap214-legacy-cc2";
    }
    for (const auto& entry : versions)
    {
        if (result.name == entry.name && result.oid == Words(entry.oid))
        {
            result.match = SchemaMatch::RegisteredVersion;
            result.registryId = entry.id;
            break;
        }
    }
    return result;
}
} // namespace

SchemaSelection SelectSchema(const step_source::SourceSnapshot& source, GeometryScope scope)
{
    // An owning SourceSnapshot is published only after the raw HEADER shape checks.
    Expects(source.Header().size() == 3);
    SchemaSelection selection;
    for (const auto& value : source.Header()[2].parameters[0].children)
    {
        selection.identifiers.push_back(Identify(value));
    }
    if (selection.identifiers.size() == 1 && selection.identifiers.front().registryId == "ap203e2-n2635")
    {
        selection.qualified = QualifiedSchemaProfile{ "ap203e2-n2635", Profile, SchemaSha256 };
    }
    else if (selection.identifiers.size() == 1 && selection.identifiers.front().registryId == "ap214-edition-3")
    {
        // This artifact-backed profile implements source declarations only. Planar validation must
        // still refuse it until AP214-specific local/global rules have their own implementation.
        selection.qualified = QualifiedSchemaProfile{ "ap214-edition-3", Ap214SourceProfile, Ap214SchemaSha256, ProfileKind::Ap214e3Source };
    }
    else if (selection.identifiers.size() == 1 && selection.identifiers.front().registryId == "ap214-mbx-2010")
    {
        // Only the same audited declaration subset is selected. This publisher-backed
        // binding does not certify an ISO edition or enable the AP203 planar rules.
        selection.qualified = QualifiedSchemaProfile{ "ap214-mbx-2010", Ap214MbxSourceProfile, Ap214SchemaSha256, ProfileKind::Ap214MbxSource };
    }
    else if (
        scope == GeometryScope::GeneralBrep && selection.identifiers.size() == 1 && selection.identifiers.front().name == "CONFIG_CONTROL_DESIGN" &&
        selection.identifiers.front().match == SchemaMatch::NameOnly
    )
    {
        // Select a fixed source-check implementation only in explicit general
        // scope. The bare HEADER still identifies a family: leave NameOnly,
        // empty OID/registry and raw span intact; no edition/APD-year inference.
        // Publication still requires all declared source and native checks.
        selection.qualified = QualifiedSchemaProfile{ "ap203-nameonly-n916-source",
                                                      Ap203N916SourceProfile,
                                                      Ap203N916SchemaSha256,
                                                      ProfileKind::Ap203N916Source };
    }
    return selection;
}
const char* SchemaInterpretationName(SchemaInterpretation interpretation)
{
    switch (interpretation)
    {
        case SchemaInterpretation::DeclaredOnly:
            return "DeclaredOnly";
        case SchemaInterpretation::UnregisteredAp214As2010:
            return "UnregisteredAp214As2010";
        case SchemaInterpretation::Ap214NameOnlyAs2010:
            return "Ap214NameOnlyAs2010";
        case SchemaInterpretation::SourceBoundAp214As2010:
            return "SourceBoundAp214As2010";
    }
    return "Unknown";
}
std::optional<QualifiedSchemaProfile> SelectExplicitInterpretation(const SchemaSelection& selection, const SchemaInterpretationRequest& interpretation)
{
    if (selection.qualified || selection.identifiers.size() != 1)
    {
        return {};
    }
    const auto& identity = selection.identifiers.front();
    const bool eightArc = interpretation == SchemaInterpretation::UnregisteredAp214As2010 && identity.match == SchemaMatch::UnregisteredVersion &&
                          identity.oid == Words("1 0 10303 214 3 1 1 1");
    const bool nameOnly = interpretation == SchemaInterpretation::Ap214NameOnlyAs2010 && identity.match == SchemaMatch::NameOnly &&
                          identity.oid.empty();
    const bool sourceBound = interpretation == SchemaInterpretation::SourceBoundAp214As2010;
    if (identity.name != "AUTOMOTIVE_DESIGN" || (!eightArc && !nameOnly && !sourceBound))
    {
        return {};
    }
    // The pinned AP214E3_2010.exp (71ab140f...) identifies seven arcs; no
    // authority establishes either an eight-arc literal or a bare family name
    // as this particular edition. These two existing policies retain their
    // disjoint, exact eligibility; they do not acquire the source-bound policy's
    // separate application permission. The caller selects the audited implementation, including
    // its separately proved compatibility transport. Keep HEADER identity,
    // original unsupported-binding diagnostic and assumption qualification.
    // The source-bound request was checked against the immutable capture before
    // Binding. It chooses this pinned implementation, not an inferred edition,
    // corrected OID or registered publisher identity. Its malformed/unregistered
    // HEADER and original refusal remain separate, owning observations.
    return QualifiedSchemaProfile{ sourceBound ? "ap214-explicit-source-bound-2010" : "ap214-mbx-2010",
                                   Ap214MbxSourceProfile,
                                   Ap214SchemaSha256,
                                   ProfileKind::Ap214MbxSource };
}
} // namespace step_semantics
