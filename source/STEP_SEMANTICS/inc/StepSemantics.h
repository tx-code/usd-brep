// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "StepSource.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace step_semantics
{
inline constexpr const char* Profile = "203e2-P0-Planar-0.1";
inline constexpr const char* SchemaName = "AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF";
inline constexpr const char* SchemaSha256 = "c68dd02200e2f4213e311553a4f6295effd168209de1fdc4e8003ce4c9ddd088";
inline constexpr const char* Ap214SourceProfile = "214e3-P0-Source-0.1";
inline constexpr const char* Ap214SchemaSha256 = "71ab140fe7f774321beee6a31e6fee2afc3973fd60350ae2018c74c211fb4295";
inline constexpr const char* Ap214MbxSourceProfile = "214-MBx2010-Source-0.1";
inline constexpr const char* Ap203GeneralSourceProfile = "203e2-G0-Source-0.1";

enum class ProfileKind
{
    Ap203e2Planar,
    Ap214e3Source,
    Ap203e2GeneralSource,
    Ap214MbxSource
};

struct Limits
{
    std::size_t dependencyVisits = 1000000; // Explicit source-test preset; no production precision policy.
    std::size_t preservedDiagnostics = 1024; // Explicit interoperability only; never suppress excess issues.
};
enum class SourcePolicy
{
    Strict,
    PreserveNonGeometric
};
enum class GeometryScope
{
    ExplicitOuterBounds,
    GeneralBrep // Source regions without an explicit outer bound require a later geometric proof.
};

enum class Protocol
{
    Unknown,
    Ap203e1,
    Ap203e2,
    Ap214,
    Ap242
};
enum class SchemaMatch
{
    Malformed,
    UnknownName,
    NameOnly,
    LegacyAlias,
    UnregisteredVersion,
    RegisteredVersion
};
struct SchemaIdentifier
{
    std::string literal, name;
    std::vector<std::string> oid; // Original numeric arc strings; no integer conversion or leading-zero normalization.
    step_source::Span span;
    Protocol protocol = Protocol::Unknown;
    SchemaMatch match = SchemaMatch::Malformed;
    std::string registryId; // Identity recognition only, not permission to run a type/rule implementation.
};
struct QualifiedSchemaProfile
{
    // This selects a source-check implementation; downstream rule/geometry capability is separate.
    std::string registryId, profile, expressSha256;
    ProfileKind kind = ProfileKind::Ap203e2Planar;
};
struct SchemaSelection
{
    std::vector<SchemaIdentifier> identifiers; // Complete HEADER order; owning observations survive failed binding.
    std::optional<QualifiedSchemaProfile> qualified; // Absent for known versions without an implemented profile.
};

struct AggregateDimension
{
    std::string_view aggregate;
    std::size_t minimum = 0;
    std::optional<std::size_t> maximum;
    bool uniqueElements = false;
};
struct AttributeView
{
    std::string_view name;
    std::string_view declaredIn;
    std::string_view declaredType;
    std::string_view redeclaredBy;
    bool optional = false;
    bool derivedSlot = false;
    std::string_view aggregate; // LIST/SET, empty for scalars.
    std::size_t minimum = 0;
    std::optional<std::size_t> maximum;
    bool uniqueElements = false;
    std::vector<AggregateDimension> nestedAggregates; // Inner LIST bounds; source Value retains the actual row structure.
    const step_source::Value* value = nullptr; // Borrowed from the immutable owning SourceSnapshot.
};

struct Role
{
    std::string name;
    std::size_t recordIndex = 0;
};

struct Part
{
    struct PropertyAttachment
    {
        // Source indices belonging to this snapshot, not portable IDs or inferred
        // material values. Keep each source link even when names/items are shared.
        std::size_t link, propertyDefinition, characterizedDefinition, productDefinition, representation, context;
    };
    std::vector<Role> roles; // Source indices, not copies of the entity/value graph.
    std::vector<PropertyAttachment> propertyAttachments;
    std::vector<std::size_t> deferredRegionFaces; // Source faces only; no bound has been relabeled or selected as outer.
    std::string shapeRole; // MaterialSolid / OpenSurfaceModel / ClosedSurfaceModel: source claims only.
    std::string apdStatus;
    std::string apdSchemaClaim;
    std::string apdYear; // Exact INTEGER lexeme; never used to infer the HEADER edition.
    bool applicationProtocolPresent = true; // False means unavailable source metadata; never synthesize an APD.
};

struct SiUnit
{
    std::size_t recordIndex = 0;
    std::string quantity; // Length / PlaneAngle / SolidAngle / Mass; nominal type is preserved.
    std::string siName;
    std::string prefix; // Empty means explicitly absent prefix, not a guessed unit.
    std::array<int, 7> dimensions{};
    int canonicalPower10 = 0; // Exact SI metadata scale to mm/rad/sr/kg; original unit/value data stay unchanged.
};

struct DimensionExponent
{
    std::string numerator;
    std::string denominator;
};
struct UnitDimensionDefinition
{
    std::size_t recordIndex = 0;
    std::string nominal; // Source subtype identity, never inferred from an equal vector.
    std::array<DimensionExponent, 7> dimensions; // L/M/T/I/temperature/amount/luminous; derived data, source unchanged.
};
struct Units
{
    std::size_t lengthUnit = 0;
    std::size_t planeAngleUnit = 0;
    std::optional<std::size_t> solidAngleUnit;
    std::vector<SiUnit> siDefinitions;
    std::optional<int> lengthPower10Mm; // Absent for deferred conversion-based interpretation.
    std::vector<UnitDimensionDefinition> dimensionDefinitions; // AP214 source-dimensional analysis only, not a unit scale.
};

struct Diagnostic
{
    step_source::Diagnostic source;
    std::string classification; // SourceInvalid / ProfileUnsupported / ResourceLimit / SourceInspectionFailure.
    std::string field;
    std::string declaredIn;
    std::string declaredType;
    std::string expected;
    std::string actual;
    std::string responsibility;
};

struct SemanticData;
struct AttributeLocation
{
    std::string recordId;
    std::string component;
    step_source::Span span;
    std::vector<std::size_t> parameterPath; // One-based source slot; strings/path own their storage.
};

// An owning, read-only field handle. References returned from it borrow its snapshot lifetime.
class FieldView
{
public:

    const AttributeView& Declaration() const;
    const step_source::Value& Value() const;
    AttributeLocation Location() const;
    // Results are record indices local to this source graph, not portable entity identities.
    std::size_t ReferenceTarget() const;
    // Flat reference aggregates only. Walk nested Value rows with Snapshot::ReferenceTarget;
    // flattening a control grid here would hide its source shape.
    std::vector<std::size_t> ReferenceTargets() const;

private:

    friend class Snapshot;
    FieldView(std::shared_ptr<const SemanticData> data, std::size_t record, std::size_t binding);
    std::shared_ptr<const SemanticData> m_data;
    std::size_t m_record;
    std::size_t m_binding;
};

class Snapshot
{
public:

    explicit Snapshot(std::shared_ptr<const SemanticData> data);
    const step_source::SourceSnapshot& Source() const;
    // Borrowed identity views remain valid while this owning snapshot lives.
    const QualifiedSchemaProfile& SelectedProfile() const;
    const SchemaIdentifier& SchemaIdentity() const;
    const Part& PartDefinition() const;
    const Units& UnitContext() const;
    const std::vector<Diagnostic>& PreservedDiagnostics() const;
    const std::vector<std::size_t>& UnattachedNonGeometricDeclarations() const;
    const std::vector<std::string>& RegisteredTypes() const;
    const std::vector<std::size_t>* Instances(std::string_view type) const; // Null: unregistered, not a proved empty set.
    bool IsA(std::size_t recordIndex, std::string_view type) const; // Unknown type is an error.
    std::vector<std::string> TypesOf(std::size_t recordIndex) const;
    AttributeView Attribute(std::size_t recordIndex, std::size_t componentIndex, std::size_t parameterIndex) const;
    // Named lookup rejects missing/ambiguous fields. Qualification uses the original declaring type.
    FieldView Field(std::size_t recordIndex, std::string_view name) const;
    FieldView Field(std::size_t recordIndex, std::string_view declaredIn, std::string_view name) const;
    // Requires an actual reference Value owned by this source snapshot, not a copied/foreign value.
    std::size_t ReferenceTarget(const step_source::Value& value) const;
    std::vector<std::size_t> UsedIn(std::string_view targetId, std::string_view ownerType, std::string_view field) const;
    bool UsedByRepresentation(std::size_t recordIndex) const; // RI-filtered use closure, not a generic management walk.

private:

    std::shared_ptr<const SemanticData> m_data;
};

enum class Status
{
    Checked, // Only the ticket-03 checks; never complete AP validity or model acceptance.
    CheckedWithDiagnostics, // Explicit interoperability; original nonconformances remain in the owning source.
    Deferred,
    Rejected,
    Unsupported,
    ResourceLimit,
    SourceFailure
};

struct UnitDependencyCounts
{
    std::size_t total = 0;
    std::size_t unitDefinitions = 0;
    std::size_t typePredicates = 0;
};
struct GeometryDependencyCounts
{
    std::size_t total = 0;
    std::size_t typePredicates = 0;
    std::size_t entityPredicates = 0;
};
struct Outcome
{
    Status status = Status::SourceFailure;
    SourcePolicy policy = SourcePolicy::Strict;
    GeometryScope geometryScope = GeometryScope::ExplicitOuterBounds;
    std::shared_ptr<const Snapshot> snapshot;
    std::optional<SchemaSelection> schemaSelection; // Null means the binding phase did not run.
    std::string binding = "NotRun";
    std::string types = "NotRun";
    std::string unitDependencies = "NotRun";
    std::string geometryDependencies = "NotRun"; // Selected AP214 source prerequisites, not complete local/global rules.
    std::string part = "NotRun";
    std::string siUnits = "NotRun";
    std::string interoperability = "NotRun";
    std::size_t dependencyVisits = 0;
    GeometryDependencyCounts geometryDependencyChecks;
    UnitDependencyCounts unitDependencyChecks;
    std::size_t propertyAttachmentChecks = 0; // Actual AP214 links attempted within part/closure guards.
    Limits limits;
    std::vector<Diagnostic> diagnostics;
    std::vector<Diagnostic> preservedDiagnostics; // Approved nongeometric issues only, separate from fatal failures.
};

Outcome Check(const step_source::InspectOutcome& source, Limits limits);
Outcome Check(const step_source::InspectOutcome& source, Limits limits, SourcePolicy policy, GeometryScope scope = GeometryScope::ExplicitOuterBounds);
bool InspectJson(const step_source::InspectOutcome& source, const Outcome& semantics, std::string& output);
const char* StatusName(Status status);
} // namespace step_semantics
