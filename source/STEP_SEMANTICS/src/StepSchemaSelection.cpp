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
                               Version{ "AUTOMOTIVE_DESIGN", "1 0 10303 214 3 1 1", "ap214-edition-3" } };

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

SchemaSelection SelectSchema(const step_source::SourceSnapshot& source)
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
    return selection;
}
} // namespace step_semantics
