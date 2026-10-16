// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "StepSemantics.h"

#include <bitset>
#include <unordered_map>

namespace step_semantics
{
enum class FieldKind
{
    String,
    Real,
    Integer,
    Boolean,
    Enumeration,
    Reference,
    Aggregate,
    Measure
};
enum class Unique
{
    None,
    Set,
    List
};
struct Field
{
    std::string name;
    std::string type;
    FieldKind kind;
    bool optional = false;
    FieldKind elementKind = FieldKind::Reference;
    std::size_t minimum = 0;
    std::size_t maximum = static_cast<std::size_t>(-1);
    Unique unique = Unique::None;
};
struct Declaration
{
    std::string name;
    std::vector<std::string> parents;
    std::vector<Field> fields;
    bool simple = false;
};
struct FieldBinding
{
    const Field* field;
    std::string_view origin;
};
struct Catalog
{
    std::vector<Declaration> declarations;
    std::vector<std::string> names;
    std::unordered_map<std::string, std::size_t> ids;
    std::vector<std::bitset<128>> ancestors;
    std::vector<std::vector<FieldBinding>> flattened;
    Catalog();
    std::optional<std::size_t> Find(std::string_view name) const;
    bool HasField(std::size_t type, std::string_view field) const;
};
const Catalog& Schema();

struct SemanticData
{
    std::shared_ptr<const step_source::SourceSnapshot> source;
    std::vector<std::bitset<128>> types; // Compact membership only; no second value/entity object graph.
    std::vector<std::vector<std::size_t>> populations;
    std::vector<unsigned char> representationUse;
    Part part;
    Units units;
};

AttributeView AttributeAt(const SemanticData& data, std::size_t record, std::size_t component, std::size_t parameter);
} // namespace step_semantics
