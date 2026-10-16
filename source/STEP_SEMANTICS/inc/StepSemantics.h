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
