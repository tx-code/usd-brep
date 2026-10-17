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

struct Limits
{
    std::size_t dependencyVisits = 1000000; // Explicit source-test preset; no production precision policy.
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
    std::string registryId, profile, expressSha256;
};
struct SchemaSelection
{
    std::vector<SchemaIdentifier> identifiers; // Complete HEADER order; owning observations survive failed binding.
    std::optional<QualifiedSchemaProfile> qualified; // Absent for known versions without an implemented profile.
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
    const step_source::Value* value = nullptr; // Borrowed from the immutable owning SourceSnapshot.
};

struct Role
{
    std::string name;
    std::size_t recordIndex = 0;
};

struct Part
{
    std::vector<Role> roles; // Source indices, not copies of the entity/value graph.
    std::string shapeRole; // MaterialSolid / OpenSurfaceModel / ClosedSurfaceModel: source claims only.
    std::string apdStatus;
    std::string apdSchemaClaim;
    std::string apdYear; // Exact INTEGER lexeme; never used to infer the HEADER edition.
};

struct SiUnit
{
    std::size_t recordIndex = 0;
    std::string quantity; // Length / PlaneAngle / SolidAngle; nominal type is preserved.
    std::string siName;
    std::string prefix; // Empty means explicitly absent prefix, not a guessed unit.
    std::array<int, 7> dimensions{};
    int canonicalPower10 = 0; // Exact scale to mm/rad/sr. No floating point conversion.
};

struct Units
{
    std::size_t lengthUnit = 0;
    std::size_t planeAngleUnit = 0;
    std::optional<std::size_t> solidAngleUnit;
    std::vector<SiUnit> siDefinitions;
    std::optional<int> lengthPower10Mm; // Absent for deferred conversion-based interpretation.
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
    const Part& PartDefinition() const;
    const Units& UnitContext() const;
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
    Deferred,
    Rejected,
    Unsupported,
    ResourceLimit,
    SourceFailure
};

struct Outcome
{
    Status status = Status::SourceFailure;
    std::shared_ptr<const Snapshot> snapshot;
    std::optional<SchemaSelection> schemaSelection; // Null means the binding phase did not run.
    std::string binding = "NotRun";
    std::string types = "NotRun";
    std::string part = "NotRun";
    std::string siUnits = "NotRun";
    std::size_t dependencyVisits = 0;
    Limits limits;
    std::vector<Diagnostic> diagnostics;
};

Outcome Check(const step_source::InspectOutcome& source, Limits limits);
bool InspectJson(const step_source::InspectOutcome& source, const Outcome& semantics, std::string& output);
const char* StatusName(Status status);
} // namespace step_semantics
