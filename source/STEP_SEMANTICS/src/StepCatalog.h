// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "StepSemantics.h"

#include <array>
#include <bit>
#include <cstdint>
#include <gsl/pointers>
#include <span>
#include <stdexcept>
#include <unordered_map>

namespace step_semantics
{
constexpr bool IsAp214SourceProfile(ProfileKind profile) noexcept
{
    return profile == ProfileKind::Ap214e3Source || profile == ProfileKind::Ap214MbxSource;
}
constexpr bool HasDeclaredProductStructureProfile(ProfileKind profile) noexcept
{
    return IsAp214SourceProfile(profile) || profile == ProfileKind::Ap203N916Source;
}
constexpr const char* SourceArtifactName(ProfileKind profile)
{
    switch (profile)
    {
        case ProfileKind::Ap203e2Planar:
        case ProfileKind::Ap203e2GeneralSource:
            return "N2635";
        case ProfileKind::Ap214e3Source:
        case ProfileKind::Ap214MbxSource:
            return "AP214E3_2010.exp";
        case ProfileKind::Ap203N916Source:
            return "STEPcode modified N916";
    }
    throw std::logic_error("Unknown source profile authority");
}
inline constexpr std::size_t CatalogTypeCapacity = 256;
class TypeMembership
{
public:

    constexpr std::size_t size() const noexcept
    {
        return CatalogTypeCapacity;
    }
    constexpr void set(std::size_t id)
    {
        if (id >= size())
        {
            throw std::out_of_range("Catalog membership index");
        }
        m_words[id / 64] |= std::uint64_t{ 1 } << (id % 64);
    }
    constexpr bool test(std::size_t id) const
    {
        if (id >= size())
        {
            throw std::out_of_range("Catalog membership index");
        }
        return (m_words[id / 64] & (std::uint64_t{ 1 } << (id % 64))) != 0;
    }
    constexpr TypeMembership& operator|=(const TypeMembership& other) noexcept
    {
        for (std::size_t i = 0; i < m_words.size(); ++i)
        {
            m_words[i] |= other.m_words[i];
        }
        return *this;
    }
    constexpr std::size_t SharedCount(const TypeMembership& other) const noexcept
    {
        std::size_t count = 0;
        for (std::size_t i = 0; i < m_words.size(); ++i)
        {
            count += std::popcount(m_words[i] & other.m_words[i]);
        }
        return count;
    }
    constexpr bool operator==(const TypeMembership&) const = default;

private:

    // Four words serve both generated ancestry and runtime source membership.
    // Bounds precede shifts; the shift count is always in [0, 63].
    std::array<std::uint64_t, CatalogTypeCapacity / 64> m_words{};
};
enum class FieldKind
{
    String,
    Real,
    Integer,
    Boolean,
    Logical,
    Enumeration,
    Reference,
    Aggregate,
    Measure,
    SizeSelect,
    TrimmingSelect,
    Number, // Unnarrowed EXPRESS NUMBER; explicit redeclarations retain their own effective kind.
    MarkerSelect // Fixed marker_type enumeration or pre_defined_marker reference.
};
enum class Unique
{
    None,
    Set,
    List
};
struct Field
{
    std::string_view name;
    std::string_view type;
    FieldKind kind;
    bool optional = false;
    FieldKind elementKind = FieldKind::Reference;
    std::size_t minimum = 0;
    std::size_t maximum = static_cast<std::size_t>(-1);
    Unique unique = Unique::None;
    std::span<const AggregateDimension> nestedAggregates;
};
struct Declaration
{
    std::string_view name;
    std::span<const std::string_view> parents;
    std::span<const Field> fields;
    bool simple = false;
};
struct FieldBinding
{
    std::size_t origin, field; // Profile-local declaring type and local field index.
    constexpr bool operator==(const FieldBinding&) const = default;
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
struct SiPrefix
{
    std::string_view name;
    int power;
};
struct CatalogRange
{
    std::size_t offset = 0, count = 0;
};
struct NameId
{
    std::string_view name;
    std::size_t id;
};
struct CompiledDerivedSlot
{
    std::size_t owner, origin, field;
};
struct ExplicitRedeclaration
{
    std::string_view recordType, declaredIn, field, type;
    FieldKind kind;
};
struct CompiledExplicitRedeclaration
{
    std::size_t owner, origin, field;
    std::string_view type;
    FieldKind kind;
};
struct ResolvedField
{
    const Field& field;
    std::string_view redeclaredBy;
    bool derivedSlot = false;
};
template <class T>
constexpr const T& CatalogAt(std::span<const T> values, std::size_t index)
{
    if (index >= values.size())
    {
        throw std::out_of_range("Catalog query index");
    }
    return values[index];
}
constexpr std::optional<std::size_t> LookupCatalogName(std::span<const NameId> names, std::string_view name)
{
    std::size_t first = 0, count = names.size();
    while (count)
    {
        const auto half = count / 2, middle = first + half;
        if (names[middle].name < name)
        {
            first = middle + 1;
            count -= half + 1;
        }
        else
        {
            count = half;
        }
    }
    return first < names.size() && names[first].name == name ? std::optional<std::size_t>(names[first].id) : std::nullopt;
}
struct Catalog
{
    std::span<const Declaration> declarations;
    std::span<const std::string_view> names;
    std::span<const NameId> nameIndex;
    std::span<const TypeMembership> ancestors;
    std::span<const FieldBinding> bindings;
    std::span<const CatalogRange> flattenedRanges;
    std::span<const DerivedSlot> derivedSlots;
    std::span<const CompiledDerivedSlot> compiledDerivedSlots;
    std::span<const NamedBranches> referenceSelects;
    std::span<const TypeMembership> selectMasks;
    std::span<const NamedBranches> enumerations;
    std::span<const std::span<const std::string_view>> complexSignatures;
    std::span<const std::string_view> measureBranches;
    std::span<const std::span<const std::string_view>> exclusiveGroups;
    std::span<const TypeMembership> exclusiveMasks;
    std::span<const SiPrefix> prefixes;
    std::span<const CompiledExplicitRedeclaration> explicitRedeclarations;
    std::span<const Field> explicitFields;
    std::optional<std::size_t> Find(std::string_view name) const;
    std::size_t RequireType(std::string_view name) const;
    std::span<const FieldBinding> Flattened(std::size_t type) const;
    const Field& FieldOf(FieldBinding binding) const;
    std::string_view OriginOf(FieldBinding binding) const;
    bool HasField(std::size_t type, std::string_view field) const;
    bool AdmitsComplex(std::span<const std::string> facets) const;
    bool HasSubtypeConflict(const TypeMembership& types) const;
    bool MatchesReference(const TypeMembership& types, std::string_view expected) const;
    bool ContainsEnumeration(std::string_view type, std::string_view value) const;
    bool AdmitsMeasure(std::string_view type) const;
    int PrefixPower10(std::string_view prefix) const;
    std::string_view RedeclaredBy(const TypeMembership& types, const FieldBinding& binding) const;
    const Field& EffectiveField(const TypeMembership& types, FieldBinding binding) const;
    ResolvedField ResolveField(const TypeMembership& types, FieldBinding binding) const;
    // Read-only access to the same fixed facts used by queries, for complete
    // refactoring snapshots; no second copy of the schema tables is maintained.
    std::span<const SiPrefix> PrefixEntries() const noexcept;
    std::span<const std::span<const std::string_view>> ExclusiveSubtypeGroups() const noexcept;
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
    enum class PreservationProof
    {
        ExistingMetadata,
        MissingApplicationProtocol,
        CurveStyleWidth,
        OpaqueDisplayText,
        OpaquePartIdentity,
        OpaqueShapeCaption,
        OpaqueAdministrativeLabel,
        DistinctApplicationContexts,
        ContextApplicationProtocols,
        ForeignRealItem,
        N916RepresentationCaption,
        AsciiInvariantRepresentationPurpose,
        VerifiedExplicitUnitDimensions
    };
    struct PendingIssue
    {
        Status status;
        Diagnostic diagnostic;
        PreservationProof proof = PreservationProof::ExistingMetadata;
    };
    SourcePolicy policy = SourcePolicy::Strict;
    GeometryScope geometryScope = GeometryScope::ExplicitOuterBounds;
    std::size_t preservedDiagnosticLimit = 1024;
    bool definitionInspectionRequested = false;
    std::size_t definitionDiagnosticCount = 0;
    std::vector<PendingIssue> pendingIssues; // Provisional only; no metadata/root qualification until the final proof.
    std::vector<Diagnostic> preservedDiagnostics;
    UnitInterpretation requestedUnitInterpretation = UnitInterpretation::DeclaredOnly;
    std::vector<UnitDimensionAssumption> unitAssumptions;
    std::vector<Diagnostic> unitInterpretationDiagnostics;
    ComplexMappingInterpretation requestedComplexMappingInterpretation = ComplexMappingInterpretation::DeclaredOnly;
    std::vector<ComplexMappingAssumption> complexMappingAssumptions;
    std::vector<Diagnostic> complexMappingDiagnostics;
    std::vector<CylindricalBandRepairDeferral> cylindricalBandRepairs;
    std::vector<unsigned char> cylindricalBandRepairUses;
    std::vector<Diagnostic> cylindricalBandRepairDiagnostics;
    SchemaInterpretation appliedInterpretation = SchemaInterpretation::DeclaredOnly;
    std::vector<Diagnostic> interpretationDiagnostics;
    std::vector<std::size_t> unattachedNonGeometric; // Proven nongeometric residue, never a selected-part ownership root.
    std::shared_ptr<const step_source::SourceSnapshot> source;
    std::optional<gsl::not_null<const Catalog*>> catalog; // Selected by binding, before type/field interpretation.
    QualifiedSchemaProfile profile;
    SchemaIdentifier schemaIdentity;
    std::vector<TypeMembership> types; // Compact membership only; no second value/entity object graph.
    std::vector<std::vector<std::size_t>> populations;
    std::vector<unsigned char> representationUse;
    std::vector<BoundRecord> fields;
    std::unordered_map<const step_source::Value*, std::size_t> referenceTargets;
    bool bindingsComplete = false;
    Part part;
    Units units;
    struct QualifiedPart
    {
        Part part;
        Units units;
    };
    std::vector<QualifiedPart> independentParts; // Candidate source parts; exposed only after the whole request succeeds.
    std::size_t DiagnosticFactCount() const
    {
        return pendingIssues.size() + interpretationDiagnostics.size() + complexMappingDiagnostics.size() + cylindricalBandRepairDiagnostics.size() +
               definitionDiagnosticCount;
    }
};

const Catalog& Schema(const SemanticData& data);

AttributeView AttributeAt(const SemanticData& data, std::size_t record, std::size_t component, std::size_t parameter);
std::size_t FindFieldBinding(const SemanticData& data, std::size_t record, std::string_view name, std::optional<std::string_view> declaredIn = {});
} // namespace step_semantics
