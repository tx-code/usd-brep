// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "StepSemantics.h"

#include <bitset>
#include <gsl/pointers>
#include <span>
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
    gsl::not_null<const Field*> field;
    std::string_view origin;
};
struct DerivedSlot
{
    std::string_view recordType, declaredIn, field;
};
struct NamedBranches
{
    std::string_view name;
    std::span<const std::string_view> branches;
};
struct Catalog
{
    std::vector<Declaration> declarations;
    std::vector<std::string> names;
    std::unordered_map<std::string, std::size_t> ids;
    std::vector<std::bitset<128>> ancestors;
    std::vector<std::vector<FieldBinding>> flattened;
    std::vector<DerivedSlot> derivedSlots;
    std::vector<NamedBranches> referenceSelects;
    std::vector<NamedBranches> enumerations;
    explicit Catalog(ProfileKind profile);
    std::optional<std::size_t> Find(std::string_view name) const;
    bool HasField(std::size_t type, std::string_view field) const;
    bool AdmitsComplex(std::span<const std::string> facets) const;
    bool HasSubtypeConflict(const std::bitset<128>& types) const;
    bool MatchesReference(const std::bitset<128>& types, std::string_view expected) const;
    bool ContainsEnumeration(std::string_view type, std::string_view value) const;
    bool AdmitsMeasure(std::string_view type) const;
    int PrefixPower10(std::string_view prefix) const;
    std::string_view RedeclaredBy(const std::bitset<128>& types, const FieldBinding& binding) const;
};
const Catalog& Schema(ProfileKind profile);

struct BoundAttribute
{
    AttributeView declaration;
    std::size_t component;
    std::size_t parameter;
    bool referenceAggregate;
};
struct BoundRecord
{
    std::vector<BoundAttribute> attributes; // Source component/slot order; values remain source-owned.
    std::unordered_multimap<std::string_view, std::size_t> named;
};
struct SemanticData
{
    std::shared_ptr<const step_source::SourceSnapshot> source;
    std::optional<gsl::not_null<const Catalog*>> catalog; // Selected by binding, before type/field interpretation.
    QualifiedSchemaProfile profile;
    SchemaIdentifier schemaIdentity;
    std::vector<std::bitset<128>> types; // Compact membership only; no second value/entity object graph.
    std::vector<std::vector<std::size_t>> populations;
    std::vector<unsigned char> representationUse;
    std::vector<BoundRecord> fields;
    std::unordered_map<const step_source::Value*, std::size_t> referenceTargets;
    bool bindingsComplete = false;
    Part part;
    Units units;
};

const Catalog& Schema(const SemanticData& data);

AttributeView AttributeAt(const SemanticData& data, std::size_t record, std::size_t component, std::size_t parameter);
} // namespace step_semantics
