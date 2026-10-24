// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepAssemblyBindings.h"

namespace step_semantics
{
enum class UnreferencedPlacementPolicy
{
    Reject,
    RetainDisconnected
};
struct RetainedPlacementDeclarations
{
    std::vector<std::size_t> placements, records;
    Diagnostic originalDiagnostic; // Original unsupported-ownership fact, not a waived geometry error.
};
struct AssemblyContextUnits
{
    std::size_t context;
    Units units; // Nominal/context checks only; conversion arithmetic may remain deferred.
};
struct AssemblyInputData;
// Owns the complete bound occurrence graph and every distinct endpoint context.
// Not a qualified source document: geometry/ownership, final interoperability
// approval, numeric frame evaluation and native construction have not run.
class AssemblyInputs
{
public:

    explicit AssemblyInputs(std::shared_ptr<const AssemblyInputData> data);
    const AssemblyBindingPlan& Bindings() const;
    const SourceBindings& SourceFields() const;
    std::span<const AssemblyContextUnits> Contexts() const;
    const AssemblyContextUnits* FindContext(std::size_t context) const; // Null for an unrelated source context.
    std::span<const Part> DefinitionChecks() const; // Empty unless explicit definition inspection completed; not qualified Part snapshots.

private:

    std::shared_ptr<const AssemblyInputData> m_data;
};
struct AssemblyInputOutcome
{
    Outcome checks; // Success is Deferred; snapshot/document always null.
    std::shared_ptr<const AssemblyInputs> inputs; // Null if any prerequisite fails.
};
AssemblyInputOutcome InspectAssemblyInputs(
    const step_source::InspectOutcome& source,
    Limits limits,
    SourcePolicy policy,
    const SchemaInterpretationRequest& interpretation = SchemaInterpretation::DeclaredOnly,
    UnitInterpretation unitInterpretation = UnitInterpretation::DeclaredOnly,
    ComplexMappingInterpretation complexMappingInterpretation = ComplexMappingInterpretation::DeclaredOnly
);
AssemblyInputOutcome InspectAssemblyDefinitions(
    const step_source::InspectOutcome& source,
    Limits limits,
    SourcePolicy policy,
    const SchemaInterpretationRequest& interpretation = SchemaInterpretation::DeclaredOnly,
    UnitInterpretation unitInterpretation = UnitInterpretation::DeclaredOnly,
    ComplexMappingInterpretation complexMappingInterpretation = ComplexMappingInterpretation::DeclaredOnly
);
struct AssemblyDocumentData;
// Complete source ownership under the selected implementation, not native
// geometry qualification. Wrapper definitions retain zero shapes explicitly.
class AssemblyDocument
{
public:

    explicit AssemblyDocument(std::shared_ptr<const AssemblyDocumentData> data);
    const AssemblyInputs& Inputs() const;
    std::size_t DefinitionCount() const;
    std::shared_ptr<const Snapshot> Definition(std::size_t index) const;
    const std::optional<RetainedPlacementDeclarations>& RetainedPlacements() const;

private:

    std::shared_ptr<const AssemblyDocumentData> m_data;
};
struct AssemblySourceOutcome
{
    Outcome checks; // No legacy single-part snapshot/independent document.
    std::shared_ptr<const AssemblyDocument> document;
};
AssemblySourceOutcome CheckAssemblySource(
    const step_source::InspectOutcome& source,
    Limits limits,
    SourcePolicy policy,
    const SchemaInterpretationRequest& interpretation = SchemaInterpretation::DeclaredOnly,
    UnitInterpretation unitInterpretation = UnitInterpretation::DeclaredOnly,
    ComplexMappingInterpretation complexMappingInterpretation = ComplexMappingInterpretation::DeclaredOnly,
    UnreferencedPlacementPolicy placementPolicy = UnreferencedPlacementPolicy::Reject
);
} // namespace step_semantics
