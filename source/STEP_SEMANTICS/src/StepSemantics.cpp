// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAp203N916Profile.h"
#include "StepAssemblyInputsInternal.h"
#include "StepCatalog.h"
#include "StepDefinitionInspection.h"
#include "StepFaceRegion.h"
#include "StepGeometryDependencies.h"
#include "StepIndependentProducts.h"
#include "StepProductStructure.h"
#include "StepSchemaInterpretationInput.h"
#include "StepSchemaSelection.h"
#include "StepSourceIsolation.h"
#include "StepSourceNumber.h"
#include "StepUnitDependencies.h"
#include "StepUnreferencedPlacements.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace step_semantics
{
namespace
{
using step_source::Span;
using step_source::Value;
using step_source::ValueKind;
struct Failure
{
    Status status;
    Diagnostic diagnostic;
};
bool OpaqueAdministrativeList(std::string_view owner, std::string_view field)
{
    return owner == "PERSON" && (field == "middle_names" || field == "prefix_titles" || field == "suffix_titles");
}
bool OpaqueAdministrativeField(std::string_view owner, std::string_view field)
{
    // The pinned N2635/AP214 declarations (StepAp203GeneralCatalogData.h)
    // make these scalar labels, not roles, selectors or physical quantities.
    // This is only a candidate whitelist: final approval additionally proves
    // selected administrative attachment and exclusion from physical reach.
    return (owner == "APPROVAL" && field == "level") || (owner == "PERSON" && (field == "id" || field == "last_name" || field == "first_name")) ||
           (owner == "ORGANIZATION" && (field == "id" || field == "name")) || OpaqueAdministrativeList(owner, field);
}
bool IsA(const SemanticData& data, std::size_t record, std::string_view name)
{
    const auto type = Schema(data).Find(name);
    return type && data.types.at(record).test(*type);
}
std::size_t IndexOf(const SemanticData& data, std::string_view id)
{
    const auto* record = data.source->Find(id);
    if (!record)
    {
        throw std::invalid_argument("Unknown source instance ID");
    }
    return static_cast<std::size_t>(record - data.source->Records().data());
}

bool PositiveInteger(std::string_view text)
{
    if (!text.empty() && text.front() == '-')
    {
        return false;
    }
    return text.find_first_of("123456789") != std::string_view::npos;
}
bool Three(std::string_view text)
{
    if (!text.empty() && text.front() == '+')
    {
        text.remove_prefix(1);
    }
    const auto first = text.find_first_not_of('0');
    return first != std::string_view::npos && text.substr(first) == "3";
}

class Checker
{
public:

    Checker(std::shared_ptr<SemanticData> data, Outcome& outcome, bool independentRequest = false)
        : m_data(std::move(data)), m_outcome(outcome), m_independentRequest(independentRequest)
    {
    }
    void RunPrerequisites()
    {
        RunPhase(
            Phase::Binding,
            [&]
            {
                Binding();
            }
        );
        RunPhase(
            Phase::Types,
            [&]
            {
                Mapping();
                CheckForeignDeclarations();
                Fields();
            }
        );
        if (m_data->profile.kind != ProfileKind::Ap203e2Planar)
        {
            RunPhase(
                Phase::UnitDependencies,
                [&]
                {
                    if (auto issue = CheckAp214UnitDependencies(
                            *m_data,
                            [this](Span span)
                            {
                                Visit(span);
                            },
                            m_outcome.unitDependencyChecks
                        ))
                    {
                        State(Phase::UnitDependencies) = FailureProgress(issue->status);
                        throw Failure{ issue->status, std::move(issue->diagnostic) };
                    }
                }
            );
            RunPhase(
                Phase::GeometryDependencies,
                [&]
                {
                    if (auto issue = CheckAp214GeometryDependencies(
                            *m_data,
                            [this](Span span)
                            {
                                Visit(span);
                            },
                            m_outcome.geometryDependencyChecks
                        );
                        !issue)
                    {
                        State(Phase::GeometryDependencies) = FailureProgress(issue.error().status);
                        throw Failure{ issue.error().status, std::move(issue.error().diagnostic) };
                    }
                }
            );
        }
    }
    void Run()
    {
        RunPrerequisites();
        RunPhase(
            Phase::Part,
            [&]
            {
                if (m_data->selectedSourceRequest && !IsAp214SourceProfile(m_data->profile.kind))
                {
                    Fail(
                        "SourceIsolationProfileUnsupported",
                        "D2",
                        "Source clause isolation requires the pinned AP214 source profile",
                        m_data->source->Header()[2].span,
                        Status::Unsupported
                    );
                }
                auto structure = InspectProductStructure(
                    m_data,
                    [this](Span span)
                    {
                        Visit(span);
                    },
                    m_data->selectedSourceRequest.has_value()
                );
                if (structure.failure)
                {
                    throw Failure{ structure.failure->status, std::move(structure.failure->diagnostic) };
                }
                m_outcome.productStructure = std::move(structure.structure);
                if (m_independentRequest && m_outcome.productStructure &&
                    (m_outcome.productStructure->Definitions().size() > 1 || m_data->selectedSourceRequest))
                {
                    IndependentProducts();
                }
                else
                {
                    Product();
                }
                Closure();
                if (!m_data->independentParts.empty())
                {
                    std::unordered_map<std::size_t, std::size_t> owners;
                    for (std::size_t i = 0; i < m_data->independentParts.size(); ++i)
                    {
                        for (const auto& role : m_data->independentParts[i].part.roles)
                        {
                            if (role.name == "definition")
                            {
                                owners.emplace(role.recordIndex, i);
                            }
                        }
                    }
                    for (const auto& attachment : m_data->part.propertyAttachments)
                    {
                        m_data->independentParts.at(owners.at(attachment.productDefinition)).part.propertyAttachments.push_back(attachment);
                    }
                }
                if (std::any_of(
                        m_data->cylindricalBandRepairUses.begin(),
                        m_data->cylindricalBandRepairUses.end(),
                        [](auto count)
                        {
                            return count != 1;
                        }
                    ))
                {
                    Fail(
                        "CylindricalBandRepairNotRequired",
                        "D2",
                        "Each explicit source repair must match one rejected face use",
                        m_data->source->Header()[2].span,
                        Status::Unsupported
                    );
                }
            }
        );
        RunPhase(
            Phase::Units,
            [&]
            {
                if (m_data->independentParts.empty())
                {
                    UnitContext();
                }
                else
                {
                    IndependentUnits();
                }
            }
        );
        if ((!m_data->independentParts.empty() && std::any_of(
                                                      m_data->independentParts.begin(),
                                                      m_data->independentParts.end(),
                                                      [](const auto& part)
                                                      {
                                                          return !part.units.lengthPower10Mm;
                                                      }
                                                  )) ||
            (m_data->independentParts.empty() && !m_data->units.lengthPower10Mm))
        {
            State(Phase::Units) = Progress::Deferred;
        }
        if (Interoperability())
        {
            RunPhase(
                Phase::Interoperability,
                [&]
                {
                    ApprovePreservedMetadata();
                }
            );
        }
        if (m_data->selectedSourceRequest)
        {
            Expects(bool(m_outcome.productStructure));
            if (auto failure = QualifySelectedSource(
                    *m_data,
                    *m_outcome.productStructure,
                    m_outgoing,
                    [this](Span span)
                    {
                        Visit(span);
                    }
                ))
            {
                State(Phase::Part) = FailureProgress(failure->status);
                throw Failure{ failure->status, std::move(failure->diagnostic) };
            }
        }
    }
    std::shared_ptr<const AssemblyInputs> InspectAssemblyContextInputs(bool checkDefinitions = false)
    {
        RunPrerequisites();
        auto candidate = std::make_shared<AssemblyInputData>();
        RunPhase(
            Phase::Part,
            [&]
            {
                auto graph = InspectProductStructure(
                    m_data,
                    [this](Span span)
                    {
                        Visit(span);
                    },
                    true
                );
                if (graph.failure)
                {
                    throw Failure{ graph.failure->status, std::move(graph.failure->diagnostic) };
                }
                m_outcome.productStructure = std::move(graph.structure);
                if (!m_outcome.productStructure)
                {
                    Fail(
                        "AssemblyProfileUnsupported",
                        "D2",
                        "The selected source profile does not supply declared assembly structure",
                        m_data->source->Header()[2].span,
                        Status::Unsupported
                    );
                }
                auto budget = m_outcome.limits;
                budget.dependencyVisits -= m_outcome.dependencyVisits;
                auto binding = BindAssemblyOccurrences(m_outcome.productStructure, budget);
                m_outcome.dependencyVisits += binding.dependencyVisits;
                if (!binding.plan)
                {
                    Expects(!binding.diagnostics.empty());
                    throw Failure{ binding.status, std::move(binding.diagnostics.front()) };
                }
                candidate->bindings = std::move(binding.plan);
            }
        );
        RunPhase(
            Phase::Units,
            [&]
            {
                BuildReferenceIndex();
                const auto dimensions = m_data->units.dimensionDefinitions;
                std::unordered_map<std::size_t, const UnitDimensionDefinition*> dimensionByRecord;
                for (const auto& dimension : dimensions)
                {
                    dimensionByRecord.emplace(dimension.recordIndex, &dimension);
                }
                std::unordered_set<std::size_t> visitedContexts;
                for (const auto& occurrence : candidate->bindings->occurrences)
                {
                    Visit(Record(occurrence.usage).span);
                    for (const auto context : { occurrence.childContext, occurrence.parentContext })
                    {
                        if (!visitedContexts.insert(context).second)
                        {
                            continue;
                        }
                        Visit(Record(context).span);
                        if (!IsA(*m_data, context, "GEOMETRIC_REPRESENTATION_CONTEXT"))
                        {
                            Fail(
                                "AssemblyGeometricContextUnestablished",
                                "D2",
                                "Assembly frames require a geometric representation context",
                                Record(context).span,
                                Status::Unsupported,
                                context
                            );
                        }
                        // The same dependency walk and nominal-unit rules used for
                        // independent definitions apply to a placement-only parent.
                        // No fake material part or inherited child units are created.
                        candidate->contextIndices.emplace(context, candidate->contexts.size());
                        candidate->contexts.push_back({ context, UnitsForContext(context, dimensionByRecord) });
                    }
                }
            }
        );
        if (checkDefinitions)
        {
            // Continue the part phase after context observations. Preserve()
            // requires an active phase even when a guarded exception is retained.
            Expects(!m_phase && State(Phase::Part) == Progress::Passed);
            m_phase = Phase::Part;
            State(Phase::Part) = Progress::Running;
            try
            {
                m_assemblyDefinitionChecks = true;
                auto remaining = m_outcome.limits;
                remaining.dependencyVisits -= m_outcome.dependencyVisits;
                auto definitions = PlanAssemblyDefinitions(std::make_shared<const AssemblyInputs>(candidate), remaining);
                m_outcome.dependencyVisits += definitions.dependencyVisits;
                if (!definitions.plan)
                {
                    Expects(!definitions.diagnostics.empty());
                    throw Failure{ definitions.status, std::move(definitions.diagnostics.front()) };
                }
                auto scopes = PlanAssemblyProductChecks(
                    *m_data,
                    *definitions.plan,
                    [this](Span span)
                    {
                        Visit(span);
                    }
                );
                if (scopes.failure)
                {
                    throw Failure{ scopes.failure->status, std::move(scopes.failure->diagnostic) };
                }
                for (const auto& definition : m_outcome.productStructure->Definitions())
                {
                    m_requestedProducts.insert(definition.product);
                }
                for (const auto& scope : scopes.parts)
                {
                    PopulationScope selected(m_scopedPopulations, scope.populations);
                    m_data->part = {};
                    m_wireframeRepresentation.reset();
                    m_constructionRepresentation.reset();
                    try
                    {
                        Product(scope.placementOnly);
                    }
                    catch (Failure& failure)
                    {
                        failure.diagnostic.source.related.push_back(Record(scope.definition).idSpan);
                        throw;
                    }
                    candidate->definitionChecks.push_back(std::move(m_data->part));
                }
                m_data->part = {};
                State(Phase::Part) = Progress::Passed;
                m_phase.reset();
            }
            catch (Failure& failure)
            {
                State(Phase::Part) = FailureProgress(failure.status);
                m_phase.reset();
                throw;
            }
        }
        // Whole-document closure and final metadata approval have not run.
        // Keep pending source facts visible instead of promoting exceptions.
        for (const auto& issue : m_data->pendingIssues)
        {
            m_outcome.unapprovedDiagnostics.push_back(issue.diagnostic);
        }
        m_outcome.unitInterpretationDiagnostics = m_data->unitInterpretationDiagnostics;
        return std::make_shared<const AssemblyInputs>(std::move(candidate));
    }
    void FinalizeAssemblySource(const AssemblyInputs& inputs, UnreferencedPlacementPolicy placementPolicy)
    {
        Expects(m_assemblyDefinitionChecks && inputs.DefinitionChecks().size() == m_outcome.productStructure->Definitions().size());
        m_placementPolicy = placementPolicy;
        Expects(!m_phase && State(Phase::Part) == Progress::Passed);
        m_phase = Phase::Part;
        State(Phase::Part) = Progress::Running;
        try
        {
            std::unordered_map<std::size_t, std::size_t> owners;
            for (const auto& part : inputs.DefinitionChecks())
            {
                std::optional<std::size_t> context, definition;
                for (const auto& role : part.roles)
                {
                    if (role.name == "unit_context")
                    {
                        context = role.recordIndex;
                    }
                    if (role.name == "definition")
                    {
                        definition = role.recordIndex;
                    }
                }
                Expects(context && definition && inputs.FindContext(*context));
                owners.emplace(*definition, m_data->independentParts.size());
                m_data->independentParts.push_back({ part, inputs.FindContext(*context)->units });
            }
            AggregateDefinitionParts();
            m_data->part.shapeRole = "AssemblyDefinitions";
            // Occurrence ownership was explicitly bound; retain every link as
            // a source root. This is not a generic "all records are owned" seed.
            // CDSR reaches its usage PDS and transformed relation/IDT; usages
            // reach the actual parent/child definitions without cloning them.
            for (const auto& usage : m_outcome.productStructure->Usages())
            {
                Visit(Record(usage.usage).span);
                m_data->part.roles.push_back({ "assembly_usage", usage.usage });
            }
            for (const auto& link : m_outcome.productStructure->ContextDependentShapes())
            {
                Visit(Record(link.link).span);
                m_data->part.roles.push_back({ "assembly_shape_link", link.link });
            }
            // Preserve global unit evidence for final metadata approval while
            // each definition view continues to own its context-specific units.
            Units summary;
            std::unordered_set<std::size_t> si, dimensions;
            for (const auto& context : inputs.Contexts())
            {
                for (const auto& unit : context.units.siDefinitions)
                {
                    if (si.insert(unit.recordIndex).second)
                    {
                        summary.siDefinitions.push_back(unit);
                    }
                }
                for (const auto& dimension : context.units.dimensionDefinitions)
                {
                    if (dimensions.insert(dimension.recordIndex).second)
                    {
                        summary.dimensionDefinitions.push_back(dimension);
                    }
                }
            }
            m_data->units = std::move(summary);
            Closure();
            for (const auto& attachment : m_data->part.propertyAttachments)
            {
                m_data->independentParts.at(owners.at(attachment.productDefinition)).part.propertyAttachments.push_back(attachment);
            }
            State(Phase::Part) = Progress::Passed;
            m_phase.reset();
        }
        catch (Failure& failure)
        {
            State(Phase::Part) = FailureProgress(failure.status);
            m_phase.reset();
            throw;
        }
        if (Interoperability())
        {
            RunPhase(
                Phase::Interoperability,
                [&]
                {
                    ApprovePreservedMetadata();
                }
            );
        }
        m_outcome.unapprovedDiagnostics.clear();
    }
    const std::optional<RetainedPlacementDeclarations>& RetainedPlacements() const
    {
        return m_retainedPlacements;
    }
    void InspectDefinitions()
    {
        RunPrerequisites();
        RunPhase(
            Phase::Part,
            [&]
            {
                auto structure = InspectProductStructure(
                    m_data,
                    [this](Span span)
                    {
                        Visit(span);
                    },
                    true
                );
                if (structure.failure)
                {
                    throw Failure{ structure.failure->status, std::move(structure.failure->diagnostic) };
                }
                m_outcome.productStructure = std::move(structure.structure);
                if (!m_outcome.productStructure || m_outcome.productStructure->Definitions().empty())
                {
                    Fail(
                        "DefinitionInspectionRequiresInventory",
                        "D2",
                        "Declared independent definitions are required",
                        m_data->source->Header()[2].span,
                        Status::Unsupported
                    );
                }
                auto plan = PlanIndependentProducts(
                    *m_data,
                    *m_outcome.productStructure,
                    [this](Span span)
                    {
                        Visit(span);
                    }
                );
                if (plan.failure)
                {
                    throw Failure{ plan.failure->status, std::move(plan.failure->diagnostic) };
                }
                BuildReferenceIndex();
                auto evidence = std::make_shared<DefinitionInspectionData>();
                m_definitionInspection = evidence;
                evidence->structure = m_outcome.productStructure;
                for (const auto& scope : plan.parts)
                {
                    DefinitionPrerequisiteCheck check;
                    check.definition = scope.definition;
                    evidence->checks.push_back(std::move(check));
                }
                m_outcome.definitionInspection = std::make_shared<const DefinitionInspection>(evidence);
                const auto dimensions = m_data->units.dimensionDefinitions;
                std::unordered_map<std::size_t, const UnitDimensionDefinition*> dimensionByRecord;
                for (const auto& dimension : dimensions)
                {
                    dimensionByRecord.emplace(dimension.recordIndex, &dimension);
                }
                for (const auto& definition : m_outcome.productStructure->Definitions())
                {
                    m_requestedProducts.insert(definition.product);
                }
                std::optional<Failure> firstFailure;
                bool resourceStopped = false;
                const auto observe = [&](Failure failure, DefinitionPrerequisiteCheck& check)
                {
                    if (m_data->DiagnosticFactCount() >= m_outcome.limits.preservedDiagnostics)
                    {
                        Fail(
                            "DefinitionDiagnosticLimit",
                            "D2",
                            "Independent definition diagnostic capacity exceeded",
                            Record(check.definition).span,
                            Status::ResourceLimit,
                            check.definition
                        );
                    }
                    ++m_data->definitionDiagnosticCount;
                    failure.diagnostic.source.related.push_back(Record(check.definition).idSpan);
                    check.diagnostics.push_back(failure.diagnostic);
                    if (!firstFailure || failure.status == Status::ResourceLimit)
                    {
                        firstFailure = failure;
                    }
                    resourceStopped |= failure.status == Status::ResourceLimit;
                    // Continue observations only: no failed part is inserted into
                    // independentParts, no ownership proof is skipped for admission.
                    State(Phase::Part) = Progress::Running;
                };
                for (std::size_t i = 0; i < plan.parts.size(); ++i)
                {
                    auto& check = evidence->checks[i];
                    const auto& scope = plan.parts[i];
                    PopulationScope selected(m_scopedPopulations, scope.populations);
                    m_data->part = {};
                    m_wireframeRepresentation.reset();
                    m_constructionRepresentation.reset();
                    try
                    {
                        Visit(Record(scope.definition).span);
                        Product();
                        check.part = m_data->part.deferredRegionFaces.empty() ? "CheckedPrerequisites" : "DeferredRegion";
                    }
                    catch (const Failure& failure)
                    {
                        check.part = StatusName(failure.status);
                        observe(failure, check);
                    }
                    if (resourceStopped)
                    {
                        break;
                    }
                    // Unit evidence uses only a context already bound by Product.
                    // A later region failure need not suppress this independent
                    // observation; absent/ambiguous context identity remains NotRun.
                    if (std::any_of(
                            m_data->part.roles.begin(),
                            m_data->part.roles.end(),
                            [](const auto& role)
                            {
                                return role.name == "unit_context";
                            }
                        ))
                    {
                        try
                        {
                            const auto units = IndependentUnitContext(m_data->part, dimensionByRecord);
                            check.units = units.lengthPower10Mm ? "Checked" : "Deferred";
                        }
                        catch (const Failure& failure)
                        {
                            check.units = StatusName(failure.status);
                            observe(failure, check);
                        }
                    }
                    if (resourceStopped)
                    {
                        break;
                    }
                }
                evidence->allDefinitionsVisited = !resourceStopped;
                if (firstFailure)
                {
                    State(Phase::Part) = FailureProgress(firstFailure->status);
                    throw *firstFailure;
                }
            }
        );
    }
    void PublishProgress()
    {
        m_outcome.binding = ProgressName(State(Phase::Binding));
        m_outcome.types = ProgressName(State(Phase::Types));
        m_outcome.unitDependencies = ProgressName(State(Phase::UnitDependencies));
        m_outcome.geometryDependencies = ProgressName(State(Phase::GeometryDependencies));
        m_outcome.part = ProgressName(State(Phase::Part));
        m_outcome.siUnits = ProgressName(State(Phase::Units));
        m_outcome.interoperability = ProgressName(State(Phase::Interoperability));
        m_outcome.interpretationDiagnostics = m_data->interpretationDiagnostics;
        m_outcome.complexMappingDiagnostics = m_data->complexMappingDiagnostics;
        m_outcome.cylindricalBandRepairDiagnostics = m_data->cylindricalBandRepairDiagnostics;
        m_outcome.isolatedSourceFailures = m_data->isolatedSourceFailures;
        m_outcome.sourceInfluenceVisits = m_data->sourceInfluenceVisits;
        if (!m_data->complexMappingDiagnostics.empty() && State(Phase::Types) == Progress::Passed)
        {
            m_outcome.types = "CheckedWithDiagnostics";
        }
        if (State(Phase::Binding) == Progress::Passed && m_data->appliedInterpretation != SchemaInterpretation::DeclaredOnly)
        {
            m_outcome.binding = "InterpretedUnverified";
        }
        for (const auto& diagnostic : m_data->preservedDiagnostics)
        {
            const auto& key = diagnostic.source.key;
            if (key == "RequiredAttributeMissing" || key == "AttributeTypeMismatch" || key == "ReferenceTypeMismatch" ||
                key == "DerivedSlotMarkerRequired" || key == "UnsupportedEntityType" || key == "OpaqueDataTextUnestablished")
            {
                m_outcome.types = "CheckedWithDiagnostics";
            }
            else if (
                key == "UnsupportedProductCategory" || key == "UnsupportedDefinitionContextRole" || key == "UnownedOrOrphanProfileContent" ||
                key == "UnsupportedSinglePartGraph"
            )
            {
                m_outcome.part = "CheckedWithDiagnostics";
            }
            else
            {
                m_outcome.unitDependencies = "CheckedWithDiagnostics";
            }
        }
        if (!m_data->unitInterpretationDiagnostics.empty())
        {
            m_outcome.types = "CheckedWithDiagnostics";
            m_outcome.unitDependencies = "CheckedWithDiagnostics";
        }
        // A source-region deferral qualifies a completed ownership check. It
        // must never hide a later orphan failure or a traversal resource limit.
        if (State(Phase::Part) == Progress::Passed && !m_data->part.deferredRegionFaces.empty() && m_data->cylindricalBandRepairDiagnostics.empty())
        {
            m_outcome.part = "Deferred";
        }
        // Preserved nonphysical metadata cannot upgrade a failed defining
        // source clause to CheckedWithDiagnostics. The repair obligation stays
        // visible after those independent qualifications have been recorded.
        if (State(Phase::Part) == Progress::Passed && !m_data->cylindricalBandRepairDiagnostics.empty())
        {
            m_outcome.part = "DeferredExplicitRepair";
        }
        if (m_outcome.definitionInspectionRequested)
        {
            m_outcome.unapprovedDiagnostics.clear();
            for (const auto& issue : m_data->pendingIssues)
            {
                m_outcome.unapprovedDiagnostics.push_back(issue.diagnostic);
            }
        }
        if (m_outcome.definitionInspection)
        {
            m_definitionInspection->unapprovedDiagnostics = m_outcome.unapprovedDiagnostics;
            m_outcome.part = "InspectedPerDefinition";
            m_outcome.siUnits = "InspectedPerDefinition";
        }
        if (m_data->sourceSelection)
        {
            m_outcome.part = "CheckedSelectedDefinitions";
        }
    }

private:

    enum class Phase
    {
        Binding,
        Types,
        UnitDependencies,
        GeometryDependencies,
        Part,
        Units,
        Interoperability
    };
    enum class Progress
    {
        NotRun,
        Running,
        Passed,
        Rejected,
        Unsupported,
        ResourceLimit,
        Deferred
    };
    std::shared_ptr<SemanticData> m_data;
    Outcome& m_outcome;
    std::optional<Phase> m_phase;
    std::array<Progress, 7> m_progress{};

    bool BorrowedForeignLayer(std::size_t owner) const
    {
        return m_data->profile.kind == ProfileKind::Ap203N916Source && IsA(*m_data, owner, "PRESENTATION_LAYER_ASSIGNMENT");
    }
    bool BorrowedForeignReal(std::size_t owner) const
    {
        return m_data->profile.kind == ProfileKind::Ap214MbxSource && IsA(*m_data, owner, "REAL_REPRESENTATION_ITEM");
    }
    std::string SourceAuthority(std::optional<std::size_t> owner = {}) const
    {
        // Used only after binding: attribute failures belong to the snapshot's pinned
        // source declarations, not the downstream rule profile or project restrictions.
        // Keep the existing AP203 labels so its diagnostic contract remains unchanged.
        Expects(m_data->catalog.has_value());
        if (owner && BorrowedForeignLayer(*owner))
        {
            return "N2635 borrowed foreign-layer transport (not a N916 declaration)";
        }
        if (owner && BorrowedForeignReal(*owner))
        {
            return "N2635 borrowed foreign REAL-item transport (not an AP214 declaration)";
        }
        return SourceArtifactName(m_data->profile.kind);
    }
    Progress& State(Phase phase)
    {
        return m_progress.at(static_cast<std::size_t>(phase));
    }
    void RunPhase(Phase phase, auto&& operation)
    {
        if (m_phase || State(phase) != Progress::NotRun)
        {
            throw std::logic_error("Semantic phase cannot be entered twice or recursively");
        }
        m_phase = phase;
        State(phase) = Progress::Running;
        operation();
        State(phase) = Progress::Passed;
        m_phase.reset();
    }
    static Progress FailureProgress(Status status)
    {
        switch (status)
        {
            case Status::Rejected:
                return Progress::Rejected;
            case Status::Unsupported:
                return Progress::Unsupported;
            case Status::ResourceLimit:
                return Progress::ResourceLimit;
            case Status::Checked:
            case Status::CheckedWithDiagnostics:
            case Status::Deferred:
            case Status::SourceFailure:
            case Status::CheckedSelectedDefinitions:
                break;
        }
        throw std::logic_error("Invalid semantic phase failure status");
    }
    static const char* ProgressName(Progress progress)
    {
        switch (progress)
        {
            case Progress::NotRun:
                return "NotRun";
            case Progress::Running:
                return "Running";
            case Progress::Passed:
                return "Passed";
            case Progress::Rejected:
                return "Rejected";
            case Progress::Unsupported:
                return "Unsupported";
            case Progress::ResourceLimit:
                return "ResourceLimit";
            case Progress::Deferred:
                return "Deferred";
        }
        throw std::logic_error("Unknown semantic phase progress");
    }
    std::vector<std::vector<std::size_t>> m_outgoing;
    std::vector<unsigned char> m_administrativeAttachments;
    std::vector<std::size_t> m_presentationRoots;
    std::unordered_set<std::size_t> m_preservedStyleOverrides; // Proven style-only invalid references into the selected body.
    std::size_t m_context = 0;
    std::size_t m_representation = 0;
    std::optional<std::size_t> m_wireframeRepresentation;
    std::optional<std::size_t> m_constructionRepresentation;
    bool m_independentRequest = false;
    std::shared_ptr<DefinitionInspectionData> m_definitionInspection;
    const std::map<std::size_t, std::vector<std::size_t>>* m_scopedPopulations = nullptr;
    std::vector<std::size_t> m_documentPhysical, m_documentBodies;
    std::unordered_set<std::size_t> m_documentBodySet;
    std::unordered_set<std::size_t> m_requestedProducts;
    bool m_assemblyDefinitionChecks = false;
    UnreferencedPlacementPolicy m_placementPolicy = UnreferencedPlacementPolicy::Reject;
    std::optional<RetainedPlacementDeclarations> m_retainedPlacements;
    std::unordered_set<std::string> m_selectedSourceDefinitions;
    class PopulationScope
    {
    public:

        PopulationScope(const std::map<std::size_t, std::vector<std::size_t>>*& slot, const std::map<std::size_t, std::vector<std::size_t>>& selected)
            : m_slot(slot), m_previous(slot)
        {
            m_slot = &selected;
        }
        PopulationScope(const PopulationScope&) = delete;
        PopulationScope& operator=(const PopulationScope&) = delete;
        ~PopulationScope()
        {
            m_slot = m_previous;
        }

    private:

        const std::map<std::size_t, std::vector<std::size_t>>*& m_slot;
        const std::map<std::size_t, std::vector<std::size_t>>* m_previous;
    };

    std::vector<std::size_t> PhysicalRepresentations(bool associated = true) const
    {
        if (m_data->independentParts.empty())
        {
            std::vector<std::size_t> result{ m_representation };
            if (associated && m_wireframeRepresentation)
            {
                result.push_back(*m_wireframeRepresentation);
            }
            if (associated && m_constructionRepresentation)
            {
                result.push_back(*m_constructionRepresentation);
            }
            return result;
        }
        return associated ? m_documentPhysical : m_documentBodies;
    }
    bool SelectedBodyRepresentation(std::size_t record) const
    {
        return m_data->independentParts.empty() ? record == m_representation : m_documentBodySet.contains(record);
    }
    std::vector<std::size_t> PhysicalItems(bool associated = true) const
    {
        std::vector<std::size_t> items;
        for (const auto representation : PhysicalRepresentations(associated))
        {
            for (const auto& value : FieldValue(representation, "items").children)
            {
                items.push_back(Target(value));
            }
        }
        return items;
    }
    void BuildReferenceIndex()
    {
        if (!m_outgoing.empty())
        {
            return;
        }
        m_outgoing.resize(m_data->types.size());
        const auto& references = m_data->source->References();
        for (std::size_t i = 0; i < references.size(); ++i)
        {
            m_outgoing[references[i].recordIndex].push_back(i);
        }
    }
    void IndependentProducts()
    {
        BuildReferenceIndex();
        auto plan = PlanIndependentProducts(
            *m_data,
            *m_outcome.productStructure,
            [this](Span span)
            {
                Visit(span);
            }
        );
        if (plan.failure)
        {
            throw Failure{ plan.failure->status, std::move(plan.failure->diagnostic) };
        }
        if (plan.parts.empty())
        {
            Fail(
                "IndependentProductsRequireDefinitions",
                "D2",
                "No independent definitions were established",
                m_data->source->Header()[2].span,
                Status::Unsupported
            );
        }
        if (m_data->selectedSourceRequest)
        {
            if (!IsAp214SourceProfile(m_data->profile.kind))
            {
                Fail(
                    "SourceIsolationProfileUnsupported",
                    "D2",
                    "Source clause isolation requires the pinned AP214 source profile",
                    m_data->source->Header()[2].span,
                    Status::Unsupported
                );
            }
            std::unordered_set<std::string> remaining;
            for (const auto& id : m_data->selectedSourceRequest->definitionIds)
            {
                if (!remaining.insert(id).second)
                {
                    Fail(
                        "DuplicateSelectedSourceDefinition",
                        "D2",
                        "A source definition may be requested only once",
                        m_data->source->Header()[2].span,
                        Status::Unsupported
                    );
                }
            }
            m_selectedSourceDefinitions = remaining;
            for (const auto& scope : plan.parts)
            {
                remaining.erase(Record(scope.definition).id);
            }
            if (!remaining.empty())
            {
                Fail(
                    "SelectedSourceDefinitionUnavailable",
                    "D2",
                    "Every selected definition must occur in the declared inventory",
                    m_data->source->Header()[2].span,
                    Status::Unsupported
                );
            }
        }
        // Scopes alter only role/population queries. All values, source indices,
        // type bindings and reference identities still belong to one source.
        for (const auto& definition : m_outcome.productStructure->Definitions())
        {
            m_requestedProducts.insert(definition.product);
        }
        for (const auto& scope : plan.parts)
        {
            PopulationScope selected(m_scopedPopulations, scope.populations);
            m_data->part = {};
            m_wireframeRepresentation.reset();
            m_constructionRepresentation.reset();
            try
            {
                Product();
            }
            catch (Failure& failure)
            {
                failure.diagnostic.source.related.push_back(Record(scope.definition).idSpan);
                throw;
            }
            m_data->independentParts.push_back({ std::move(m_data->part), {} });
        }
        AggregateDefinitionParts();
    }
    void AggregateDefinitionParts()
    {
        m_scopedPopulations = nullptr;
        m_data->part = {};
        m_data->part.shapeRole = "IndependentProductDefinitions";
        for (const auto& part : m_data->independentParts)
        {
            m_data->part.roles.insert(m_data->part.roles.end(), part.part.roles.begin(), part.part.roles.end());
            m_data->part.shapes.insert(m_data->part.shapes.end(), part.part.shapes.begin(), part.part.shapes.end());
            m_data->part.applicationProtocolPresent &= part.part.applicationProtocolPresent;
            for (const auto& role : part.part.roles)
            {
                if (role.name == "representation")
                {
                    m_documentBodies.push_back(role.recordIndex);
                    m_documentBodySet.insert(role.recordIndex);
                }
                if (role.name == "representation" || role.name == "wireframe_representation" || role.name == "construction_representation")
                {
                    m_documentPhysical.push_back(role.recordIndex);
                }
            }
        }
    }
    std::vector<std::size_t> SparseReach(std::size_t root)
    {
        std::unordered_set<std::size_t> visited;
        std::vector<std::size_t> stack{ root };
        const auto& references = m_data->source->References();
        while (!stack.empty())
        {
            const auto r = stack.back();
            stack.pop_back();
            if (!visited.insert(r).second)
            {
                continue;
            }
            Visit(Record(r).span);
            for (auto edge : m_outgoing[r])
            {
                Visit(references[edge].span);
                stack.push_back(references[edge].targetRecordIndex);
            }
        }
        std::vector<std::size_t> result(visited.begin(), visited.end());
        std::sort(result.begin(), result.end());
        return result;
    }
    Units IndependentUnitContext(const Part& part, const std::unordered_map<std::size_t, const UnitDimensionDefinition*>& dimensionByRecord)
    {
        const auto context = std::find_if(
            part.roles.begin(),
            part.roles.end(),
            [](const auto& role)
            {
                return role.name == "unit_context";
            }
        );
        Expects(context != part.roles.end());
        return UnitsForContext(context->recordIndex, dimensionByRecord);
    }
    Units UnitsForContext(std::size_t context, const std::unordered_map<std::size_t, const UnitDimensionDefinition*>& dimensionByRecord)
    {
        m_context = context;
        const auto reachable = SparseReach(m_context);
        m_data->units = {};
        for (auto record : reachable)
        {
            if (auto found = dimensionByRecord.find(record); found != dimensionByRecord.end())
            {
                m_data->units.dimensionDefinitions.push_back(*found->second);
            }
        }
        std::map<std::size_t, std::vector<std::size_t>> selected;
        std::vector<std::size_t> si;
        for (auto record : reachable)
        {
            if (IsA(*m_data, record, "SI_UNIT"))
            {
                si.push_back(record);
            }
        }
        selected.emplace(Schema(*m_data).RequireType("SI_UNIT"), std::move(si));
        PopulationScope scope(m_scopedPopulations, selected);
        UnitContext();
        return std::move(m_data->units);
    }
    void IndependentUnits()
    {
        auto dimensions = std::move(m_data->units.dimensionDefinitions);
        std::unordered_map<std::size_t, const UnitDimensionDefinition*> dimensionByRecord;
        for (const auto& dimension : dimensions)
        {
            dimensionByRecord.emplace(dimension.recordIndex, &dimension);
        }
        Units summary;
        summary.dimensionDefinitions = dimensions;
        std::unordered_set<std::size_t> observedSi;
        for (auto& part : m_data->independentParts)
        {
            part.units = IndependentUnitContext(part.part, dimensionByRecord);
            for (const auto& unit : part.units.siDefinitions)
            {
                if (observedSi.insert(unit.recordIndex).second)
                {
                    summary.siDefinitions.push_back(unit);
                }
            }
        }
        m_scopedPopulations = nullptr;
        m_data->units = std::move(summary); // Global assumptions are approved against the complete owned document.
    }

    [[noreturn]] void Fail(
        std::string key,
        std::string stage,
        std::string message,
        Span span,
        Status status = Status::Rejected,
        std::optional<std::size_t> owner = {},
        std::size_t component = 0,
        std::vector<std::size_t> path = {},
        std::string expected = {},
        std::string actual = {},
        std::string responsibility = {}
    )
    {
        if (status == Status::Rejected && owner && (BorrowedForeignLayer(*owner) || BorrowedForeignReal(*owner)))
        {
            // A malformed foreign field prevents the compatibility proof. Its
            // borrowed N2635 requirement is not a rule of the selected artifact.
            // Keep the field/key/location while refusing this profile exception.
            status = Status::Unsupported;
            stage = "D2";
            const auto authority = SourceAuthority(owner);
            if (responsibility.find(authority) == std::string::npos)
            {
                responsibility = authority + ": " + responsibility;
            }
        }
        if (m_phase)
        {
            State(*m_phase) = FailureProgress(status);
        }
        Diagnostic diagnostic;
        diagnostic.classification = status == Status::Unsupported   ? "ProfileUnsupported" :
                                    status == Status::ResourceLimit ? "ResourceLimit" :
                                                                      "SourceInvalid";
        diagnostic.source.key = std::move(key);
        diagnostic.source.stage = std::move(stage);
        diagnostic.source.message = std::move(message);
        diagnostic.source.primary = span;
        diagnostic.source.parameterPath = path;
        diagnostic.expected = std::move(expected);
        diagnostic.actual = std::move(actual);
        diagnostic.responsibility = std::move(responsibility);
        if (owner)
        {
            const auto& record = m_data->source->Records()[*owner];
            diagnostic.source.recordId = record.id;
            diagnostic.source.component = record.components[component].name;
            if (!path.empty() && path[0] <= record.components[component].parameters.size())
            {
                const auto view = AttributeAt(*m_data, *owner, component, path[0] - 1);
                diagnostic.field = view.name;
                diagnostic.declaredIn = view.declaredIn;
                diagnostic.declaredType = view.declaredType;
            }
        }
        throw Failure{ status, std::move(diagnostic) };
    }
    void Visit(Span span)
    {
        if (++m_outcome.dependencyVisits > m_outcome.limits.dependencyVisits)
        {
            Fail("DependencyVisitLimit", "D2", "Configured semantic traversal budget exceeded", span, Status::ResourceLimit);
        }
    }
    bool Interoperability() const
    {
        return m_data->policy == SourcePolicy::PreserveNonGeometric && m_data->profile.kind != ProfileKind::Ap203e2Planar;
    }
    bool CanPreserveMissingApplicationProtocol() const
    {
        return m_data->profile.kind == ProfileKind::Ap214MbxSource || m_data->profile.kind == ProfileKind::Ap203N916Source;
    }
    bool NonGeometricStyle(std::size_t record) const
    {
        // The fixed AP214 colour leaf carries only a palette name. It may join
        // the existing explicitly diagnosed nonphysical residue policy, never
        // create a geometry/part role. Keep other profiles' policy unchanged.
        if (IsAp214SourceProfile(m_data->profile.kind) && IsA(*m_data, record, "PRE_DEFINED_COLOUR"))
        {
            return true;
        }
        // Closed declarations whose outgoing references stay in the style chain.
        // STYLED_ITEM is deliberately absent: it references a geometric RI and
        // must still pass the existing selected-body attachment proof.
        constexpr std::string_view names[] = { "PRESENTATION_STYLE_ASSIGNMENT",
                                               "SURFACE_STYLE_USAGE",
                                               "SURFACE_SIDE_STYLE",
                                               "SURFACE_STYLE_FILL_AREA",
                                               "FILL_AREA_STYLE",
                                               "FILL_AREA_STYLE_COLOUR",
                                               "COLOUR_RGB",
                                               "SURFACE_STYLE_RENDERING",
                                               "SURFACE_STYLE_TRANSPARENT",
                                               "CURVE_STYLE",
                                               "PRE_DEFINED_CURVE_FONT",
                                               "POINT_STYLE",
                                               "PRE_DEFINED_POINT_MARKER_SYMBOL" };
        return std::any_of(
            std::begin(names),
            std::end(names),
            [&](auto name)
            {
                return IsA(*m_data, record, name);
            }
        );
    }
    void Preserve(Failure failure, SemanticData::PreservationProof proof = SemanticData::PreservationProof::ExistingMetadata)
    {
        if (m_outcome.definitionInspectionRequested && m_data->DiagnosticFactCount() >= m_outcome.limits.preservedDiagnostics)
        {
            // Diagnostic-only continuation shares one request ledger with
            // provisional metadata; never allocate a new pending fact after
            // earlier definition failures have consumed that capacity.
            Fail(
                "DefinitionDiagnosticLimit",
                "D2",
                "Independent definition diagnostic capacity exceeded",
                failure.diagnostic.source.primary,
                Status::ResourceLimit
            );
        }
        // Capture the original strict diagnostic, not a replacement verdict. It
        // remains provisional until attachment and physical-dependency checks pass.
        m_data->pendingIssues.push_back({ failure.status, std::move(failure.diagnostic), proof });
        // Fail marked the phase terminal. Restore it only for this provisional
        // issue; the final proof still precedes every typed snapshot publication.
        State(*m_phase) = Progress::Running;
        if (m_data->DiagnosticFactCount() > m_outcome.limits.preservedDiagnostics)
        {
            Fail(
                "PreservedDiagnosticLimit",
                "D2",
                "Interoperability diagnostic capacity exceeded",
                m_data->pendingIssues.back().diagnostic.source.primary,
                Status::ResourceLimit
            );
        }
    }
    const step_source::Record& Record(std::size_t index) const
    {
        return m_data->source->Records().at(index);
    }
    const BoundAttribute& Binding(std::size_t record, std::string_view name) const
    {
        return m_data->fields.at(record).attributes.at(FindFieldBinding(*m_data, record, name));
    }
    const Value& FieldValue(std::size_t record, std::string_view name) const
    {
        return *Binding(record, name).declaration.value;
    }
    std::size_t Target(const Value& value) const
    {
        return ResolveBoundReference(*m_data, value);
    }
    std::size_t UnboundTarget(const Value& value) const
    {
        return IndexOf(*m_data, value.text);
    }
    enum class FieldDetail
    {
        SpanOnly, // Preserve record/component but omit parameter and declaration details.
        AttributePath
    };
    [[noreturn]] void FieldFailure(
        std::string key,
        std::string stage,
        std::string message,
        Span span,
        Status status,
        std::size_t owner,
        std::string_view name,
        FieldDetail detail,
        std::string expected = {},
        std::string actual = {},
        std::string responsibility = {}
    )
    {
        const auto& bound = Binding(owner, name);
        Fail(
            std::move(key),
            std::move(stage),
            std::move(message),
            span,
            status,
            owner,
            bound.component,
            detail == FieldDetail::AttributePath ? std::vector<std::size_t>{ bound.parameter + 1 } : std::vector<std::size_t>{},
            std::move(expected),
            std::move(actual),
            std::move(responsibility)
        );
    }

    void Binding()
    {
        m_outcome.schemaSelection = SelectSchema(*m_data->source, m_data->geometryScope);
        // An explicit general-BRep request selects a separate source subset.
        // The exact N2635 HEADER identity stays unchanged; default P0 checking
        // must not acquire newly admitted geometry or management declarations.
        if (m_data->geometryScope == GeometryScope::GeneralBrep && m_outcome.schemaSelection->qualified &&
            m_outcome.schemaSelection->qualified->kind == ProfileKind::Ap203e2Planar)
        {
            m_outcome.schemaSelection->qualified->profile = Ap203GeneralSourceProfile;
            m_outcome.schemaSelection->qualified->kind = ProfileKind::Ap203e2GeneralSource;
        }
        const auto& identifiers = m_data->source->Header()[2].parameters[0].children;
        if (identifiers.size() != 1)
        {
            Fail(
                "UnsupportedSchemaBinding",
                "D2",
                "One precise schema name/OID is required",
                m_data->source->Header()[2].span,
                Status::Unsupported,
                {},
                0,
                {},
                "one N2635 identifier",
                std::to_string(identifiers.size()),
                "profile registry"
            );
        }
        // Header recognition alone cannot select an arbitrary type/rule implementation.
        const auto& literal = identifiers[0].text;
        if (m_outcome.requestedInterpretation == SchemaInterpretation::SourceBoundAp214As2010 && m_outcome.schemaSelection->qualified)
        {
            Fail(
                "SourceInterpretationNotApplicable",
                "D2",
                "Source-bound AP214 interpretation requires an unbound source declaration",
                identifiers[0].span,
                Status::Unsupported
            );
        }
        if (!m_outcome.schemaSelection->qualified)
        {
            try
            {
                Fail(
                    "UnsupportedSchemaBinding",
                    "D2",
                    "HEADER identifier is not bound to the fixed N2635 registry entry",
                    identifiers[0].span,
                    Status::Unsupported,
                    {},
                    0,
                    {},
                    std::string(SchemaName) + " {1 0 10303 403 2 1 2}",
                    literal,
                    "profile registry; no APD/year inference"
                );
            }
            catch (Failure& failure)
            {
                auto implementation = SelectExplicitInterpretation(*m_outcome.schemaSelection, m_outcome.requestedInterpretation);
                if (!implementation)
                {
                    throw;
                }
                // This is a user-selected interpretation, not a proof that an
                // unknown schema is nonphysical or equivalent. Preserve the
                // exact strict failure in a separate, owning diagnostic set.
                // It shares the configured storage quota with metadata issues.
                if (m_outcome.limits.preservedDiagnostics == 0)
                {
                    Fail(
                        "PreservedDiagnosticLimit",
                        "D2",
                        "Source interpretation diagnostic capacity exceeded",
                        failure.diagnostic.source.primary,
                        Status::ResourceLimit
                    );
                }
                m_data->interpretationDiagnostics.push_back(std::move(failure.diagnostic));
                // Apply the assumption only after its mandatory diagnostic can
                // be retained. A quota failure keeps the original unbound selection.
                m_outcome.schemaSelection->qualified = std::move(*implementation);
                m_outcome.schemaSelection->appliedInterpretation = m_outcome.requestedInterpretation;
                m_data->appliedInterpretation = m_outcome.schemaSelection->appliedInterpretation;
                State(Phase::Binding) = Progress::Running;
            }
        }
        m_data->profile = *m_outcome.schemaSelection->qualified;
        m_data->schemaIdentity = m_outcome.schemaSelection->identifiers.front();
        // Fix declaration identity before any membership bits, reference types or derived slots are bound.
        m_data->catalog.emplace(&Schema(m_data->profile.kind));
    }

    void CheckForeignDeclarations()
    {
        constexpr std::string_view mbxForeignDeclarations[] = { "MECHANICAL_CONTEXT", "DESIGN_CONTEXT", "REAL_REPRESENTATION_ITEM" };
        const bool n916 = m_data->profile.kind == ProfileKind::Ap203N916Source;
        if (m_data->profile.kind != ProfileKind::Ap214MbxSource && !n916)
        {
            return;
        }
        const auto foreign = n916 ? std::span<const std::string_view>(ap203_n916::ForeignDeclarations) :
                                    std::span<const std::string_view>(mbxForeignDeclarations);
        for (const auto name : foreign)
        {
            for (const auto record : Population(name))
            {
                if (name == "REAL_REPRESENTATION_ITEM")
                {
                    try
                    {
                        // Preserve the exact original unknown-type verdict,
                        // before any borrowed field interpretation is approved.
                        Fail(
                            "UnsupportedEntityType",
                            "D2",
                            "Required content is outside the closed source profile",
                            Record(record).components.front().span,
                            Status::Unsupported,
                            record,
                            0,
                            {},
                            "registered profile entity",
                            std::string(name),
                            "closed type guard; unknown content is not skipped"
                        );
                    }
                    catch (Failure& failure)
                    {
                        if (!Interoperability() || m_data->geometryScope != GeometryScope::GeneralBrep)
                        {
                            throw;
                        }
                        Preserve(std::move(failure), SemanticData::PreservationProof::ForeignRealItem);
                    }
                    continue;
                }
                if (n916 && std::none_of(
                                Record(record).components.begin(),
                                Record(record).components.end(),
                                [&](const auto& component)
                                {
                                    return component.name == name;
                                }
                            ))
                {
                    continue; // Inherited diagnostic descriptors are not additional source instances.
                }
                Visit(Record(record).span);
                try
                {
                    // Each listed type is absent from the selected artifact.
                    // The separately pinned N2635 descriptor can explain its
                    // transport fields, but cannot erase that source error.
                    Fail(
                        "UnsupportedEntityType",
                        "D2",
                        n916 ? "Required content is outside the fixed modified N916 source declarations" :
                               "Legacy context is outside the selected AP214 declarations",
                        Record(record).span,
                        Status::Unsupported,
                        record,
                        0,
                        {},
                        n916 ? "modified N916 declaration" : "AP214E3_2010.exp declaration",
                        std::string(name),
                        n916 ? "modified N916 source type membership; borrowed transport does not admit an entity" : "AP214 source type membership"
                    );
                }
                catch (Failure& failure)
                {
                    if (!Interoperability() || m_data->geometryScope != GeometryScope::GeneralBrep ||
                        (n916 && name != "PRESENTATION_LAYER_ASSIGNMENT"))
                    {
                        throw;
                    }
                    Preserve(std::move(failure));
                }
            }
        }
    }
    void Mapping()
    {
        const auto& catalog = Schema(*m_data);
        const auto& records = m_data->source->Records();
        m_data->types.resize(records.size());
        m_data->populations.resize(catalog.names.size());
        for (std::size_t r = 0; r < records.size(); ++r)
        {
            Visit(records[r].span);
            std::vector<std::string> explicitTypes;
            std::unordered_set<std::string_view> unique; // Borrows immutable source component names for this record only.
            for (std::size_t c = 0; c < records[r].components.size(); ++c)
            {
                const auto& part = records[r].components[c];
                if (!unique.insert(part.name).second)
                {
                    Fail(
                        "DuplicateComplexConstituent",
                        "D1",
                        "A partial entity occurs twice in one instance",
                        part.span,
                        Status::Rejected,
                        r,
                        c,
                        {},
                        "unique constituent",
                        part.name,
                        "external mapping"
                    );
                }
                const auto type = catalog.Find(part.name);
                if (!type)
                {
                    Fail(
                        records[r].complex ? "UnsupportedComplexConstituent" : "UnsupportedEntityType",
                        "D2",
                        "Required content is outside the closed source profile",
                        part.span,
                        Status::Unsupported,
                        r,
                        c,
                        {},
                        "registered profile entity",
                        part.name,
                        "closed type guard; unknown content is not skipped"
                    );
                }
                m_data->types[r] |= catalog.ancestors[*type];
                explicitTypes.push_back(part.name);
                if (!records[r].complex && !catalog.declarations[*type].simple)
                {
                    Fail(
                        part.name == "FACE_SURFACE" ? "UnsupportedFaceSubtype" : "UnsupportedEntityMapping",
                        "D2",
                        "This declaration is not an admitted simple leaf",
                        part.span,
                        Status::Unsupported,
                        r,
                        c,
                        {},
                        "admitted simple leaf",
                        part.name,
                        "profile mapping"
                    );
                }
            }
            if (records[r].complex)
            {
                std::optional<ComplexMappingAssumption> mapping;
                if (catalog.HasSubtypeConflict(m_data->types[r]))
                {
                    Fail(
                        "ComplexSubtypeConflict",
                        "D1",
                        "Audited mutually exclusive subtype branches occur together",
                        records[r].span,
                        Status::Rejected,
                        r,
                        0,
                        {},
                        "compatible partial types",
                        "ONEOF conflict",
                        SourceAuthority(r) + " audited subtype groups"
                    );
                }
                if (!std::is_sorted(explicitTypes.begin(), explicitTypes.end()))
                {
                    try
                    {
                        Fail(
                            "ComplexConstituentOrderInvalid",
                            "D1",
                            "External mapping facets must follow ascending type-name order",
                            records[r].span,
                            Status::Rejected,
                            r,
                            0,
                            {},
                            "ascending type names",
                            "source order",
                            "Part21 external mapping"
                        );
                    }
                    catch (Failure& failure)
                    {
                        if (m_data->requestedComplexMappingInterpretation != ComplexMappingInterpretation::RegisteredFacetNames)
                        {
                            throw;
                        }
                        if (m_data->pendingIssues.size() + m_data->interpretationDiagnostics.size() + m_data->complexMappingDiagnostics.size() >=
                            m_outcome.limits.preservedDiagnostics)
                        {
                            Fail(
                                "PreservedDiagnosticLimit",
                                "D2",
                                "Complex mapping diagnostic capacity exceeded",
                                records[r].span,
                                Status::ResourceLimit
                            );
                        }
                        m_data->complexMappingDiagnostics.push_back(std::move(failure.diagnostic));
                        State(*m_phase) = Progress::Running;
                        mapping = ComplexMappingAssumption{ r, {} };
                        // RegisteredFacetNames project contract: STEP_SEMANTICS/README.md.
                        // Only this local lookup vector is sorted. Fields() binds
                        // each explicit slot by its actual source component NAME
                        // and index, including inherited/derived/type predicates.
                        // Unique names and exclusive subtype guards above, the
                        // complete ancestor set and admitted signature below,
                        // then ALL fields/units/ownership checks must succeed.
                        // Original bytes, facet order, values and reference paths
                        // remain unchanged; this is not source conformance/repair.
                        for (std::size_t c = 0; c < records[r].components.size(); ++c)
                        {
                            Visit(records[r].components[c].span);
                            mapping->canonicalComponentOrder.push_back(c);
                        }
                        std::sort(
                            mapping->canonicalComponentOrder.begin(),
                            mapping->canonicalComponentOrder.end(),
                            [&](auto a, auto b)
                            {
                                return records[r].components[a].name < records[r].components[b].name;
                            }
                        );
                        std::sort(explicitTypes.begin(), explicitTypes.end());
                    }
                }
                for (std::size_t t = 0; t < catalog.names.size(); ++t)
                {
                    if (m_data->types[r].test(t) && !unique.count(catalog.names[t]))
                    {
                        Fail(
                            "MissingComplexConstituent",
                            "D1",
                            "An inherited partial entity is missing",
                            records[r].span,
                            Status::Rejected,
                            r,
                            0,
                            {},
                            std::string(catalog.names[t]),
                            "absent",
                            "Part21 external mapping"
                        );
                    }
                }
                if (!catalog.AdmitsComplex(explicitTypes))
                {
                    Fail(
                        "UnsupportedComplexSignature",
                        "D2",
                        m_data->profile.kind == ProfileKind::Ap203N916Source ?
                            "Complete complex mapping is outside the modified N916 source subset" :
                        m_data->profile.kind == ProfileKind::Ap203e2GeneralSource ?
                            "Complete complex mapping is outside the selected N2635 general source signatures" :
                        IsAp214SourceProfile(m_data->profile.kind) ? "Complete complex mapping is outside the selected AP214 source signatures" :
                                                                     "Complete complex mapping is outside the six admitted signatures",
                        records[r].span,
                        Status::Unsupported,
                        r,
                        0,
                        {},
                        m_data->profile.kind == ProfileKind::Ap203N916Source      ? "registered modified N916 source signature" :
                        m_data->profile.kind == ProfileKind::Ap203e2GeneralSource ? "registered N2635 general source signature" :
                        IsAp214SourceProfile(m_data->profile.kind)                ? "registered AP214 source signature" :
                                                                                    "one of six signatures",
                        "other signature",
                        "profile mapping"
                    );
                }
                if (mapping)
                {
                    m_data->complexMappingAssumptions.push_back(std::move(*mapping));
                }
            }
            for (std::size_t t = 0; t < catalog.names.size(); ++t)
            {
                if (m_data->types[r].test(t))
                {
                    m_data->populations[t].push_back(r);
                }
            }
        }
    }

    bool DeferUnselectedFaceClause(std::size_t face, const Failure& original)
    {
        if (!m_data->selectedSourceRequest || !IsAp214SourceProfile(m_data->profile.kind) ||
            original.diagnostic.source.key != "SourceRuleViolation:FACE.WR2")
        {
            return false;
        }
        const auto owner = std::find_if(
            m_data->part.roles.begin(),
            m_data->part.roles.end(),
            [](const auto& role)
            {
                return role.name == "definition";
            }
        );
        if (owner == m_data->part.roles.end() || m_selectedSourceDefinitions.contains(Record(owner->recordIndex).id))
        {
            return false;
        }
        if (m_data->isolatedSourceFailures.size() >= m_data->selectedSourceRequest->maximumIsolatedFailures ||
            m_data->DiagnosticFactCount() >= m_outcome.limits.preservedDiagnostics)
        {
            Fail(
                "SourceIsolationDiagnosticLimit",
                "D2",
                "Explicit source isolation diagnostic capacity exceeded",
                Record(face).span,
                Status::ResourceLimit,
                face
            );
        }
        // Retain the actual defining-clause failure, without repairing source,
        // granting metadata approval or declaring the candidate part qualified.
        // Whole-source checks and the separate locality proof still follow.
        m_data->isolatedSourceFailures.push_back({ Record(owner->recordIndex).id, original.diagnostic });
        State(*m_phase) = Progress::Running;
        return true;
    }
    bool DeferCylindricalBandRepair(std::size_t face, const Failure& original)
    {
        const auto& roles = m_data->part.roles;
        const auto definition = std::find_if(
            roles.begin(),
            roles.end(),
            [](const auto& r)
            {
                return r.name == "definition";
            }
        );
        if (definition == roles.end() || m_data->cylindricalBandRepairs.empty())
        {
            return false;
        }
        const auto& definitionId = Record(definition->recordIndex).id;
        const auto& faceId = Record(face).id;
        const auto& bounds = FieldValue(face, "bounds").children;
        if (bounds.size() != 2 || !IsA(*m_data, face, "ADVANCED_FACE") ||
            !IsA(*m_data, Target(FieldValue(face, "face_geometry")), "CYLINDRICAL_SURFACE"))
        {
            return false;
        }
        const std::array<std::string, 2> ids{ Record(Target(bounds[0])).id, Record(Target(bounds[1])).id };
        std::vector<std::size_t> matched;
        for (const auto& shape : m_data->part.shapes)
        {
            const auto& faces = FieldValue(shape.shell, "cfs_faces").children;
            if (std::none_of(
                    faces.begin(),
                    faces.end(),
                    [&](const auto& f)
                    {
                        return Target(f) == face;
                    }
                ))
            {
                continue;
            }
            if (shape.role != "MaterialSolid")
            {
                return false;
            }
            const auto found = std::find_if(
                m_data->cylindricalBandRepairs.begin(),
                m_data->cylindricalBandRepairs.end(),
                [&](const auto& plan)
                {
                    return plan.definitionId == definitionId && plan.shapeId == Record(shape.shape).id && plan.faceId == faceId &&
                           ((plan.sourceOuterBoundIds[0] == ids[0] && plan.sourceOuterBoundIds[1] == ids[1]) ||
                            (plan.sourceOuterBoundIds[0] == ids[1] && plan.sourceOuterBoundIds[1] == ids[0]));
                }
            );
            if (found == m_data->cylindricalBandRepairs.end())
            {
                return false;
            }
            matched.push_back(static_cast<std::size_t>(found - m_data->cylindricalBandRepairs.begin()));
        }
        if (matched.empty())
        {
            return false;
        }
        if (m_data->pendingIssues.size() + m_data->interpretationDiagnostics.size() + m_data->complexMappingDiagnostics.size() +
                m_data->cylindricalBandRepairDiagnostics.size() >=
            m_outcome.limits.preservedDiagnostics)
        {
            Fail("PreservedDiagnosticLimit", "D2", "Explicit repair source diagnostic capacity exceeded", Record(face).span, Status::ResourceLimit);
        }
        // This is a failed defining source clause, never metadata preservation.
        // Typed bindings remain original. Publication is deferred for an explicit
        // downstream source-region proof and native repair; no outer is guessed.
        m_data->cylindricalBandRepairDiagnostics.push_back(original.diagnostic);
        for (const auto index : matched)
        {
            ++m_data->cylindricalBandRepairUses[index];
        }
        m_data->part.deferredRegionFaces.push_back(face);
        m_data->part.deferredCylindricalBandRepairs.push_back(face);
        State(*m_phase) = Progress::Running;
        return true;
    }
    bool OpaqueTopologyCaption(std::size_t record) const
    {
        // Explicit audited label owners from pinned N2635/AP214/modified N916.
        // New topology registrations must not acquire this exception implicitly.
        constexpr std::string_view owners[]{ "ADVANCED_FACE", "EDGE_CURVE",       "VERTEX_POINT", "CLOSED_SHELL", "OPEN_SHELL",
                                             "FACE_BOUND",    "FACE_OUTER_BOUND", "EDGE_LOOP",    "ORIENTED_EDGE" };
        const auto& source = Record(record);
        return !source.complex && source.components.size() == 1 &&
               std::find(std::begin(owners), std::end(owners), source.components.front().name) != std::end(owners);
    }

    void Scalar(const Value& value, FieldKind kind, const Field& field, std::size_t r, std::size_t c, std::vector<std::size_t> path)
    {
        const auto mismatch = [&]
        {
            Fail(
                "AttributeTypeMismatch",
                "D1",
                "Serialized value does not match the declared field type",
                value.span,
                Status::Rejected,
                r,
                c,
                path,
                std::string(field.type),
                step_source::ValueKindName(value.kind),
                SourceAuthority(r) + " explicit attribute"
            );
        };
        switch (kind)
        {
            case FieldKind::String:
                if (value.kind == ValueKind::OpaqueString)
                {
                    try
                    {
                        // Unknown encoding is unavailable text, not a known
                        // empty/malformed label. Keep the strict diagnostic and
                        // require a field-specific ownership proof before any
                        // typed snapshot can use this interoperability exception.
                        Fail(
                            "OpaqueDataTextUnestablished",
                            "D1",
                            "DATA text interpretation remains unestablished",
                            value.span,
                            Status::Unsupported,
                            r,
                            c,
                            path,
                            std::string(field.type),
                            "OpaqueString",
                            "project DATA carrier policy; not established source STRING semantics"
                        );
                    }
                    catch (Failure& failure)
                    {
                        const auto& issue = failure.diagnostic;
                        // AP214/N2635 representation names remain purpose-sensitive.
                        // Modified N916 has no corresponding name predicates;
                        // its separate proof must establish selected ownership.
                        const bool display = (issue.declaredIn == "PRODUCT" && (issue.field == "id" || issue.field == "name")) ||
                                             (issue.declaredIn == "PRESENTATION_LAYER_ASSIGNMENT" && issue.field == "name");
                        const bool partIdentity = issue.field == "id" &&
                                                  (issue.declaredIn == "PRODUCT_DEFINITION_FORMATION" || issue.declaredIn == "PRODUCT_DEFINITION");
                        const bool shapeCaption = issue.declaredIn == "REPRESENTATION_ITEM" && issue.field == "name" &&
                                                  (IsA(*m_data, r, "MANIFOLD_SOLID_BREP") || OpaqueTopologyCaption(r));
                        const bool n916Representation = m_data->profile.kind == ProfileKind::Ap203N916Source &&
                                                        issue.declaredIn == "REPRESENTATION" && issue.field == "name";
                        const bool asciiPurpose = IsAp214SourceProfile(m_data->profile.kind) && issue.declaredIn == "REPRESENTATION" &&
                                                  issue.field == "name" && m_data->source->IsAssumedNonAsciiData(value);
                        const bool administrative = OpaqueAdministrativeField(issue.declaredIn, issue.field);
                        const bool listElement = administrative && OpaqueAdministrativeList(issue.declaredIn, issue.field);
                        if (!Interoperability() || m_data->geometryScope != GeometryScope::GeneralBrep ||
                            !(display || partIdentity || shapeCaption || administrative || n916Representation || asciiPurpose) ||
                            path.size() != (listElement ? 2u : 1u))
                        {
                            throw;
                        }
                        const auto proof = display            ? SemanticData::PreservationProof::OpaqueDisplayText :
                                           partIdentity       ? SemanticData::PreservationProof::OpaquePartIdentity :
                                           shapeCaption       ? SemanticData::PreservationProof::OpaqueShapeCaption :
                                           n916Representation ? SemanticData::PreservationProof::N916RepresentationCaption :
                                           asciiPurpose       ? SemanticData::PreservationProof::AsciiInvariantRepresentationPurpose :
                                                                SemanticData::PreservationProof::OpaqueAdministrativeLabel;
                        Preserve(std::move(failure), proof);
                    }
                    break;
                }
                if (value.kind != ValueKind::String)
                {
                    mismatch();
                }
                break;
            case FieldKind::Real:
                if (value.kind != ValueKind::Real)
                {
                    mismatch();
                }
                break;
            case FieldKind::Number:
                if (value.kind != ValueKind::Integer && value.kind != ValueKind::Real)
                {
                    mismatch();
                }
                break;
            case FieldKind::Integer:
                if (value.kind != ValueKind::Integer)
                {
                    mismatch();
                }
                break;
            case FieldKind::Boolean:
                if (value.kind != ValueKind::Enumeration || (value.text != "T" && value.text != "F"))
                {
                    Fail(
                        "BooleanValueInvalid",
                        "D1",
                        "BOOLEAN accepts T/F, not LOGICAL U",
                        value.span,
                        Status::Rejected,
                        r,
                        c,
                        path,
                        "BOOLEAN T/F",
                        std::string(m_data->source->Raw(value.span)),
                        SourceAuthority(r) + " BOOLEAN"
                    );
                }
                break;
            case FieldKind::Enumeration:
            {
                if (value.kind != ValueKind::Enumeration)
                {
                    mismatch();
                }
                if (!Schema(*m_data).ContainsEnumeration(field.type, value.text))
                {
                    Fail(
                        "EnumerationValueInvalid",
                        "D1",
                        "Enumeration token is absent from the fixed " + SourceAuthority(r) + " type",
                        value.span,
                        Status::Rejected,
                        r,
                        c,
                        path,
                        std::string(field.type),
                        value.text,
                        "fixed " + SourceAuthority(r) + " enumeration"
                    );
                }
                break;
            }
            case FieldKind::Logical:
                if (value.kind != ValueKind::Enumeration || (value.text != "T" && value.text != "F" && value.text != "U"))
                {
                    Fail(
                        "LogicalValueInvalid",
                        "D1",
                        "LOGICAL accepts T/F/U without resolving unknown",
                        value.span,
                        Status::Rejected,
                        r,
                        c,
                        path,
                        "LOGICAL T/F/U",
                        std::string(m_data->source->Raw(value.span)),
                        SourceAuthority(r) + " LOGICAL"
                    );
                }
                break;
            case FieldKind::Reference:
                if (value.kind != ValueKind::Reference)
                {
                    mismatch();
                }
                if (!Schema(*m_data).MatchesReference(m_data->types.at(UnboundTarget(value)), field.type))
                {
                    try
                    {
                        Fail(
                            "ReferenceTypeMismatch",
                            "D1",
                            "Reference target does not instantiate the declared type/SELECT",
                            value.span,
                            Status::Rejected,
                            r,
                            c,
                            path,
                            std::string(field.type),
                            Record(UnboundTarget(value)).components[0].name,
                            SourceAuthority(r) + " explicit reference type"
                        );
                    }
                    catch (Failure& failure)
                    {
                        failure.diagnostic.source.targetId = value.text;
                        failure.diagnostic.source.related.push_back(Record(UnboundTarget(value)).idSpan);
                        // N2635 APPROVED_ITEM excludes PD/formation; MBx exports
                        // can also put a CLOSED_SHELL in a style-only override slot.
                        // These mismatches remain provisional source errors. Their
                        // separate attachment and physical-exclusion proofs must
                        // finish before any typed snapshot can publish.
                        const auto target = UnboundTarget(value);
                        const bool approval = m_data->profile.kind == ProfileKind::Ap203e2GeneralSource && field.type == "APPROVED_ITEM" &&
                                              IsA(*m_data, r, "CC_DESIGN_APPROVAL") &&
                                              (IsA(*m_data, target, "PRODUCT_DEFINITION") || IsA(*m_data, target, "PRODUCT_DEFINITION_FORMATION"));
                        const bool styleOverride = m_data->profile.kind == ProfileKind::Ap214MbxSource &&
                                                   m_data->geometryScope == GeometryScope::GeneralBrep && field.name == "over_ridden_style" &&
                                                   IsA(*m_data, r, "OVER_RIDING_STYLED_ITEM") && IsA(*m_data, target, "CLOSED_SHELL");
                        // AP214E3_2010.exp STYLED_ITEM.item is RI, not a
                        // representation SELECT. Preserve this source violation
                        // provisionally; selected presentation ownership and the
                        // exact body representation are proved after binding.
                        const bool styledRepresentation = IsAp214SourceProfile(m_data->profile.kind) &&
                                                          m_data->geometryScope == GeometryScope::GeneralBrep && field.name == "item" &&
                                                          field.type == "REPRESENTATION_ITEM" && path.size() == 1 && IsA(*m_data, r, "STYLED_ITEM") &&
                                                          IsA(*m_data, target, "SHAPE_REPRESENTATION");
                        if (!Interoperability() || (!approval && !styleOverride && !styledRepresentation))
                        {
                            throw;
                        }
                        Preserve(std::move(failure));
                    }
                }
                m_data->referenceTargets.emplace(&value, UnboundTarget(value));
                break;
            case FieldKind::Measure:
                if (value.kind != ValueKind::Typed || value.children.size() != 1)
                {
                    mismatch();
                }
                if (!Schema(*m_data).AdmitsMeasure(value.text))
                {
                    Fail(
                        "UnsupportedMeasureBranch",
                        "D2",
                        IsAp214SourceProfile(m_data->profile.kind) ? "This source profile consumes the fixed registered measure SELECT alternatives" :
                                                                     "This profile consumes only the registered length measure family",
                        value.span,
                        Status::Unsupported,
                        r,
                        c,
                        path,
                        IsAp214SourceProfile(m_data->profile.kind) ? "registered typed measure" : "typed length measure",
                        value.text,
                        "profile measure SELECT guard"
                    );
                }
                // The fixed AP214/N2635 count_measure and numeric_measure are
                // NUMBER, descriptive_measure is STRING, and the other admitted
                // alternatives are REAL. No integer/real/string coercion occurs.
                if (value.text == "DESCRIPTIVE_MEASURE" && IsAp214SourceProfile(m_data->profile.kind))
                {
                    if (value.children[0].kind == ValueKind::OpaqueString)
                    {
                        // Reuse the existing unavailable-text classification;
                        // unknown encoding is not a proven malformed scalar.
                        Scalar(value.children[0], FieldKind::String, field, r, c, path);
                    }
                    if (value.children[0].kind != ValueKind::String)
                    {
                        mismatch();
                    }
                }
                else if (
                    value.children[0].kind != ValueKind::Real &&
                    !((m_data->profile.kind == ProfileKind::Ap203e2GeneralSource || m_data->profile.kind == ProfileKind::Ap203N916Source ||
                       IsAp214SourceProfile(m_data->profile.kind)) &&
                      (value.text == "COUNT_MEASURE" || value.text == "NUMERIC_MEASURE") && value.children[0].kind == ValueKind::Integer)
                )
                {
                    mismatch();
                }
                break;
            case FieldKind::SizeSelect:
                // SIZE_SELECT is not MEASURE_VALUE: preserve its typed positive
                // scalar, typed descriptive string or generic MWU reference.
                // The referenced measure keeps its own nominal unit/type; it
                // must not be recast as LENGTH_MEASURE_WITH_UNIT by its use here.
                if (value.kind == ValueKind::Reference)
                {
                    auto reference = field;
                    reference.kind = FieldKind::Reference;
                    reference.type = "MEASURE_WITH_UNIT";
                    Scalar(value, FieldKind::Reference, reference, r, c, path);
                }
                else if (
                    value.kind != ValueKind::Typed || value.children.size() != 1 ||
                    !((value.text == "POSITIVE_LENGTH_MEASURE" && value.children[0].kind == ValueKind::Real) ||
                      (value.text == "DESCRIPTIVE_MEASURE" && value.children[0].kind == ValueKind::String))
                )
                {
                    mismatch();
                }
                break;
            case FieldKind::TrimmingSelect:
                if (value.kind == ValueKind::Reference)
                {
                    auto reference = field;
                    reference.kind = FieldKind::Reference;
                    Scalar(value, FieldKind::Reference, reference, r, c, path);
                }
                else if (
                    value.kind != ValueKind::Typed || value.text != "PARAMETER_VALUE" || value.children.size() != 1 ||
                    value.children[0].kind != ValueKind::Real
                )
                {
                    mismatch();
                }
                break;
            case FieldKind::MarkerSelect:
                if (value.kind == ValueKind::Reference)
                {
                    auto reference = field;
                    reference.kind = FieldKind::Reference;
                    Scalar(value, FieldKind::Reference, reference, r, c, path);
                }
                else if (value.kind == ValueKind::Typed && value.text == "MARKER_TYPE" && value.children.size() == 1)
                {
                    auto marker = field;
                    marker.kind = FieldKind::Enumeration;
                    marker.type = "MARKER_TYPE";
                    Scalar(value.children[0], FieldKind::Enumeration, marker, r, c, path);
                }
                else
                {
                    mismatch();
                }
                break;
            case FieldKind::Aggregate:
                throw std::logic_error("Nested aggregate descriptor not expected");
        }
    }

    void AggregateValue(
        const Value& value,
        const Field& field,
        std::size_t r,
        std::size_t c,
        const std::vector<std::size_t>& path,
        std::string_view declaredIn,
        std::size_t depth = 0
    )
    {
        // Validate each declared LIST dimension without flattening source rows. The
        // outer field/member visit and error order is unchanged for existing AP203;
        // only nested fields recurse, adding their actual one-based source indices.
        const auto* inner = depth ? &CatalogAt(field.nestedAggregates, depth - 1) : nullptr;
        const auto minimum = inner ? inner->minimum : field.minimum;
        const auto maximum = inner ? inner->maximum.value_or(static_cast<std::size_t>(-1)) : field.maximum;
        const auto unique = inner ? inner->uniqueElements : field.unique != Unique::None;
        const auto set = inner ? inner->aggregate == "SET" : field.unique == Unique::Set;
        if (value.kind != ValueKind::Aggregate)
        {
            Fail(
                "AttributeTypeMismatch",
                "D1",
                "Expected an aggregate value",
                value.span,
                Status::Rejected,
                r,
                c,
                path,
                "aggregate",
                step_source::ValueKindName(value.kind),
                SourceAuthority(r) + " aggregate"
            );
        }
        if (value.children.size() < minimum || value.children.size() > maximum)
        {
            Fail(
                "AggregateCardinalityViolation",
                "D1",
                "Aggregate cardinality violates the declared bounds",
                value.span,
                Status::Rejected,
                r,
                c,
                path,
                "declared bounds",
                std::to_string(value.children.size()),
                SourceAuthority(r) + " aggregate"
            );
        }
        std::unordered_map<std::string, Span> members;
        for (std::size_t i = 0; i < value.children.size(); ++i)
        {
            const auto& child = value.children[i];
            auto childPath = path;
            childPath.push_back(i + 1);
            Visit(child.span);
            if (depth < field.nestedAggregates.size())
            {
                AggregateValue(child, field, r, c, childPath, declaredIn, depth + 1);
            }
            else
            {
                Scalar(child, field.elementKind, field, r, c, childPath);
            }
            std::optional<Span> duplicate;
            if (unique && field.elementKind == FieldKind::TrimmingSelect)
            {
                // Mixed SELECT SET identity is not the typed wrapper's name.
                // PARAMETER_VALUE(0.0) equals PARAMETER_VALUE(0.E0), whereas
                // two distinct parameters must reach the separate WR1/WR2 check.
                for (std::size_t previous = 0; previous < i; ++previous)
                {
                    const auto& other = value.children[previous];
                    Visit(other.span);
                    const bool equal = child.kind == other.kind &&
                                       (child.kind == ValueKind::Reference ? UnboundTarget(child) == UnboundTarget(other) :
                                                                             CompareSourceReals(child.children[0].text, other.children[0].text) == 0);
                    if (equal)
                    {
                        duplicate = other.span;
                        break;
                    }
                }
            }
            else if (unique && !members.emplace(child.text, child.span).second)
            {
                duplicate = members.at(child.text);
            }
            if (duplicate)
            {
                try
                {
                    Fail(
                        set ? "SetDuplicateMember" : "AggregateUniqueViolation",
                        "D1",
                        "Aggregate repeats an entity identity",
                        child.span,
                        Status::Rejected,
                        r,
                        c,
                        childPath,
                        "unique instance identity",
                        child.text,
                        std::string(declaredIn) + "." + std::string(field.name)
                    );
                }
                catch (Failure& failure)
                {
                    failure.diagnostic.source.related.push_back(*duplicate);
                    throw;
                }
            }
        }
    }

    void Fields()
    {
        const auto& catalog = Schema(*m_data);
        m_data->fields.resize(m_data->source->Records().size());
        for (std::size_t r = 0; r < m_data->source->Records().size(); ++r)
        {
            const auto& record = Record(r);
            for (std::size_t c = 0; c < record.components.size(); ++c)
            {
                const auto& part = record.components[c];
                const auto type = catalog.RequireType(part.name);
                const auto count = record.complex ? catalog.declarations[type].fields.size() : catalog.Flattened(type).size();
                if (part.parameters.size() != count)
                {
                    Fail(
                        "AttributeCountMismatch",
                        "D1",
                        "Explicit attribute slot count does not match this mapping",
                        part.span,
                        Status::Rejected,
                        r,
                        c,
                        {},
                        std::to_string(count),
                        std::to_string(part.parameters.size()),
                        SourceAuthority(r) + " attribute layout"
                    );
                }
                for (std::size_t p = 0; p < count; ++p)
                {
                    const auto view = AttributeAt(*m_data, r, c, p);
                    const auto& value = *view.value;
                    const auto binding = record.complex ? FieldBinding{ type, p } : catalog.Flattened(type)[p];
                    const auto& field = catalog.EffectiveField(m_data->types[r], binding);
                    const std::vector<std::size_t> path{ p + 1 };
                    Visit(value.span);
                    auto& bound = m_data->fields[r];
                    const auto index = bound.attributes.size();
                    bound.attributes.push_back(
                        { view,
                          c,
                          p,
                          field.kind == FieldKind::Aggregate && field.elementKind == FieldKind::Reference && field.nestedAggregates.empty() }
                    );
                    bound.named.emplace(view.name, index);
                    if (view.derivedSlot)
                    {
                        if (value.kind != ValueKind::Derived)
                        {
                            try
                            {
                                Fail(
                                    "DerivedSlotMarkerRequired",
                                    "D1",
                                    "Redeclared explicit slot must contain '*'",
                                    value.span,
                                    Status::Rejected,
                                    r,
                                    c,
                                    path,
                                    "*",
                                    std::string(m_data->source->Raw(value.span)),
                                    "explicit-to-derived redeclaration"
                                );
                            }
                            catch (Failure& failure)
                            {
                                // AP214 derived slots still require '*'. Mass metadata
                                // retains its existing nonphysical proof. Separately
                                // requested SI/conversion-length interpretation must
                                // prove every supplied exponent against its derivation
                                // and pass closure; neither path rewrites the source.
                                const bool interpretedUnit = m_data->requestedUnitInterpretation ==
                                                                 UnitInterpretation::VerifiedExplicitUnitDimensions &&
                                                             m_data->geometryScope == GeometryScope::GeneralBrep &&
                                                             (IsA(*m_data, r, "LENGTH_UNIT") || IsA(*m_data, r, "SI_UNIT"));
                                if (!Interoperability() || !IsAp214SourceProfile(m_data->profile.kind) || field.name != "dimensions" ||
                                    value.kind != ValueKind::Reference ||
                                    (!IsA(*m_data, r, "SI_UNIT") && !IsA(*m_data, r, "CONVERSION_BASED_UNIT")) ||
                                    (!IsA(*m_data, r, "MASS_UNIT") && !interpretedUnit) || (IsA(*m_data, r, "SI_UNIT") && !interpretedUnit))
                                {
                                    throw;
                                }
                                Scalar(value, FieldKind::Reference, field, r, c, path);
                                Preserve(
                                    std::move(failure),
                                    interpretedUnit ? SemanticData::PreservationProof::VerifiedExplicitUnitDimensions :
                                                      SemanticData::PreservationProof::ExistingMetadata
                                );
                            }
                        }
                        continue;
                    }
                    if (value.kind == ValueKind::Unset)
                    {
                        if (field.optional)
                        {
                            continue;
                        }
                        try
                        {
                            Fail(
                                "RequiredAttributeMissing",
                                "D1",
                                "A required attribute is missing; empty string is a distinct value",
                                value.span,
                                Status::Rejected,
                                r,
                                c,
                                path,
                                std::string(field.type),
                                "$",
                                SourceAuthority(r) + " required attribute"
                            );
                        }
                        catch (Failure& failure)
                        {
                            const bool label = field.name == "name" && field.kind == FieldKind::String &&
                                               (IsA(*m_data, r, "REPRESENTATION_ITEM") || IsA(*m_data, r, "REPRESENTATION") ||
                                                (NonGeometricStyle(r) && !IsA(*m_data, r, "PRE_DEFINED_ITEM")));
                            const bool rendering = field.name == "rendering_method" && field.kind == FieldKind::Enumeration &&
                                                   IsA(*m_data, r, "SURFACE_STYLE_RENDERING");
                            // Preserve only the original missing label or rendering
                            // method. The latter still needs a selected-presentation
                            // and nonphysical-use proof before publication. No enum,
                            // axis, reference, unit or other value is invented.
                            if (!Interoperability() || (!label && !rendering))
                            {
                                throw;
                            }
                            Preserve(std::move(failure));
                            continue;
                        }
                    }
                    if (field.kind != FieldKind::Aggregate)
                    {
                        try
                        {
                            Scalar(value, field.kind, field, r, c, path);
                        }
                        catch (Failure& failure)
                        {
                            // A malformed display width can be retained as an
                            // uninterpreted STRING, never parsed into a measure.
                            // This is provisional: selected-presentation and
                            // complete physical-exclusion proofs must finish.
                            if (!Interoperability() || !IsAp214SourceProfile(m_data->profile.kind) ||
                                m_data->geometryScope != GeometryScope::GeneralBrep || !IsA(*m_data, r, "CURVE_STYLE") ||
                                field.name != "curve_width" || field.kind != FieldKind::SizeSelect || value.kind != ValueKind::String ||
                                failure.diagnostic.source.key != "AttributeTypeMismatch")
                            {
                                throw;
                            }
                            Preserve(std::move(failure), SemanticData::PreservationProof::CurveStyleWidth);
                        }
                        continue;
                    }
                    AggregateValue(value, field, r, c, path, view.declaredIn);
                }
            }
        }
        m_data->bindingsComplete = true;
    }

    const std::vector<std::size_t>& Population(std::string_view name) const
    {
        const auto type = Schema(*m_data).RequireType(name);
        if (m_scopedPopulations)
        {
            const auto found = m_scopedPopulations->find(type);
            if (found != m_scopedPopulations->end())
            {
                return found->second;
            }
        }
        return m_data->populations[type];
    }
    std::size_t One(std::string_view type)
    {
        const auto& members = Population(type);
        if (members.size() != 1)
        {
            const auto span = members.empty() ? m_data->source->Header()[2].span : Record(members.back()).idSpan;
            const auto owner = members.empty() ? std::nullopt : std::optional<std::size_t>(members.back());
            std::string ids;
            for (auto r : members)
            {
                ids += "#" + Record(r).id + " ";
            }
            try
            {
                Fail(
                    "UnsupportedSinglePartGraph",
                    "D2",
                    "The selected profile requires a unique product/representation chain",
                    span,
                    Status::Unsupported,
                    owner,
                    0,
                    {},
                    std::string(type) + " count=1",
                    std::to_string(members.size()) + " " + ids,
                    "single-part profile guard; not PRODUCT.id uniqueness"
                );
            }
            catch (Failure& failure)
            {
                if (members.size() > 1)
                {
                    failure.diagnostic.source.related.push_back(Record(members[0]).idSpan);
                }
                throw;
            }
        }
        return members[0];
    }
    void Link(std::size_t owner, std::string_view name, std::size_t target, std::optional<std::size_t> member = {})
    {
        const auto& value = FieldValue(owner, name);
        const auto& field = member ? value.children.at(*member) : value;
        if (Target(field) != target)
        {
            FieldFailure(
                "UnsupportedSinglePartGraph",
                "D2",
                "Product chain roles do not refer to the same source definition",
                field.span,
                Status::Unsupported,
                owner,
                name,
                FieldDetail::SpanOnly,
                Record(target).id,
                field.text,
                "single-part identity guard"
            );
        }
    }
    struct SourceRepresentations
    {
        std::size_t definition;
        std::size_t body;
        std::optional<std::size_t> relationship;
        std::optional<std::size_t> wireframe;
        std::optional<std::size_t> wireframeRelationship;
        std::optional<std::size_t> construction;
        std::optional<std::size_t> constructionRelationship;
    };
    void SelectAp214Construction(SourceRepresentations& selected)
    {
        if (!IsAp214SourceProfile(m_data->profile.kind))
        {
            return;
        }
        const auto& representations = Population("CONSTRUCTIVE_GEOMETRY_REPRESENTATION");
        const auto& relationships = Population("CONSTRUCTIVE_GEOMETRY_REPRESENTATION_RELATIONSHIP");
        if (representations.empty() && relationships.empty())
        {
            return;
        }
        const auto unsupported = [&](std::size_t owner, const char* message)
        {
            Fail(
                "UnsupportedConstructionGeometryGraph",
                "D2",
                message,
                Record(owner).span,
                Status::Unsupported,
                owner,
                0,
                {},
                {},
                {},
                "project single selected supporting representation guard"
            );
        };
        if (m_data->geometryScope != GeometryScope::GeneralBrep || representations.size() != 1 || relationships.size() != 1)
        {
            unsupported(selected.definition, "General source scope requires exactly one supporting representation and directed construction link");
        }
        const auto representation = representations[0], relationship = relationships[0];
        const auto& firstValue = FieldValue(relationship, "rep_1");
        const auto& secondValue = FieldValue(relationship, "rep_2");
        const auto first = Target(firstValue), second = Target(secondValue);
        const auto firstContext = Target(FieldValue(first, "context_of_items"));
        const auto secondContext = Target(FieldValue(second, "context_of_items"));
        Visit(Record(relationship).span);
        // Fixed AP214 CGRR.WR1-WR3, in source order. Unlike ordinary SRR,
        // endpoints are directed: reversing the source link is not equivalent.
        if (firstContext != secondContext || !IsA(*m_data, firstContext, "GEOMETRIC_REPRESENTATION_CONTEXT"))
        {
            FieldFailure(
                "SourceRuleViolation:CONSTRUCTIVE_GEOMETRY_REPRESENTATION_RELATIONSHIP.WR1",
                "D3",
                "Construction relationship requires the identical geometric context",
                secondValue.span,
                Status::Rejected,
                relationship,
                "rep_2",
                FieldDetail::AttributePath,
                "same GRC identity",
                secondValue.text,
                "AP214E3_2010.exp CONSTRUCTIVE_GEOMETRY_REPRESENTATION_RELATIONSHIP.WR1"
            );
        }
        if (!IsA(*m_data, second, "CONSTRUCTIVE_GEOMETRY_REPRESENTATION"))
        {
            FieldFailure(
                "SourceRuleViolation:CONSTRUCTIVE_GEOMETRY_REPRESENTATION_RELATIONSHIP.WR2",
                "D3",
                "rep_2 must be a constructive geometry representation",
                secondValue.span,
                Status::Rejected,
                relationship,
                "rep_2",
                FieldDetail::AttributePath,
                "CGR",
                secondValue.text,
                "AP214E3_2010.exp CONSTRUCTIVE_GEOMETRY_REPRESENTATION_RELATIONSHIP.WR2"
            );
        }
        const auto firstCategories = std::size_t(IsA(*m_data, first, "SHAPE_REPRESENTATION")) +
                                     std::size_t(IsA(*m_data, first, "CONSTRUCTIVE_GEOMETRY_REPRESENTATION"));
        if (firstCategories != 1)
        {
            FieldFailure(
                "SourceRuleViolation:CONSTRUCTIVE_GEOMETRY_REPRESENTATION_RELATIONSHIP.WR3",
                "D3",
                "rep_1 must have exactly one shape/construction category",
                firstValue.span,
                Status::Rejected,
                relationship,
                "rep_1",
                FieldDetail::AttributePath,
                "one SHAPE_REPRESENTATION/CGR category",
                firstValue.text,
                "AP214E3_2010.exp CONSTRUCTIVE_GEOMETRY_REPRESENTATION_RELATIONSHIP.WR3"
            );
        }
        // Complete mappings still exclude transformed CGRR (only transformed shape
        // relationships are registered). REPRESENTATION_MAP remains unregistered,
        // excluding CGR.WR4's mapped use; this is not a general transform/map check.
        if (second != representation || (first != selected.definition && first != selected.body && selected.wireframe != first))
        {
            unsupported(relationship, "Supporting geometry must link directly from an already selected shape; construction chains are not selected");
        }
        const auto context = Target(FieldValue(representation, "context_of_items"));
        const auto& dimension = FieldValue(context, "coordinate_space_dimension");
        Visit(dimension.span);
        if (CompareSourceReals(dimension.text, "2") < 0 || CompareSourceReals(dimension.text, "3") > 0)
        {
            FieldFailure(
                "SourceRuleViolation:CONSTRUCTIVE_GEOMETRY_REPRESENTATION.WR1",
                "D3",
                "Supporting representation context dimension must be 2 or 3",
                dimension.span,
                Status::Rejected,
                context,
                "coordinate_space_dimension",
                FieldDetail::AttributePath,
                "2 <= dimension <= 3",
                dimension.text,
                "AP214E3_2010.exp CONSTRUCTIVE_GEOMETRY_REPRESENTATION.WR1"
            );
        }
        const auto& items = FieldValue(representation, "items");
        constexpr std::string_view categories[] = { "PLACEMENT", "CURVE", "EDGE", "FACE", "POINT", "SURFACE", "FACE_SURFACE", "VERTEX_POINT" };
        for (const auto& item : items.children)
        {
            Visit(item.span);
            const auto target = Target(item);
            std::size_t count = 0;
            for (const auto category : categories)
            {
                count += IsA(*m_data, target, category) ? 1 : 0;
            }
            if (count != 1)
            {
                FieldFailure(
                    "SourceRuleViolation:CONSTRUCTIVE_GEOMETRY_REPRESENTATION.WR2",
                    "D3",
                    "Supporting item must have exactly one fixed-source geometry category",
                    item.span,
                    Status::Rejected,
                    representation,
                    "items",
                    FieldDetail::AttributePath,
                    "one source category",
                    item.text,
                    "AP214E3_2010.exp CONSTRUCTIVE_GEOMETRY_REPRESENTATION.WR2"
                );
            }
        }
        // The selected, actually typed CGRR.rep_2 reference above supplies
        // CGR.WR3's required incoming RR use; no synthetic inverse is added.
        if (context != Target(FieldValue(selected.body, "context_of_items")))
        {
            unsupported(
                representation,
                "Supporting geometry must share the selected physical unit/context identity; no relation transform is inferred"
            );
        }
        selected.construction = representation;
        selected.constructionRelationship = relationship;
    }
    SourceRepresentations SelectAp214Representations(std::size_t sdr)
    {
        const auto root = Target(FieldValue(sdr, "used_representation"));
        SourceRepresentations selected{ root, root, {}, {}, {}, {}, {} };
        const auto isBody = [&](std::size_t record)
        {
            return IsA(*m_data, record, "ADVANCED_BREP_SHAPE_REPRESENTATION") || IsA(*m_data, record, "MANIFOLD_SURFACE_SHAPE_REPRESENTATION");
        };
        const auto unsupported = [&](std::size_t owner, const char* message)
        {
            Fail(
                "UnsupportedRepresentationGraph",
                "D2",
                message,
                Record(owner).span,
                Status::Unsupported,
                owner,
                0,
                {},
                {},
                {},
                "profile shape/presentation graph guard"
            );
        };
        const auto& relationships = Population("SHAPE_REPRESENTATION_RELATIONSHIP");
        const auto& wireframes = Population("GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION");
        if (!wireframes.empty())
        {
            // Ordinary SRR expresses association, not definition containment,
            // equivalence, Boolean composition or display priority. This closed
            // project selection retains one distinct linked wireframe alongside
            // the unique BRep; it never merges or demotes its physical geometry.
            if (m_data->geometryScope != GeometryScope::GeneralBrep || wireframes.size() != 1 || wireframes[0] == root ||
                !IsA(*m_data, root, "SHAPE_REPRESENTATION") || relationships.size() != (isBody(root) ? 1 : 2))
            {
                unsupported(root, "The linked wireframe requires the explicit general source scope and one unambiguous root/BRep/wireframe graph");
            }
            selected.wireframe = wireframes[0];
            for (const auto relationship : relationships)
            {
                Visit(Record(relationship).span);
                const auto a = Target(FieldValue(relationship, "rep_1")), b = Target(FieldValue(relationship, "rep_2"));
                if ((a == root) == (b == root))
                {
                    unsupported(relationship, "Each linked shape relationship must join the selected root to one distinct representation");
                }
                const auto other = a == root ? b : a;
                if (other == *selected.wireframe && !selected.wireframeRelationship)
                {
                    selected.wireframeRelationship = relationship;
                }
                else if (isBody(other) && !isBody(root) && !selected.relationship)
                {
                    selected.body = other;
                    selected.relationship = relationship;
                }
                else
                {
                    unsupported(relationship, "Duplicate, ambiguous or additional linked geometry is outside the selected source graph");
                }
            }
            if (!selected.wireframeRelationship || !isBody(selected.body))
            {
                unsupported(root, "The selected root must identify both the BRep and its separate linked wireframe");
            }
            const auto context = Target(FieldValue(root, "context_of_items"));
            if (Target(FieldValue(selected.body, "context_of_items")) != context ||
                Target(FieldValue(*selected.wireframe, "context_of_items")) != context)
            {
                unsupported(root, "Linked geometry requires the identical source context; no transform or scale is inferred from a relationship");
            }
            if (root != selected.body)
            {
                for (const auto& value : FieldValue(root, "items").children)
                {
                    Visit(value.span);
                    if (!IsA(*m_data, Target(value), "AXIS2_PLACEMENT_3D"))
                    {
                        unsupported(root, "The source wrapper may contain only retained placement items");
                    }
                }
            }
        }
        else if (isBody(root))
        {
            if (!relationships.empty())
            {
                unsupported(relationships[0], "A direct body with additional shape relationships is outside this subset");
            }
        }
        else
        {
            if (!IsA(*m_data, root, "SHAPE_REPRESENTATION") || relationships.size() != 1)
            {
                unsupported(root, "The source root requires one unambiguous shape relationship to its body");
            }
            const auto relationship = relationships[0];
            Visit(Record(relationship).span);
            const auto first = Target(FieldValue(relationship, "rep_1")), second = Target(FieldValue(relationship, "rep_2"));
            if (first == root && second != root)
            {
                selected.body = second;
            }
            else if (second == root && first != root)
            {
                selected.body = first;
            }
            else
            {
                unsupported(relationship, "The shape relationship does not join the selected root to a distinct body");
            }
            if (!isBody(selected.body))
            {
                unsupported(relationship, "The linked representation is not an admitted exact BRep body");
            }
            // AP214E3_2010.exp, SHAPE_REPRESENTATION_RELATIONSHIP inherits rep_1/rep_2
            // without a transformation operator. Preserve both directions and identities;
            // this project subset requires the same context instead of guessing a mapping.
            if (Target(FieldValue(root, "context_of_items")) != Target(FieldValue(selected.body, "context_of_items")))
            {
                unsupported(relationship, "Linked shape representations require the identical source context in this subset");
            }
            for (const auto& value : FieldValue(root, "items").children)
            {
                Visit(value.span);
                if (!IsA(*m_data, Target(value), "AXIS2_PLACEMENT_3D"))
                {
                    unsupported(root, "The source wrapper may contain only retained placement items");
                }
            }
            selected.relationship = relationship;
        }
        SelectAp214Construction(selected);
        for (const auto representation : Population("REPRESENTATION"))
        {
            Visit(Record(representation).span);
            // A plain representation is only a metadata candidate here. Closure
            // grants ownership only after proving its property link and part.
            // A definitional representation is likewise only a candidate: it
            // has its own parameter context, is not a body/unit root, and must
            // remain in the complete selected geometry's forward closure.
            if (representation != selected.definition && representation != selected.body && selected.wireframe != representation &&
                selected.construction != representation && !IsA(*m_data, representation, "MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION") &&
                !IsA(*m_data, representation, "DEFINITIONAL_REPRESENTATION") && !IsPlainRepresentation(representation))
            {
                unsupported(representation, "Additional representation is outside the selected root/body/presentation graph");
            }
        }
        return selected;
    }
    bool IsPlainRepresentation(std::size_t record) const
    {
        return Record(record).components.size() == 1 && Record(record).components.front().name == "REPRESENTATION";
    }
    void Product(bool placementOnly = false)
    {
        std::optional<std::size_t> singleApplication;
        std::optional<Failure> distinctApplications;
        try
        {
            singleApplication = One("APPLICATION_CONTEXT");
        }
        catch (Failure& failure)
        {
            if (!Interoperability() || m_data->geometryScope != GeometryScope::GeneralBrep || Population("APPLICATION_CONTEXT").size() < 2)
            {
                throw;
            }
            distinctApplications = std::move(failure); // Approval waits for the actual selected reference graph.
        }
        std::optional<std::size_t> apd;
        std::optional<std::size_t> definitionProtocol;
        try
        {
            apd = One("APPLICATION_PROTOCOL_DEFINITION");
        }
        catch (Failure& failure)
        {
            if (Interoperability() && m_data->geometryScope == GeometryScope::GeneralBrep && Population("APPLICATION_PROTOCOL_DEFINITION").size() > 1)
            {
                // Select by the existing PC/PDC application references, never
                // by APD text, year or array order. Two metadata applications
                // need not share an APD; each must have exactly one association.
                const auto productContext = One("PRODUCT_CONTEXT");
                const auto definitionContext = One("PRODUCT_DEFINITION_CONTEXT");
                const auto productApplication = Target(FieldValue(productContext, "frame_of_reference"));
                const auto definitionApplication = Target(FieldValue(definitionContext, "frame_of_reference"));
                for (const auto protocol : Population("APPLICATION_PROTOCOL_DEFINITION"))
                {
                    const auto application = Target(FieldValue(protocol, "application"));
                    if (application == productApplication)
                    {
                        if (apd)
                        {
                            throw std::move(failure);
                        }
                        apd = protocol;
                    }
                    if (application == definitionApplication)
                    {
                        if (definitionProtocol)
                        {
                            throw std::move(failure);
                        }
                        definitionProtocol = protocol;
                    }
                    if (application != productApplication && application != definitionApplication)
                    {
                        throw std::move(failure);
                    }
                }
                if (!apd || !definitionProtocol)
                {
                    throw std::move(failure);
                }
                Preserve(std::move(failure), SemanticData::PreservationProof::ContextApplicationProtocols);
            }
            else
            {
                // Binding already selected this source implementation. An
                // absent administrative APD is not a replacement geometry root.
                // Keep the exact strict guard diagnostic; do not invent its year,
                // status or schema claim from HEADER/application text.
                if (!CanPreserveMissingApplicationProtocol() || !Interoperability() || m_data->geometryScope != GeometryScope::GeneralBrep ||
                    !Population("APPLICATION_PROTOCOL_DEFINITION").empty())
                {
                    throw;
                }
                m_data->part.applicationProtocolPresent = false;
                Preserve(std::move(failure), SemanticData::PreservationProof::MissingApplicationProtocol);
            }
        }
        const auto pc = One("PRODUCT_CONTEXT");
        const auto pdc = One("PRODUCT_DEFINITION_CONTEXT"), product = One("PRODUCT"), formation = One("PRODUCT_DEFINITION_FORMATION");
        const auto pd = One("PRODUCT_DEFINITION"), pds = One("PRODUCT_DEFINITION_SHAPE"), sdr = One("SHAPE_DEFINITION_REPRESENTATION");
        const auto productApplication = distinctApplications ? Target(FieldValue(pc, "frame_of_reference")) : *singleApplication;
        const auto definitionApplication = distinctApplications ? Target(FieldValue(pdc, "frame_of_reference")) : *singleApplication;
        const auto protocolApplication = distinctApplications && apd ? Target(FieldValue(*apd, "application")) : productApplication;
        const auto ac = singleApplication.value_or(protocolApplication);
        if (distinctApplications)
        {
            // Application contexts are source identities, not physical unit
            // contexts. Do not merge them by application text or select an
            // arbitrary first instance. Every admitted context must actually
            // occur on this unique PC/PDC/APD chain.
            const std::array selected{ productApplication, definitionApplication, protocolApplication };
            for (const auto context : Population("APPLICATION_CONTEXT"))
            {
                if (std::find(selected.begin(), selected.end(), context) == selected.end())
                {
                    throw std::move(*distinctApplications);
                }
            }
            Preserve(std::move(*distinctApplications), SemanticData::PreservationProof::DistinctApplicationContexts);
        }
        std::size_t definitionRepresentation = 0;
        std::optional<std::size_t> shapeRelationship;
        std::optional<std::size_t> wireframeRelationship;
        std::optional<std::size_t> constructionRelationship;
        if (placementOnly)
        {
            // Only the explicit assembly scope planner can establish this role.
            // Still run all product-chain/administrative checks below. A wrapper
            // is not an empty material solid and cannot hide arbitrary items.
            Expects(m_assemblyDefinitionChecks);
            m_representation = Target(FieldValue(sdr, "used_representation"));
            definitionRepresentation = m_representation;
            m_context = Target(FieldValue(m_representation, "context_of_items"));
            if (Population("REPRESENTATION").size() != 1 || !Population("SHAPE_REPRESENTATION_RELATIONSHIP").empty())
            {
                Fail(
                    "AssemblyWrapperRepresentationUnsupported",
                    "D2",
                    "Placement-only wrapper requires one direct representation",
                    Record(m_representation).span,
                    Status::Unsupported,
                    m_representation
                );
            }
        }
        else if (m_data->profile.kind != ProfileKind::Ap203e2Planar)
        {
            // Source SDR selects the root; presentation order and representation counts
            // cannot select the body's geometry or override its unit context.
            const auto selected = SelectAp214Representations(sdr);
            definitionRepresentation = selected.definition;
            m_representation = selected.body;
            shapeRelationship = selected.relationship;
            m_wireframeRepresentation = selected.wireframe;
            wireframeRelationship = selected.wireframeRelationship;
            m_constructionRepresentation = selected.construction;
            constructionRelationship = selected.constructionRelationship;
            m_context = Target(FieldValue(m_representation, "context_of_items"));
        }
        else
        {
            m_representation = One("REPRESENTATION");
            m_context = One("REPRESENTATION_CONTEXT");
            definitionRepresentation = m_representation;
        }
        m_data->part.roles.reserve(11);
        m_data->part.roles = { { "application_context", ac },
                               { "product_context", pc },
                               { "definition_context", pdc },
                               { "product", product },
                               { "formation", formation },
                               { "definition", pd },
                               { "product_shape", pds },
                               { "shape_definition_representation", sdr },
                               { "representation", m_representation },
                               { "unit_context", m_context } };
        if (distinctApplications)
        {
            m_data->part.roles.push_back({ "product_application_context", productApplication });
            m_data->part.roles.push_back({ "definition_application_context", definitionApplication });
        }
        if (apd)
        {
            m_data->part.roles.insert(m_data->part.roles.begin() + 1, { "application_protocol", *apd });
        }
        if (definitionProtocol && definitionProtocol != apd)
        {
            m_data->part.roles.push_back({ "definition_application_protocol", *definitionProtocol });
        }
        if (shapeRelationship)
        {
            m_data->part.roles.push_back({ "definition_representation", definitionRepresentation });
            m_data->part.roles.push_back({ "shape_representation_relationship", *shapeRelationship });
        }
        if (m_wireframeRepresentation)
        {
            Expects(wireframeRelationship.has_value());
            m_data->part.roles.push_back({ "wireframe_representation", *m_wireframeRepresentation });
            m_data->part.roles.push_back({ "wireframe_shape_relationship", *wireframeRelationship });
        }
        if (m_constructionRepresentation)
        {
            Expects(constructionRelationship.has_value());
            m_data->part.roles.push_back({ "construction_representation", *m_constructionRepresentation });
            m_data->part.roles.push_back({ "construction_relationship", *constructionRelationship });
        }
        if (apd)
        {
            Link(*apd, "application", distinctApplications ? protocolApplication : ac);
        }
        Link(pc, "frame_of_reference", distinctApplications ? productApplication : ac);
        Link(pdc, "frame_of_reference", distinctApplications ? definitionApplication : ac);
        if (FieldValue(product, "frame_of_reference").children.size() != 1)
        {
            FieldFailure(
                "UnsupportedSinglePartGraph",
                "D2",
                "Only one product context is supported",
                FieldValue(product, "frame_of_reference").span,
                Status::Unsupported,
                product,
                "frame_of_reference",
                FieldDetail::SpanOnly
            );
        }
        Link(product, "frame_of_reference", pc, 0);
        Link(formation, "of_product", product);
        Link(pd, "formation", formation);
        Link(pd, "frame_of_reference", pdc);
        Link(pds, "definition", pd);
        Link(sdr, "definition", pds);
        Link(sdr, "used_representation", definitionRepresentation);
        Link(m_representation, "context_of_items", m_context);
        if (apd)
        {
            m_data->part.apdStatus = FieldValue(*apd, "status").text;
            m_data->part.apdSchemaClaim = FieldValue(*apd, "application_interpreted_model_schema_name").text;
            m_data->part.apdYear = FieldValue(*apd, "application_protocol_year").text;
        }
        if (IsA(*m_data, pc, "MECHANICAL_CONTEXT") && FieldValue(pc, "discipline_type").text != "mechanical")
        {
            FieldFailure(
                "SourceRuleViolation:MECHANICAL_CONTEXT.WR1",
                "D3",
                "Legacy mechanical context discipline is invalid",
                FieldValue(pc, "discipline_type").span,
                Status::Rejected,
                pc,
                "discipline_type",
                FieldDetail::AttributePath,
                "mechanical",
                FieldValue(pc, "discipline_type").text,
                "MECHANICAL_CONTEXT.WR1"
            );
        }
        if (IsA(*m_data, pdc, "DESIGN_CONTEXT") && FieldValue(pdc, "life_cycle_stage").text != "design")
        {
            FieldFailure(
                "SourceRuleViolation:DESIGN_CONTEXT.WR1",
                "D3",
                "Legacy design context lifecycle is invalid",
                FieldValue(pdc, "life_cycle_stage").span,
                Status::Rejected,
                pdc,
                "life_cycle_stage",
                FieldDetail::AttributePath,
                "design",
                FieldValue(pdc, "life_cycle_stage").text,
                "DESIGN_CONTEXT.WR1"
            );
        }
        if (FieldValue(pc, "discipline_type").text != "mechanical")
        {
            FieldFailure(
                "UnsupportedProductContextDiscipline",
                "D2",
                "Product discipline is outside this profile",
                FieldValue(pc, "discipline_type").span,
                Status::Unsupported,
                pc,
                "discipline_type",
                FieldDetail::AttributePath,
                "mechanical",
                FieldValue(pc, "discipline_type").text,
                "profile role guard"
            );
        }
        if (FieldValue(pdc, "name").text != "part definition")
        {
            try
            {
                FieldFailure(
                    "UnsupportedDefinitionContextRole",
                    "D2",
                    "Definition context is outside the single-part profile",
                    FieldValue(pdc, "name").span,
                    Status::Unsupported,
                    pdc,
                    "name",
                    FieldDetail::AttributePath,
                    "part definition",
                    FieldValue(pdc, "name").text,
                    "profile role guard"
                );
            }
            catch (Failure& failure)
            {
                // The unique selected PD->PDC link was already checked above.
                // Its administrative label cannot choose or transform geometry.
                if (!Interoperability())
                {
                    throw;
                }
                Preserve(std::move(failure));
            }
        }
        if (FieldValue(pdc, "life_cycle_stage").text != "design")
        {
            FieldFailure(
                "UnsupportedDefinitionLifecycleStage",
                "D2",
                "Definition lifecycle is outside this profile",
                FieldValue(pdc, "life_cycle_stage").span,
                Status::Unsupported,
                pdc,
                "life_cycle_stage",
                FieldDetail::AttributePath,
                "design",
                FieldValue(pdc, "life_cycle_stage").text,
                "profile role guard"
            );
        }
        const std::unordered_set<std::string> specialNames{ "visual appearance", "tactile appearance", "contact ratio",
                                                            "hardness",          "treatment result",   "surface texture" };
        if (specialNames.count(FieldValue(pds, "name").text))
        {
            FieldFailure(
                "UnsupportedShapePurpose",
                "D2",
                "Special surface-condition purpose is not supported",
                FieldValue(pds, "name").span,
                Status::Unsupported,
                pds,
                "name",
                FieldDetail::AttributePath
            );
        }
        // AP214E3_2010.exp restrict_centre_of_mass_representation and
        // restrict_treatment_result use exact ASCII literals. Opaque text is
        // never an empty decoded name: its pending proof must establish explicit
        // non-ASCII interpretation and selected ownership before publication.
        const auto& representationName = FieldValue(m_representation, "name");
        if (representationName.kind == ValueKind::String &&
            (representationName.text == "centre of mass" || representationName.text == "treatment result"))
        {
            try
            {
                FieldFailure(
                    "UnsupportedRepresentationPurpose",
                    "D2",
                    "Special representation purpose is not supported",
                    FieldValue(m_representation, "name").span,
                    Status::Unsupported,
                    m_representation,
                    "name",
                    FieldDetail::AttributePath
                );
            }
            catch (Failure& failure)
            {
                // Preserve the existing strict profile diagnostic. The fixed
                // modified N916 artifact (020b4d25...) REPRESENTATION has only
                // a label/items/context declaration; unlike AP214's named
                // purpose rules, it does not constrain items by this label.
                if (!Interoperability() || m_data->geometryScope != GeometryScope::GeneralBrep ||
                    m_data->profile.kind != ProfileKind::Ap203N916Source)
                {
                    throw;
                }
                Preserve(std::move(failure), SemanticData::PreservationProof::N916RepresentationCaption);
            }
        }
        const auto& categories = Population("PRODUCT_RELATED_PRODUCT_CATEGORY");
        if (categories.size() > 1)
        {
            Fail(
                "UnsupportedProductCategory",
                "D2",
                "Multiple categories are outside this profile",
                Record(categories[1]).span,
                Status::Unsupported,
                categories[1]
            );
        }
        if (!categories.empty())
        {
            const auto category = categories[0];
            const auto& products = FieldValue(category, "products");
            const bool membership = m_scopedPopulations && (m_independentRequest || m_assemblyDefinitionChecks) ?
                                        !products.children.empty() &&
                                            std::all_of(
                                                products.children.begin(),
                                                products.children.end(),
                                                [&](const auto& member)
                                                {
                                                    return m_requestedProducts.contains(Target(member));
                                                }
                                            ) &&
                                            std::any_of(
                                                products.children.begin(),
                                                products.children.end(),
                                                [&](const auto& member)
                                                {
                                                    return Target(member) == product;
                                                }
                                            ) :
                                        products.children.size() == 1 && Target(products.children[0]) == product;
            if (!membership || FieldValue(category, "name").text != "part")
            {
                try
                {
                    FieldFailure(
                        "UnsupportedProductCategory",
                        "D2",
                        "Category name/products are outside the single-part guard",
                        FieldValue(category, "name").span,
                        Status::Unsupported,
                        category,
                        "name",
                        FieldDetail::AttributePath,
                        "part for this product",
                        FieldValue(category, "name").text,
                        "profile category guard"
                    );
                }
                catch (Failure& failure)
                {
                    // An arbitrary source category label is not geometry, but
                    // membership must still stay within the complete selected product set.
                    if (!Interoperability() || !membership)
                    {
                        throw;
                    }
                    Preserve(std::move(failure));
                }
            }
            m_data->part.roles.push_back({ "category", category });
        }
        if (placementOnly)
        {
            for (const auto& item : FieldValue(m_representation, "items").children)
            {
                if (!IsA(*m_data, Target(item), "AXIS2_PLACEMENT_3D"))
                {
                    Fail(
                        "AssemblyWrapperItemUnsupported",
                        "D2",
                        "Wrapper items must retain explicit placement roles",
                        item.span,
                        Status::Unsupported,
                        m_representation
                    );
                }
            }
            m_data->part.shapeRole = "AssemblyPlacementWrapper";
            return;
        }
        std::vector<std::size_t> shapes;
        for (const auto& item : FieldValue(m_representation, "items").children)
        {
            const auto target = Target(item);
            if (IsA(*m_data, target, "AXIS2_PLACEMENT_3D"))
            {
                continue;
            }
            const bool solid = IsA(*m_data, m_representation, "ADVANCED_BREP_SHAPE_REPRESENTATION");
            if (!IsA(*m_data, target, solid ? "MANIFOLD_SOLID_BREP" : "SHELL_BASED_SURFACE_MODEL"))
            {
                FieldFailure(
                    "UnsupportedRepresentationItem",
                    "D2",
                    "Only the declared shape and auxiliary placements are supported",
                    item.span,
                    Status::Unsupported,
                    m_representation,
                    "items",
                    FieldDetail::AttributePath,
                    "one MSB/SBSM",
                    Record(target).components[0].name,
                    "profile shape guard"
                );
            }
            shapes.push_back(target);
        }
        // General source checking distinguishes one product definition from
        // one solid. AP214/N2635/modified N916 ABSR.items may retain several
        // MSBs. This is a typed source inventory, not a Boolean union or native
        // construction qualification. The frozen planar profile remains singular.
        const bool materialCollection = m_data->geometryScope == GeometryScope::GeneralBrep &&
                                        IsA(*m_data, m_representation, "ADVANCED_BREP_SHAPE_REPRESENTATION");
        if (shapes.empty() || (shapes.size() != 1 && !materialCollection))
        {
            FieldFailure(
                "UnsupportedShapeMultiplicity",
                "D2",
                "Exactly one shape item is required",
                FieldValue(m_representation, "items").span,
                Status::Unsupported,
                m_representation,
                "items",
                FieldDetail::SpanOnly
            );
        }
        for (const auto shape : shapes)
        {
            std::size_t shell = 0;
            std::string shapeRole;
            if (IsA(*m_data, shape, "MANIFOLD_SOLID_BREP"))
            {
                shell = Target(FieldValue(shape, "outer"));
                shapeRole = "MaterialSolid";
            }
            else
            {
                if (FieldValue(shape, "sbsm_boundary").children.size() != 1)
                {
                    FieldFailure(
                        "UnsupportedShapeMultiplicity",
                        "D2",
                        "One source shell is required",
                        FieldValue(shape, "sbsm_boundary").span,
                        Status::Unsupported,
                        shape,
                        "sbsm_boundary",
                        FieldDetail::SpanOnly
                    );
                }
                shell = Target(FieldValue(shape, "sbsm_boundary").children[0]);
                shapeRole = IsA(*m_data, shell, "CLOSED_SHELL") ? "ClosedSurfaceModel" : "OpenSurfaceModel";
            }
            m_data->part.shapes.push_back({ shape, shell, std::move(shapeRole) });
        }
        if (m_data->part.shapes.size() == 1)
        {
            const auto& item = m_data->part.shapes.front();
            m_data->part.roles.push_back({ "shape", item.shape });
            m_data->part.roles.push_back({ "shell", item.shell });
            m_data->part.shapeRole = item.role;
        }
        else
        {
            // No singular shape role is invented. In particular, JSON role maps
            // must not contain duplicate keys that silently keep the last body.
            m_data->part.shapeRole = "MaterialSolidCollection";
        }
        for (const auto face : Population("FACE"))
        {
            // Preserve the existing field visit and diagnostics/repair policy;
            // assembly scope checking shares only the nominal source predicate.
            (void)FieldValue(face, "bounds");
            const auto outer = SourceOuterBoundCount(SourceBindings(m_data), face);
            if (outer != 1)
            {
                if (outer > 1 && IsAp214SourceProfile(m_data->profile.kind) && m_data->geometryScope == GeometryScope::GeneralBrep)
                {
                    // AP214E3_2010.exp FACE.WR2 permits at most one outer
                    // marker. This is a defining region-role contradiction,
                    // not optional metadata or a missing native capability.
                    // Preserve both source bounds; never guess which to relabel.
                    try
                    {
                        FieldFailure(
                            "SourceRuleViolation:FACE.WR2",
                            "D3",
                            "FACE has more than one FACE_OUTER_BOUND",
                            FieldValue(face, "bounds").span,
                            Status::Rejected,
                            face,
                            "bounds",
                            FieldDetail::AttributePath,
                            "at most one FACE_OUTER_BOUND",
                            std::to_string(outer),
                            "AP214E3_2010.exp FACE.WR2"
                        );
                    }
                    catch (const Failure& failure)
                    {
                        if (!DeferUnselectedFaceClause(face, failure) && !DeferCylindricalBandRepair(face, failure))
                        {
                            throw;
                        }
                        continue;
                    }
                }
                if (outer == 0 && m_data->profile.kind != ProfileKind::Ap203e2Planar && m_data->geometryScope == GeometryScope::GeneralBrep)
                {
                    // Ordinary FACE_BOUND is legal source data. Do not guess its
                    // role or silently change the source type: general native
                    // construction must classify all regions before publication.
                    m_data->part.deferredRegionFaces.push_back(face);
                    continue;
                }
                FieldFailure(
                    "UnsupportedExplicitOuterBoundRequired",
                    "D2",
                    "This profile requires one explicit FACE_OUTER_BOUND",
                    FieldValue(face, "bounds").span,
                    Status::Unsupported,
                    face,
                    "bounds",
                    FieldDetail::AttributePath,
                    "one explicit outer bound",
                    std::to_string(outer),
                    "profile face-region guard"
                );
            }
        }
    }

    std::vector<unsigned char> Reach(std::vector<std::size_t> pending, bool geometryOnly)
    {
        const auto composite = geometryOnly ? Schema(*m_data).Find("COMPOSITE_CURVE") : std::optional<std::size_t>{};
        const auto segment = geometryOnly ? Schema(*m_data).Find("COMPOSITE_CURVE_SEGMENT") : std::optional<std::size_t>{};
        std::vector<unsigned char> visited(m_data->types.size(), 0);
        while (!pending.empty())
        {
            const auto r = pending.back();
            pending.pop_back();
            if (visited[r])
            {
                continue;
            }
            visited[r] = 1;
            Visit(Record(r).span);
            for (auto index : m_outgoing[r])
            {
                const auto& reference = m_data->source->References()[index];
                Visit(reference.span);
                // The ordinary segment is FOUNDED_ITEM, not RI. Its only
                // admitted incoming definition edge is CC.segments, and its
                // typed parent is bounded geometry. Follow that bridge without
                // treating arbitrary founded styles/metadata as geometry roots.
                // The public UsedByRepresentation query still filters to RIs.
                const bool segmentUse = composite && segment && m_data->types[r].test(*composite) &&
                                        m_data->types[reference.targetRecordIndex].test(*segment);
                if (!geometryOnly || IsA(*m_data, reference.targetRecordIndex, "REPRESENTATION_ITEM") || segmentUse)
                {
                    pending.push_back(reference.targetRecordIndex);
                }
            }
        }
        return visited;
    }
    void AttachAp214Properties()
    {
        // Product() already proved each selected PD/PDS identity link.
        // AP214E3_2010.exp PDR/PD fields supply source references; allowing these
        // metadata roots is a project attachment guard, not full EXPRESS WHERE.
        const auto& definitions = Population("PRODUCT_DEFINITION");
        const auto& shapes = Population("PRODUCT_DEFINITION_SHAPE");
        Expects(!definitions.empty() && !shapes.empty());
        if (m_data->independentParts.empty())
        {
            Expects(definitions.size() == 1 && shapes.size() == 1);
        }
        std::vector<Part::PropertyAttachment> attached;
        const auto reject = [&](std::size_t owner,
                                std::string_view field,
                                const Value& at,
                                const char* key,
                                const char* expected,
                                std::optional<std::size_t> item = {})
        {
            const auto& bound = Binding(owner, field);
            std::vector<std::size_t> path{ bound.parameter + 1 };
            if (item)
            {
                path.push_back(*item);
            }
            Fail(
                key,
                "D2",
                "Property metadata is outside the selected-part attachment subset",
                at.span,
                Status::Unsupported,
                owner,
                bound.component,
                std::move(path),
                expected,
                at.text,
                "profile property attachment guard; not complete EXPRESS WHERE"
            );
        };
        for (const auto link : Population("PROPERTY_DEFINITION_REPRESENTATION"))
        {
            if (IsA(*m_data, link, "SHAPE_DEFINITION_REPRESENTATION"))
            {
                continue; // The SDR is the existing geometry root, not a metadata link.
            }
            ++m_outcome.propertyAttachmentChecks;
            Visit(Record(link).span);
            const auto& definition = FieldValue(link, "definition");
            Visit(definition.span);
            const auto property = Target(definition);
            const auto& characterized = FieldValue(property, "definition");
            Visit(characterized.span);
            const auto owner = Target(characterized);
            std::optional<std::size_t> selectedDefinition;
            for (const auto& role : m_data->part.roles)
            {
                if (role.name == "definition" && role.recordIndex == owner)
                {
                    selectedDefinition = owner;
                }
                if (role.name == "product_shape" && role.recordIndex == owner)
                {
                    selectedDefinition = Target(FieldValue(owner, "definition"));
                }
            }
            if (!selectedDefinition)
            {
                reject(property, "definition", characterized, "UnsupportedPropertyAttachment", "selected PD or PDS identity");
            }
            const auto& used = FieldValue(link, "used_representation");
            Visit(used.span);
            const auto representation = Target(used);
            if (!IsPlainRepresentation(representation))
            {
                reject(link, "used_representation", used, "UnsupportedPropertyRepresentation", "plain property REPRESENTATION");
            }
            const auto& items = FieldValue(representation, "items").children;
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                const auto& value = items[i];
                Visit(value.span);
                const auto target = Target(value);
                // Restrict the entire metadata item boundary, not just its name.
                // MRI can reference only units; DRI has text. Neither can smuggle
                // geometry in through a style, placement or another BRep shape.
                const bool valueItem = IsA(*m_data, target, "VALUE_REPRESENTATION_ITEM");
                const bool foreignReal = Interoperability() && m_data->geometryScope == GeometryScope::GeneralBrep && BorrowedForeignReal(target);
                if (!IsA(*m_data, target, "MEASURE_REPRESENTATION_ITEM") && !IsA(*m_data, target, "DESCRIPTIVE_REPRESENTATION_ITEM") && !valueItem &&
                    !foreignReal)
                {
                    reject(representation, "items", value, "UnsupportedPropertyItem", "measure or descriptive item", i + 1);
                }
                if (valueItem && m_data->profile.kind == ProfileKind::Ap203e2GeneralSource &&
                    !IsA(*m_data, Target(FieldValue(representation, "context_of_items")), "GLOBAL_UNIT_ASSIGNED_CONTEXT"))
                {
                    FieldFailure(
                        "SourceRuleViolation:VALUE_REPRESENTATION_ITEM.WR1",
                        "D3",
                        "Value items require an assigned unit context",
                        FieldValue(representation, "context_of_items").span,
                        Status::Rejected,
                        representation,
                        "context_of_items",
                        FieldDetail::AttributePath,
                        "GLOBAL_UNIT_ASSIGNED_CONTEXT",
                        "context without unit assignment",
                        "N2635 VALUE_REPRESENTATION_ITEM.WR1"
                    );
                }
            }
            const auto& context = FieldValue(representation, "context_of_items");
            Visit(context.span);
            attached.push_back({ link, property, owner, *selectedDefinition, representation, Target(context) });
        }
        // Grant roots only after every link has proven its owner and item boundary.
        // Preserve every source link, shared representation/item and original context;
        // property units never replace m_context or the body's selected unit scale.
        for (std::size_t i = 0; i < attached.size(); ++i)
        {
            m_data->part.roles.push_back({ "property_link_" + std::to_string(i), attached[i].link });
        }
        m_data->part.propertyAttachments = std::move(attached);
    }
    void AttachGeneralPropertyAssociations()
    {
        if (m_data->profile.kind != ProfileKind::Ap214MbxSource)
        {
            return;
        }
        const auto& associations = Population("GENERAL_PROPERTY_ASSOCIATION");
        if (associations.empty())
        {
            return;
        }
        std::vector<std::size_t> roots;
        for (const auto& role : m_data->part.roles)
        {
            roots.push_back(role.recordIndex);
        }
        const auto owned = Reach(std::move(roots), false);
        std::unordered_set<std::size_t> attachedProperties;
        for (const auto& attachment : m_data->part.propertyAttachments)
        {
            Visit(Record(attachment.propertyDefinition).span);
            attachedProperties.insert(attachment.propertyDefinition);
        }
        std::unordered_map<std::size_t, std::size_t> counts;
        for (const auto record : associations)
        {
            Visit(Record(record).span);
            ++counts[Target(FieldValue(record, "derived_definition"))];
        }
        for (const auto record : associations)
        {
            const auto& derived = FieldValue(record, "derived_definition");
            const auto target = Target(derived);
            const auto base = Target(FieldValue(record, "base_definition"));
            Visit(derived.span);
            // Evaluate the two actual source WHERE predicates independently of
            // attachment. A duplicate target is not repaired by keeping one link.
            if (counts.at(target) != 1)
            {
                FieldFailure(
                    "SourceRuleViolation:GENERAL_PROPERTY_ASSOCIATION.WR1",
                    "D3",
                    "Derived property must have one general-property association",
                    derived.span,
                    Status::Rejected,
                    record,
                    "derived_definition",
                    FieldDetail::AttributePath,
                    "one association",
                    std::to_string(counts.at(target)),
                    "AP214E3_2010.exp GENERAL_PROPERTY_ASSOCIATION.WR1"
                );
            }
            if (FieldValue(base, "name").text != FieldValue(target, "name").text)
            {
                FieldFailure(
                    "SourceRuleViolation:GENERAL_PROPERTY_ASSOCIATION.WR2",
                    "D3",
                    "General and derived property names differ",
                    derived.span,
                    Status::Rejected,
                    record,
                    "derived_definition",
                    FieldDetail::AttributePath,
                    FieldValue(base, "name").text,
                    FieldValue(target, "name").text,
                    "AP214E3_2010.exp GENERAL_PROPERTY_ASSOCIATION.WR2"
                );
            }
            // The association is inverse metadata. Only a property already
            // attached by a proven PDR may receive it; do not make an orphan
            // PROPERTY_DEFINITION a part root through this link.
            if (!owned[target] || !attachedProperties.contains(target))
            {
                FieldFailure(
                    "UnsupportedGeneralPropertyAttachment",
                    "D2",
                    "General property target is not an attached selected-part property",
                    derived.span,
                    Status::Unsupported,
                    record,
                    "derived_definition",
                    FieldDetail::AttributePath
                );
            }
        }
        // Grant roots only after every source rule and ownership check passes.
        for (const auto record : associations)
        {
            m_data->part.roles.push_back({ "general_property_association_" + std::to_string(record), record });
        }
    }
    void CheckStyleOverrideCycles()
    {
        if (m_data->profile.kind != ProfileKind::Ap214MbxSource)
        {
            return;
        }
        const auto& overrides = Population("OVER_RIDING_STYLED_ITEM");
        if (overrides.empty())
        {
            return;
        }
        // Check the complete admitted population, including detached metadata.
        // A presentation root is an ownership fact, not permission to ignore a
        // cycle elsewhere. Iterative colours keep long shared chains linear.
        std::vector<unsigned char> colour(m_data->types.size());
        for (const auto start : overrides)
        {
            std::vector<std::size_t> path;
            auto current = start;
            while (IsA(*m_data, current, "OVER_RIDING_STYLED_ITEM") && colour[current] != 2)
            {
                colour[current] = 1;
                path.push_back(current);
                const auto& link = FieldValue(current, "over_ridden_style");
                Visit(link.span);
                const auto next = Target(link);
                if (colour[next] == 1)
                {
                    FieldFailure(
                        "UnsupportedPresentationOverrideCycle",
                        "D2",
                        "Cyclic style override is outside the presentation subset",
                        link.span,
                        Status::Unsupported,
                        current,
                        "over_ridden_style",
                        FieldDetail::SpanOnly
                    );
                }
                current = next;
            }
            for (const auto record : path)
            {
                colour[record] = 2;
            }
        }
    }
    void AttachAp214Presentations()
    {
        auto items = PhysicalItems();
        const auto selectedGeometryUse = Reach(std::move(items), true);
        const auto selectedStyleTarget = [&](const Value& value)
        {
            const auto target = Target(value);
            // Interoperability does not reinterpret a representation as an RI.
            // This exact selected root only supplies a metadata attachment; its
            // geometry, units and complete body graph were checked separately.
            return selectedGeometryUse[target] || (Interoperability() && m_data->geometryScope == GeometryScope::GeneralBrep &&
                                                   IsAp214SourceProfile(m_data->profile.kind) && SelectedBodyRepresentation(target));
        };
        const auto& presentations = Population("MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION");
        for (std::size_t i = 0; i < presentations.size(); ++i)
        {
            const auto presentation = presentations[i];
            for (const auto& value : FieldValue(presentation, "items").children)
            {
                Visit(value.span);
                const auto styled = Target(value);
                if (!IsA(*m_data, styled, "STYLED_ITEM"))
                {
                    FieldFailure(
                        "UnsupportedPresentationAttachment",
                        "D2",
                        "This presentation subset requires styled items",
                        value.span,
                        Status::Unsupported,
                        presentation,
                        "items",
                        FieldDetail::AttributePath
                    );
                }
                const auto& target = FieldValue(styled, "item");
                // This is a project attachment guard, not MDGPR WHERE evaluation. Prove
                // the style refers to the selected BRep/wireframe RI closure before granting it
                // ownership. A style/management path cannot smuggle in another shape.
                if (!selectedStyleTarget(target))
                {
                    FieldFailure(
                        "UnsupportedPresentationAttachment",
                        "D2",
                        "Styled target is not used by the selected body",
                        target.span,
                        Status::Unsupported,
                        styled,
                        "item",
                        FieldDetail::AttributePath,
                        "RI used by selected body",
                        target.text,
                        "profile presentation attachment guard"
                    );
                }
                if (IsA(*m_data, styled, "OVER_RIDING_STYLED_ITEM"))
                {
                    // Every ancestor style must target the same selected geometry
                    // closure. Following the override reference without this
                    // proof could otherwise grant ownership to foreign geometry.
                    auto current = styled;
                    while (IsA(*m_data, current, "OVER_RIDING_STYLED_ITEM"))
                    {
                        const auto owner = current;
                        const auto& ancestor = FieldValue(current, "over_ridden_style");
                        Visit(ancestor.span);
                        current = Target(ancestor);
                        if (!IsA(*m_data, current, "STYLED_ITEM"))
                        {
                            // Some source files put the selected CLOSED_SHELL in
                            // the style-only override slot. Keep the original type
                            // error; do not reinterpret the shell as an ancestor
                            // style or change either geometric reference.
                            if (m_data->profile.kind == ProfileKind::Ap214MbxSource && Interoperability() && IsA(*m_data, current, "CLOSED_SHELL") &&
                                selectedGeometryUse[current])
                            {
                                m_preservedStyleOverrides.insert(owner);
                                break;
                            }
                            FieldFailure(
                                "UnsupportedPresentationAttachment",
                                "D2",
                                "Invalid override target is outside the selected body",
                                ancestor.span,
                                Status::Unsupported,
                                owner,
                                "over_ridden_style",
                                FieldDetail::AttributePath
                            );
                        }
                        const auto& ancestorItem = FieldValue(current, "item");
                        Visit(ancestorItem.span);
                        if (!selectedStyleTarget(ancestorItem))
                        {
                            FieldFailure(
                                "UnsupportedPresentationAttachment",
                                "D2",
                                "Overridden style targets geometry outside the selected body",
                                ancestorItem.span,
                                Status::Unsupported,
                                current,
                                "item",
                                FieldDetail::AttributePath
                            );
                        }
                    }
                }
            }
            // Retain the presentation's own context and complete style chain through
            // source references. Its context never replaces the body's unit context.
            m_data->part.roles.push_back({ "presentation_" + std::to_string(i), presentation });
            m_presentationRoots.push_back(presentation);
        }
    }
    void AttachAdministrativeMetadata()
    {
        // N2635 CC assignments / AP214E3_2010.exp applied date assignments are
        // inverse administrative associations, not geometry roots. Admit
        // a root only when ALL of its target items are already owned by this
        // selected part. Fixed-point expansion handles approval/security links
        // without allowing an administrative cycle to invent its own ownership.
        std::vector<std::size_t> candidates;
        for (const auto name : { "CC_DESIGN_APPROVAL",
                                 "CC_DESIGN_SECURITY_CLASSIFICATION",
                                 "CC_DESIGN_PERSON_AND_ORGANIZATION_ASSIGNMENT",
                                 "CC_DESIGN_DATE_AND_TIME_ASSIGNMENT",
                                 "APPROVAL_PERSON_ORGANIZATION",
                                 "APPROVAL_DATE_TIME",
                                 "PRESENTATION_LAYER_ASSIGNMENT",
                                 "APPLIED_DATE_AND_TIME_ASSIGNMENT" })
        {
            if (!Schema(*m_data).Find(name))
            {
                continue; // Only declarations in this protocol's catalog participate.
            }
            for (const auto r : Population(name))
            {
                candidates.push_back(r);
            }
        }
        if (IsAp214SourceProfile(m_data->profile.kind) && candidates.empty())
        {
            return; // Existing AP214 files pay no visits for an absent new feature.
        }
        m_administrativeAttachments.assign(m_data->types.size(), 0);
        bool changed = true;
        while (changed)
        {
            changed = false;
            std::vector<std::size_t> roots;
            for (const auto& role : m_data->part.roles)
            {
                roots.push_back(role.recordIndex);
            }
            const auto owned = Reach(std::move(roots), false);
            for (const auto record : candidates)
            {
                if (m_administrativeAttachments[record])
                {
                    continue;
                }
                Visit(Record(record).span);
                const char* field = IsA(*m_data, record, "APPROVAL_PERSON_ORGANIZATION")  ? "authorized_approval" :
                                    IsA(*m_data, record, "APPROVAL_DATE_TIME")            ? "dated_approval" :
                                    IsA(*m_data, record, "PRESENTATION_LAYER_ASSIGNMENT") ? "assigned_items" :
                                                                                            "items";
                const auto& value = FieldValue(record, field);
                bool attached = true;
                if (value.kind == ValueKind::Aggregate)
                {
                    for (const auto& item : value.children)
                    {
                        Visit(item.span);
                        attached = attached && owned[Target(item)];
                    }
                }
                else
                {
                    Visit(value.span);
                    attached = owned[Target(value)];
                }
                if (!attached)
                {
                    continue;
                }
                m_administrativeAttachments[record] = 1;
                m_data->part.roles.push_back({ "administrative_attachment_" + std::to_string(record), record });
                changed = true;
            }
        }
        for (const auto record : candidates)
        {
            if (!m_administrativeAttachments[record])
            {
                Fail(
                    "UnsupportedAdministrativeAttachment",
                    "D2",
                    "Administrative targets are outside the selected part closure",
                    Record(record).span,
                    Status::Unsupported,
                    record
                );
            }
        }
    }

    void CheckAp214PredefinedColourValues()
    {
        // AP214E3_2010.exp (71ab140f...), DRAUGHTING_PRE_DEFINED_COLOUR.WR1.
        // The decoded STRING is a case-sensitive palette discriminator, not a
        // display label. Typing has already required the inherited LABEL; no
        // missing/opaque-name exception, RGB conversion or default is allowed.
        constexpr std::string_view names[] = { "red", "green", "blue", "yellow", "magenta", "cyan", "black", "white" };
        for (const auto colour : Population("DRAUGHTING_PRE_DEFINED_COLOUR"))
        {
            const auto& name = FieldValue(colour, "name");
            Visit(name.span);
            if (std::find(std::begin(names), std::end(names), name.text) == std::end(names))
            {
                FieldFailure(
                    "SourceRuleViolation:DRAUGHTING_PRE_DEFINED_COLOUR.WR1",
                    "D3",
                    "Unknown predefined draughting-colour name",
                    name.span,
                    Status::Rejected,
                    colour,
                    "name",
                    FieldDetail::AttributePath,
                    "eight exact draughting colour names",
                    name.text,
                    "AP214E3_2010.exp DRAUGHTING_PRE_DEFINED_COLOUR.WR1"
                );
            }
        }
    }

    void CheckStyleSize(std::size_t style, std::string_view field, const char* message)
    {
        const auto& width = FieldValue(style, field);
        Visit(width.span);
        if (width.kind == ValueKind::Typed && width.text == "POSITIVE_LENGTH_MEASURE" && CompareSourceReals(width.children[0].text, "0.") <= 0)
        {
            FieldFailure(
                "SourceRuleViolation:POSITIVE_LENGTH_MEASURE.WR1",
                "D3",
                message,
                width.span,
                Status::Rejected,
                style,
                field,
                FieldDetail::AttributePath,
                "value > 0",
                width.children[0].text,
                "AP214E3_2010.exp POSITIVE_LENGTH_MEASURE.WR1"
            );
        }
    }
    void CheckStyleColour(std::size_t style, std::string_view colourField)
    {
        const auto colour = Target(FieldValue(style, colourField));
        if (IsA(*m_data, colour, "COLOUR_RGB"))
        {
            constexpr std::pair<std::string_view, std::string_view> channels[] = { { "red", "COLOUR_RGB.WR1" },
                                                                                   { "green", "COLOUR_RGB.WR2" },
                                                                                   { "blue", "COLOUR_RGB.WR3" } };
            for (const auto& [field, clause] : channels)
            {
                const auto& value = FieldValue(colour, field);
                Visit(value.span);
                if (CompareSourceReals(value.text, "0.") < 0 || CompareSourceReals(value.text, "1.") > 0)
                {
                    FieldFailure(
                        "SourceRuleViolation:" + std::string(clause),
                        "D3",
                        "Source colour channel is outside [0,1]",
                        value.span,
                        Status::Rejected,
                        colour,
                        field,
                        FieldDetail::AttributePath,
                        "0 <= value <= 1",
                        value.text,
                        "AP214E3_2010.exp " + std::string(clause)
                    );
                }
            }
        }
    }
    std::size_t CountStyleType(const Value& values, std::string_view type)
    {
        std::size_t count = 0;
        for (const auto& value : values.children)
        {
            Visit(value.span);
            count += IsA(*m_data, Target(value), type);
        }
        return count;
    }
    void CheckAp214CurveStyleValues()
    {
        // These source checks never turn an appearance dependency into a body
        // root. Names are case-sensitive STRING values, not enum spellings.
        constexpr std::string_view names[] = { "continuous", "chain", "chain double dash", "dashed", "dotted" };
        for (const auto font : Population("DRAUGHTING_PRE_DEFINED_CURVE_FONT"))
        {
            const auto& name = FieldValue(font, "name");
            Visit(name.span);
            if (std::find(std::begin(names), std::end(names), name.text) == std::end(names))
            {
                FieldFailure(
                    "SourceRuleViolation:DRAUGHTING_PRE_DEFINED_CURVE_FONT.WR1",
                    "D3",
                    "Unknown predefined curve-font name",
                    name.span,
                    Status::Rejected,
                    font,
                    "name",
                    FieldDetail::AttributePath,
                    "five exact draughting font names",
                    name.text,
                    "AP214E3_2010.exp DRAUGHTING_PRE_DEFINED_CURVE_FONT.WR1"
                );
            }
        }
        const auto& styles = Population("CURVE_STYLE");
        for (const auto style : styles)
        {
            CheckStyleSize(style, "curve_width", "Source width is not strictly positive");
            // A provisional invalid STRING width remains uninterpreted here.
            // Its typed pending proof is checked only after closure/attachment.
            CheckStyleColour(style, "curve_colour");
        }
        std::unordered_set<std::size_t> curveAssignments;
        // No curve style means no matching assignment. Skip the assignment scan,
        // but STILL check styled curves below: zero matches violates the rule.
        if (!styles.empty())
        {
            for (const auto assignment : Population("PRESENTATION_STYLE_ASSIGNMENT"))
            {
                const auto& values = FieldValue(assignment, "styles");
                const auto curves = CountStyleType(values, "CURVE_STYLE");
                if (curves > 1)
                {
                    // Every admitted CURVE_STYLE has the same TYPEOF set. PSA.WR1
                    // permits repeated surface usage but not two curve styles.
                    FieldFailure(
                        "SourceRuleViolation:PRESENTATION_STYLE_ASSIGNMENT.WR1",
                        "D3",
                        "Assignment repeats the curve-style type",
                        values.span,
                        Status::Rejected,
                        assignment,
                        "styles",
                        FieldDetail::AttributePath,
                        "at most one CURVE_STYLE",
                        std::to_string(curves),
                        "AP214E3_2010.exp PRESENTATION_STYLE_ASSIGNMENT.WR1"
                    );
                }
                if (curves)
                {
                    curveAssignments.insert(assignment);
                }
            }
        }
        for (const auto item : Population("STYLED_ITEM"))
        {
            if (!IsA(*m_data, Target(FieldValue(item, "item")), "CURVE"))
            {
                continue;
            }
            std::size_t assignments = 0;
            const auto& values = FieldValue(item, "styles");
            for (const auto& value : values.children)
            {
                Visit(value.span);
                assignments += curveAssignments.contains(Target(value));
            }
            if (assignments != 1)
            {
                // Count assignments CONTAINING a curve style, not references to
                // the style itself, as required by styled_curve.WR1.
                FieldFailure(
                    "SourceRuleViolation:styled_curve.WR1",
                    "D3",
                    "Styled curve does not have exactly one curve-style assignment",
                    values.span,
                    Status::Rejected,
                    item,
                    "styles",
                    FieldDetail::AttributePath,
                    "one curve-style assignment",
                    std::to_string(assignments),
                    "AP214E3_2010.exp styled_curve.WR1"
                );
            }
        }
    }
    void CheckAp214PointStyleValues()
    {
        const auto& styles = Population("POINT_STYLE");
        const auto& markers = Population("PRE_DEFINED_POINT_MARKER_SYMBOL");
        if (styles.empty() && markers.empty())
        {
            return;
        }
        constexpr std::string_view names[] = { "asterisk", "circle", "dot", "plus", "square", "triangle", "x" };
        for (const auto marker : markers)
        {
            const auto& name = FieldValue(marker, "name");
            Visit(name.span);
            if (std::find(std::begin(names), std::end(names), name.text) == std::end(names))
            {
                FieldFailure(
                    "SourceRuleViolation:PRE_DEFINED_POINT_MARKER_SYMBOL.WR1",
                    "D3",
                    "Unknown predefined point marker name",
                    name.span,
                    Status::Rejected,
                    marker,
                    "name",
                    FieldDetail::AttributePath,
                    "seven exact predefined point marker names",
                    name.text,
                    "AP214E3_2010.exp PRE_DEFINED_POINT_MARKER_SYMBOL.WR1"
                );
            }
        }
        for (const auto style : styles)
        {
            CheckStyleSize(style, "marker_size", "Source marker size is not strictly positive");
            CheckStyleColour(style, "marker_colour");
        }
        for (const auto assignment : Population("PRESENTATION_STYLE_ASSIGNMENT"))
        {
            const auto& values = FieldValue(assignment, "styles");
            const auto count = CountStyleType(values, "POINT_STYLE");
            if (count > 1)
            {
                FieldFailure(
                    "SourceRuleViolation:PRESENTATION_STYLE_ASSIGNMENT.WR1",
                    "D3",
                    "Presentation assignment repeats the point-style type",
                    values.span,
                    Status::Rejected,
                    assignment,
                    "styles",
                    FieldDetail::AttributePath,
                    "at most one POINT_STYLE",
                    std::to_string(count),
                    "AP214E3_2010.exp PRESENTATION_STYLE_ASSIGNMENT.WR1"
                );
            }
        }
        // This extra restriction belongs to MDGPR.WR6, not to POINT_STYLE or
        // SIZE_SELECT in every source use. Generic styles may retain descriptive
        // or explicit-MWU sizes; this geometric-presentation use requires the
        // typed positive-length branch and its admitted colour family. Charge
        // every traversed use, including non-point assignments: shared styled
        // items can otherwise multiply this work without consuming the budget.
        for (const auto representation : Population("MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION"))
        {
            for (const auto& value : FieldValue(representation, "items").children)
            {
                Visit(value.span);
                const auto styled = Target(value);
                if (!IsA(*m_data, styled, "STYLED_ITEM"))
                {
                    continue;
                }
                for (const auto& use : FieldValue(styled, "styles").children)
                {
                    Visit(use.span);
                    for (const auto& entry : FieldValue(Target(use), "styles").children)
                    {
                        Visit(entry.span);
                        const auto style = Target(entry);
                        if (!IsA(*m_data, style, "POINT_STYLE"))
                        {
                            continue;
                        }
                        const auto& size = FieldValue(style, "marker_size");
                        Visit(size.span);
                        const auto colour = Target(FieldValue(style, "marker_colour"));
                        if (size.kind != ValueKind::Typed || size.text != "POSITIVE_LENGTH_MEASURE" ||
                            (!IsA(*m_data, colour, "COLOUR_RGB") && !IsA(*m_data, colour, "DRAUGHTING_PRE_DEFINED_COLOUR")))
                        {
                            FieldFailure(
                                "SourceRuleViolation:MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION.WR6",
                                "D3",
                                "Point style in geometric presentation requires positive-length size and admitted colour",
                                size.span,
                                Status::Rejected,
                                style,
                                "marker_size",
                                FieldDetail::AttributePath,
                                "POSITIVE_LENGTH_MEASURE and RGB/draughting colour",
                                size.text,
                                "AP214E3_2010.exp MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION.WR6"
                            );
                        }
                    }
                }
            }
        }
    }
    void CheckAp214RenderingValues()
    {
        // AP214E3_2010.exp source predicates. Compare exact source REALs, not
        // floating approximations. These checks neither interpret a display
        // method nor grant a geometry/presentation ownership root.
        for (const auto record : Population("SURFACE_STYLE_TRANSPARENT"))
        {
            const auto& value = FieldValue(record, "transparency");
            Visit(value.span);
            if (CompareSourceReals(value.text, "0.") < 0 || CompareSourceReals(value.text, "1.") > 0)
            {
                FieldFailure(
                    "SourceRuleViolation:SURFACE_STYLE_TRANSPARENT.WR1",
                    "D3",
                    "Transparency is outside the declared source range",
                    value.span,
                    Status::Rejected,
                    record,
                    "transparency",
                    FieldDetail::AttributePath,
                    "0 <= value <= 1",
                    value.text,
                    "AP214E3_2010.exp SURFACE_STYLE_TRANSPARENT.WR1"
                );
            }
        }
        for (const auto record : Population("SURFACE_STYLE_RENDERING_WITH_PROPERTIES"))
        {
            const auto& properties = FieldValue(record, "properties");
            Visit(properties.span);
            // The SET cardinality/type checks have already run. WR1 additionally
            // forbids two separate records of the same TYPEOF set. Identical
            // value content is not a substitute for this nominal rule.
            if (properties.children.size() == 2 && m_data->types[Target(properties.children[0])] == m_data->types[Target(properties.children[1])])
            {
                FieldFailure(
                    "SourceRuleViolation:SURFACE_STYLE_RENDERING_WITH_PROPERTIES.WR1",
                    "D3",
                    "Rendering properties have the same source type",
                    properties.span,
                    Status::Rejected,
                    record,
                    "properties",
                    FieldDetail::AttributePath,
                    "distinct TYPEOF sets",
                    "same TYPEOF set",
                    "AP214E3_2010.exp SURFACE_STYLE_RENDERING_WITH_PROPERTIES.WR1"
                );
            }
        }
    }

    void CheckAp214CategoryHierarchy()
    {
        const auto& relationships = Population("PRODUCT_CATEGORY_RELATIONSHIP");
        if (relationships.empty())
        {
            return;
        }
        // Fixed AP214E3_2010.exp PRODUCT_CATEGORY_RELATIONSHIP.WR1 calls
        // acyclic_product_category_relationship. Iterative three-colour DFS
        // proves the same absence of directed category cycles, including self
        // links, without source-depth recursion or exponential shared-DAG walks.
        std::unordered_map<std::size_t, std::vector<std::size_t>> outgoing;
        for (const auto relation : relationships)
        {
            Visit(Record(relation).span);
            outgoing[Target(FieldValue(relation, "category"))].push_back(relation);
        }
        struct Frame
        {
            std::size_t category, next = 0;
        };
        std::vector<unsigned char> colour(m_data->types.size());
        std::vector<Frame> stack;
        for (const auto root : Population("PRODUCT_CATEGORY"))
        {
            Visit(Record(root).span);
            if (colour[root])
            {
                continue;
            }
            colour[root] = 1;
            stack.push_back({ root });
            while (!stack.empty())
            {
                auto& frame = stack.back();
                const auto found = outgoing.find(frame.category);
                if (found == outgoing.end() || frame.next == found->second.size())
                {
                    colour[frame.category] = 2;
                    stack.pop_back();
                    continue;
                }
                const auto relation = found->second[frame.next++];
                const auto& subcategory = FieldValue(relation, "sub_category");
                Visit(subcategory.span);
                const auto child = Target(subcategory);
                if (colour[child] == 1)
                {
                    FieldFailure(
                        "SourceRuleViolation:PRODUCT_CATEGORY_RELATIONSHIP.WR1",
                        "D3",
                        "Product category hierarchy contains a directed cycle",
                        subcategory.span,
                        Status::Rejected,
                        relation,
                        "sub_category",
                        FieldDetail::AttributePath,
                        "acyclic category references",
                        "cycle",
                        "AP214E3_2010.exp PRODUCT_CATEGORY_RELATIONSHIP.WR1"
                    );
                }
                if (!colour[child])
                {
                    colour[child] = 1;
                    stack.push_back({ child });
                }
            }
        }
    }
    void AttachAp214Invisibility()
    {
        const auto& instances = Population("INVISIBILITY");
        if (instances.empty())
        {
            return;
        }
        // AP214 INVISIBILITY affects presentation only. Prove every target was
        // already owned before these records acquire roles; an invisibility
        // reference cannot introduce or suppress required geometry/units.
        std::vector<std::size_t> roots;
        for (const auto& role : m_data->part.roles)
        {
            roots.push_back(role.recordIndex);
        }
        const auto owned = Reach(std::move(roots), false);
        for (const auto record : instances)
        {
            const auto& items = FieldValue(record, "invisible_items");
            for (const auto& item : items.children)
            {
                Visit(item.span);
                if (!owned[Target(item)])
                {
                    FieldFailure(
                        "UnsupportedInvisibilityAttachment",
                        "D2",
                        "Invisible target is outside the previously established selected model/presentation closure",
                        item.span,
                        Status::Unsupported,
                        record,
                        "invisible_items",
                        FieldDetail::AttributePath,
                        "previously owned presentation target",
                        item.text,
                        "project invisibility attachment guard"
                    );
                }
            }
            m_data->part.roles.push_back({ "invisibility_" + std::to_string(record), record });
        }
    }
    void CheckAp214ValueItemContexts()
    {
        if (!IsAp214SourceProfile(m_data->profile.kind))
        {
            return;
        }
        const auto& values = Population("VALUE_REPRESENTATION_ITEM");
        if (values.empty())
        {
            return;
        }
        // AP214E3_2010.exp VALUE_REPRESENTATION_ITEM.WR1 uses ALL using_representations,
        // including reverse uses through RI/founded-item references. Propagate
        // bad-context representation uses forward over exactly those item
        // dependencies once, instead of repeatedly traversing a shared DAG for
        // every value. Cyclic item uses terminate; traversal never grants ownership.
        std::vector<std::optional<std::size_t>> invalidUse(m_data->types.size());
        std::vector<std::size_t> pending;
        const auto add = [&](std::size_t item, std::size_t representation)
        {
            if (!invalidUse[item])
            {
                invalidUse[item] = representation;
                pending.push_back(item);
            }
        };
        for (const auto representation : Population("REPRESENTATION"))
        {
            const auto& context = FieldValue(representation, "context_of_items");
            Visit(context.span);
            if (IsA(*m_data, Target(context), "GLOBAL_UNIT_ASSIGNED_CONTEXT"))
            {
                continue;
            }
            for (const auto& item : FieldValue(representation, "items").children)
            {
                Visit(item.span);
                add(Target(item), representation);
            }
        }
        for (std::size_t at = 0; at < pending.size(); ++at)
        {
            const auto item = pending[at];
            Visit(Record(item).span);
            for (const auto edge : m_outgoing[item])
            {
                const auto& reference = m_data->source->References()[edge];
                Visit(reference.span);
                const auto child = reference.targetRecordIndex;
                if (IsA(*m_data, child, "REPRESENTATION_ITEM") || IsA(*m_data, child, "FOUNDED_ITEM"))
                {
                    add(child, *invalidUse[item]);
                }
            }
        }
        for (const auto value : values)
        {
            Visit(Record(value).span);
            if (!invalidUse[value])
            {
                continue;
            }
            const auto representation = *invalidUse[value];
            const auto& context = FieldValue(representation, "context_of_items");
            try
            {
                FieldFailure(
                    "SourceRuleViolation:VALUE_REPRESENTATION_ITEM.WR1",
                    "D3",
                    "Every using representation of a value item requires assigned units",
                    context.span,
                    Status::Rejected,
                    representation,
                    "context_of_items",
                    FieldDetail::AttributePath,
                    "GLOBAL_UNIT_ASSIGNED_CONTEXT",
                    "context without unit assignment",
                    "AP214E3_2010.exp VALUE_REPRESENTATION_ITEM.WR1"
                );
            }
            catch (Failure& failure)
            {
                failure.diagnostic.source.related.push_back(Record(value).idSpan);
                throw;
            }
        }
    }
    void Closure()
    {
        BuildReferenceIndex();
        const auto& references = m_data->source->References();
        if (m_data->profile.kind != ProfileKind::Ap203e2Planar)
        {
            CheckAp214ValueItemContexts();
            AttachAp214Properties();
            AttachGeneralPropertyAssociations();
            CheckStyleOverrideCycles();
            AttachAp214Presentations();
        }
        if (m_data->profile.kind != ProfileKind::Ap203e2Planar)
        {
            AttachAdministrativeMetadata();
        }
        if (IsAp214SourceProfile(m_data->profile.kind))
        {
            CheckAp214CategoryHierarchy();
            AttachAp214Invisibility();
            CheckAp214CurveStyleValues();
            CheckAp214PointStyleValues();
            CheckAp214PredefinedColourValues();
            CheckAp214RenderingValues();
        }
        std::vector<std::size_t> seeds;
        for (const auto& role : m_data->part.roles)
        {
            seeds.push_back(role.recordIndex);
        }
        const auto reached = Reach(std::move(seeds), false);
        std::vector<std::size_t> orphans;
        std::string ids;
        for (std::size_t r = 0; r < reached.size(); ++r)
        {
            if (!reached[r])
            {
                orphans.push_back(r);
                ids += "#" + Record(r).id + " ";
            }
        }
        if (!orphans.empty())
        {
            try
            {
                Fail(
                    "UnownedOrOrphanProfileContent",
                    "D2",
                    "Records are not owned by the closed single-part input",
                    Record(orphans[0]).span,
                    Status::Unsupported,
                    orphans[0],
                    0,
                    {},
                    "reachable from selected roles",
                    ids,
                    "profile ownership, not RI.WR1"
                );
            }
            catch (Failure& failure)
            {
                for (std::size_t i = 1; i < orphans.size(); ++i)
                {
                    failure.diagnostic.source.related.push_back(Record(orphans[i]).idSpan);
                }
                bool retained = false;
                if (m_placementPolicy == UnreferencedPlacementPolicy::RetainDisconnected)
                {
                    auto proof = ProveUnreferencedPlacements(
                        *m_data,
                        m_outgoing,
                        orphans,
                        failure.diagnostic,
                        [this](Span span)
                        {
                            Visit(span);
                        }
                    );
                    if (proof.failure)
                    {
                        throw Failure{ proof.failure->status, std::move(proof.failure->diagnostic) };
                    }
                    if (proof.receipt)
                    {
                        m_retainedPlacements = std::move(proof.receipt);
                        State(Phase::Part) = Progress::Running;
                        retained = true;
                    }
                }
                if (!retained)
                {
                    std::vector<unsigned char> bodyUse;
                    const auto styledItem = [&](auto r)
                    {
                        return IsA(*m_data, r, "STYLED_ITEM");
                    };
                    if (Interoperability() && std::any_of(orphans.begin(), orphans.end(), styledItem))
                    {
                        auto bodyItems = PhysicalItems(false);
                        bodyUse = Reach(std::move(bodyItems), true);
                    }
                    const auto nondefiningResidue = [&](std::size_t r)
                    {
                        Visit(Record(r).span);
                        if (styledItem(r))
                        {
                            // An absent presentation owner cannot hide another shape.
                            // The explicit target must already be used by this body.
                            const auto& target = FieldValue(r, "item");
                            Visit(target.span);
                            return !bodyUse.empty() && bodyUse[Target(target)];
                        }
                        // Standalone category dictionaries have no source references,
                        // shape role or unit effect. Keep them as unattached source
                        // metadata with the original orphan diagnostic, never fake roots.
                        if ((m_data->profile.kind == ProfileKind::Ap203e2GeneralSource || IsAp214SourceProfile(m_data->profile.kind)) &&
                            Record(r).components.size() == 1 && Record(r).components.front().name == "PRODUCT_CATEGORY")
                        {
                            return m_outgoing[r].empty();
                        }
                        if (IsAp214SourceProfile(m_data->profile.kind) && Record(r).components.size() == 1 &&
                            Record(r).components.front().name == "PRODUCT_CATEGORY_RELATIONSHIP")
                        {
                            // The checked hierarchy retains dictionary relations
                            // as unattached source metadata, not invented product
                            // membership. Its complete outgoing graph contains only
                            // already typed category entities; cycles failed above.
                            return std::all_of(
                                m_outgoing[r].begin(),
                                m_outgoing[r].end(),
                                [&](auto reference)
                                {
                                    Visit(references[reference].span);
                                    return IsA(*m_data, references[reference].targetRecordIndex, "PRODUCT_CATEGORY");
                                }
                            );
                        }
                        if (!NonGeometricStyle(r))
                        {
                            return false;
                        }
                        for (auto reference : m_outgoing[r])
                        {
                            Visit(references[reference].span);
                            if (!NonGeometricStyle(references[reference].targetRecordIndex))
                            {
                                return false;
                            }
                        }
                        return true;
                    };
                    const bool nonphysicalResidue = Interoperability() && std::all_of(orphans.begin(), orphans.end(), nondefiningResidue);
                    if (!nonphysicalResidue)
                    {
                        throw;
                    }
                    // Preserve source-only residue separately. No part role or fake
                    // geometry/presentation attachment is manufactured for these IDs.
                    m_data->unattachedNonGeometric = orphans;
                    Preserve(std::move(failure));
                }
            }
        }
        auto items = PhysicalItems(false);
        if (m_data->profile.kind != ProfileKind::Ap203e2Planar)
        {
            // RI use follows actual representation items and RI references, never the
            // non-RI style/unit/management dependencies traversed for ownership above.
            for (const auto representation : Population("REPRESENTATION"))
            {
                if (representation == m_representation)
                {
                    continue;
                }
                for (const auto& value : FieldValue(representation, "items").children)
                {
                    items.push_back(Target(value));
                }
            }
        }
        m_data->representationUse = Reach(std::move(items), true);
    }

    void ApprovePreservedMetadata()
    {
        if (m_data->pendingIssues.empty())
        {
            return;
        }
        Expects(Interoperability());
        // All property roots are now proven. A bad MRI may be retained only if
        // it is an actual metadata item and is absent from body geometry and
        // physical-unit dependencies (including conversion-factor references).
        auto physicalRoots = PhysicalRepresentations();
        if (m_data->independentParts.empty())
        {
            physicalRoots.push_back(m_context);
        }
        else
        {
            for (const auto& part : m_data->independentParts)
            {
                for (const auto& role : part.part.roles)
                {
                    if (role.name == "unit_context")
                    {
                        physicalRoots.push_back(role.recordIndex);
                    }
                }
            }
        }
        const auto physical = Reach(std::move(physicalRoots), false);
        std::unordered_set<std::size_t> assemblyIdentityRecords;
        if (m_assemblyDefinitionChecks)
        {
            // Opaque revision/view labels cannot choose between siblings. Whole
            // assembly ownership already selected definitions by source refs;
            // prove a singleton chain PER PRODUCT, not by display text. Extra
            // formations/definitions in any same-product family deny this proof.
            std::unordered_map<std::size_t, std::size_t> formationsPerProduct, definitionsPerFormation;
            for (auto formation : Population("PRODUCT_DEFINITION_FORMATION"))
            {
                ++formationsPerProduct[Target(FieldValue(formation, "of_product"))];
            }
            for (auto definition : Population("PRODUCT_DEFINITION"))
            {
                ++definitionsPerFormation[Target(FieldValue(definition, "formation"))];
            }
            for (const auto& definition : m_outcome.productStructure->Definitions())
            {
                Visit(Record(definition.definition).span);
                if (formationsPerProduct[definition.product] == 1 && definitionsPerFormation[definition.formation] == 1)
                {
                    assemblyIdentityRecords.insert(definition.definition);
                    assemblyIdentityRecords.insert(definition.formation);
                }
            }
        }
        std::optional<std::vector<unsigned char>> presentationUse;
        std::optional<std::vector<unsigned char>> propertyUse;
        std::optional<std::vector<unsigned char>> selectedUse;
        std::optional<std::vector<unsigned char>> selectedCaptionTopology;
        std::vector<std::size_t> propertyItems;
        std::vector<unsigned char> metadata(m_data->types.size());
        for (const auto& attachment : m_data->part.propertyAttachments)
        {
            for (const auto& value : FieldValue(attachment.representation, "items").children)
            {
                Visit(value.span);
                metadata[Target(value)] = 1;
                propertyItems.push_back(Target(value));
            }
        }
        if (m_data->pendingIssues.size() + m_data->interpretationDiagnostics.size() + m_data->complexMappingDiagnostics.size() +
                m_data->cylindricalBandRepairDiagnostics.size() >
            m_outcome.limits.preservedDiagnostics)
        {
            Fail(
                "PreservedDiagnosticLimit",
                "D2",
                "Interoperability diagnostic capacity exceeded",
                m_data->pendingIssues.back().diagnostic.source.primary,
                Status::ResourceLimit
            );
        }
        for (auto& issue : m_data->pendingIssues)
        {
            Visit(issue.diagnostic.source.primary);
            if (issue.proof == SemanticData::PreservationProof::MissingApplicationProtocol)
            {
                // Part/closure and physical units completed without an APD
                // dependency. No such record or role is manufactured. This
                // preserves a failed profile guard, not AP/global-rule conformance.
                if (!CanPreserveMissingApplicationProtocol() || m_data->part.applicationProtocolPresent ||
                    !Population("APPLICATION_PROTOCOL_DEFINITION").empty() || State(Phase::Part) != Progress::Passed)
                {
                    throw Failure{ issue.status, std::move(issue.diagnostic) };
                }
                continue;
            }
            const auto record = IndexOf(*m_data, issue.diagnostic.source.recordId);
            const auto& key = issue.diagnostic.source.key;
            if (issue.proof == SemanticData::PreservationProof::VerifiedExplicitUnitDimensions)
            {
                // The complete unit pass verified all explicit exponents against
                // SI-name/factor-derived dimensions, nominal quantity constraints
                // and conversion dependencies. Closure established ownership;
                // this interpretation grants no new roots. Nonphysical source
                // declarations remain source errors just as physical ones do.
                const auto found = std::find_if(
                    m_data->units.dimensionDefinitions.begin(),
                    m_data->units.dimensionDefinitions.end(),
                    [&](const auto& definition)
                    {
                        return definition.recordIndex == record;
                    }
                );
                const bool supportedUnit = IsA(*m_data, record, "SI_UNIT") ||
                                           (IsA(*m_data, record, "CONVERSION_BASED_UNIT") && IsA(*m_data, record, "LENGTH_UNIT"));
                if (m_data->requestedUnitInterpretation != UnitInterpretation::VerifiedExplicitUnitDimensions ||
                    !IsAp214SourceProfile(m_data->profile.kind) || !supportedUnit || found == m_data->units.dimensionDefinitions.end() ||
                    State(Phase::Part) != Progress::Passed || State(Phase::UnitDependencies) != Progress::Passed ||
                    issue.diagnostic.field != "dimensions" || key != "DerivedSlotMarkerRequired")
                {
                    State(Phase::Interoperability) = FailureProgress(issue.status);
                    throw Failure{ issue.status, std::move(issue.diagnostic) };
                }
                continue;
            }
            if (issue.proof == SemanticData::PreservationProof::ContextApplicationProtocols)
            {
                const auto selectedProtocol = [&](std::size_t protocol)
                {
                    return std::any_of(
                        m_data->part.roles.begin(),
                        m_data->part.roles.end(),
                        [&](const auto& role)
                        {
                            return role.recordIndex == protocol &&
                                   (role.name == "application_protocol" || role.name == "definition_application_protocol");
                        }
                    );
                };
                if (m_data->geometryScope != GeometryScope::GeneralBrep || !std::all_of(
                                                                               Population("APPLICATION_PROTOCOL_DEFINITION").begin(),
                                                                               Population("APPLICATION_PROTOCOL_DEFINITION").end(),
                                                                               [&](auto protocol)
                                                                               {
                                                                                   return selectedProtocol(protocol) && !physical[protocol];
                                                                               }
                                                                           ))
                {
                    throw Failure{ issue.status, std::move(issue.diagnostic) };
                }
                continue;
            }
            if (issue.proof == SemanticData::PreservationProof::DistinctApplicationContexts)
            {
                // The original singleton profile refusal remains a diagnostic.
                // Both descriptive application identities are retained; neither
                // can become a physical geometry/unit dependency or a new part.
                const auto selectedContext = [&](std::size_t context)
                {
                    return std::any_of(
                        m_data->part.roles.begin(),
                        m_data->part.roles.end(),
                        [&](const auto& role)
                        {
                            return role.recordIndex == context && (role.name == "application_context" || role.name == "product_application_context" ||
                                                                   role.name == "definition_application_context");
                        }
                    );
                };
                if (m_data->geometryScope != GeometryScope::GeneralBrep || !std::all_of(
                                                                               Population("APPLICATION_CONTEXT").begin(),
                                                                               Population("APPLICATION_CONTEXT").end(),
                                                                               [&](auto context)
                                                                               {
                                                                                   return selectedContext(context) && !physical[context];
                                                                               }
                                                                           ))
                {
                    throw Failure{ issue.status, std::move(issue.diagnostic) };
                }
                continue;
            }
            if (issue.proof == SemanticData::PreservationProof::AsciiInvariantRepresentationPurpose)
            {
                // AP214E3_2010.exp (71ab140fe7f77432...) purpose predicates are
                // exact comparisons, not transliteration or case folding. This
                // explicit carrier assumption excludes ASCII reserved names;
                // it does not establish decoded text or full schema conformance.
                // Limit the proof to selected definition/body roles. Inverse
                // property uses could impose surface_condition_correlation;
                // only already-checked shape/presentation links qualify here.
                const bool selected = std::any_of(
                    m_data->part.roles.begin(),
                    m_data->part.roles.end(),
                    [&](const auto& role)
                    {
                        return role.recordIndex == record && (role.name == "representation" || role.name == "definition_representation");
                    }
                );
                bool inverseSafe = true;
                for (const auto referenceIndex : m_data->source->Incoming(Record(record).id))
                {
                    const auto& reference = m_data->source->References()[referenceIndex];
                    Visit(reference.span);
                    const auto owner = reference.recordIndex;
                    if (!IsA(*m_data, owner, "SHAPE_DEFINITION_REPRESENTATION") && !IsA(*m_data, owner, "SHAPE_REPRESENTATION_RELATIONSHIP") &&
                        !IsA(*m_data, owner, "STYLED_ITEM"))
                    {
                        inverseSafe = false;
                    }
                }
                const auto& value = FieldValue(record, "name");
                if (!IsAp214SourceProfile(m_data->profile.kind) || m_data->geometryScope != GeometryScope::GeneralBrep || !selected || !inverseSafe ||
                    !m_data->source->IsAssumedNonAsciiData(value) || issue.diagnostic.field != "name" ||
                    issue.diagnostic.declaredIn != "REPRESENTATION" || value.span.begin != issue.diagnostic.source.primary.begin ||
                    value.span.end != issue.diagnostic.source.primary.end)
                {
                    State(Phase::Interoperability) = FailureProgress(issue.status);
                    throw Failure{ issue.status, std::move(issue.diagnostic) };
                }
                continue;
            }
            if (issue.proof == SemanticData::PreservationProof::N916RepresentationCaption)
            {
                // Only the selected body's representation or its proven SDR
                // definition representation is a caption here. Do not turn an
                // orphan, property representation or unknown profile into a
                // geometry root, or infer a character encoding from its bytes.
                const bool selectedRepresentation = std::any_of(
                    m_data->part.roles.begin(),
                    m_data->part.roles.end(),
                    [&](const auto& role)
                    {
                        return role.recordIndex == record && (role.name == "representation" || role.name == "definition_representation");
                    }
                );
                const auto& value = FieldValue(record, "name");
                const auto span = issue.diagnostic.source.primary;
                if (m_data->profile.kind != ProfileKind::Ap203N916Source || m_data->geometryScope != GeometryScope::GeneralBrep ||
                    issue.diagnostic.field != "name" || issue.diagnostic.declaredIn != "REPRESENTATION" || !selectedRepresentation ||
                    (value.kind != ValueKind::OpaqueString && value.kind != ValueKind::String) || value.span.begin != span.begin ||
                    value.span.end != span.end)
                {
                    State(Phase::Interoperability) = FailureProgress(issue.status);
                    throw Failure{ issue.status, std::move(issue.diagnostic) };
                }
                continue;
            }
            if (issue.proof == SemanticData::PreservationProof::OpaquePartIdentity ||
                issue.proof == SemanticData::PreservationProof::OpaqueShapeCaption ||
                issue.proof == SemanticData::PreservationProof::OpaqueAdministrativeLabel)
            {
                const auto& field = issue.diagnostic.field;
                const auto& owner = issue.diagnostic.declaredIn;
                const auto selected = [&](std::string_view name)
                {
                    return std::any_of(
                        m_data->part.roles.begin(),
                        m_data->part.roles.end(),
                        [&](const auto& role)
                        {
                            return role.name == name && role.recordIndex == record;
                        }
                    );
                };
                bool approved = false;
                if (issue.proof == SemanticData::PreservationProof::OpaquePartIdentity)
                {
                    // Pinned N2635/AP214 PD/PDF identifiers label the selected
                    // reference graph. They do not choose a revision/view in
                    // this singleton subset. Keep unknown text unavailable;
                    // Multiple sibling revisions/views never gain this proof.
                    const bool singleton = m_assemblyDefinitionChecks ?
                                               assemblyIdentityRecords.contains(record) :
                                               Population("PRODUCT_DEFINITION").size() == 1 && Population("PRODUCT_DEFINITION_FORMATION").size() == 1;
                    approved = field == "id" && singleton && !physical[record] &&
                               ((owner == "PRODUCT_DEFINITION" && selected("definition")) ||
                                (owner == "PRODUCT_DEFINITION_FORMATION" && selected("formation")));
                }
                else if (issue.proof == SemanticData::PreservationProof::OpaqueShapeCaption)
                {
                    // MSB's inherited RI.name is a caption, not a shell, role,
                    // unit or selector. This is deliberately field-level: its
                    // owner IS physical, while every defining field must still
                    // pass. REPRESENTATION/PDS purpose names remain excluded.
                    approved = owner == "REPRESENTATION_ITEM" && field == "name" && IsA(*m_data, record, "MANIFOLD_SOLID_BREP") &&
                               std::any_of(
                                   m_data->part.shapes.begin(),
                                   m_data->part.shapes.end(),
                                   [&](const auto& shape)
                                   {
                                       return shape.shape == record;
                                   }
                               );
                    if (owner == "REPRESENTATION_ITEM" && field == "name" && OpaqueTopologyCaption(record))
                    {
                        // These inherited RI labels do not select bounds,
                        // geometry, units or orientation. Only the selected
                        // shell's closure may authorize them; auxiliary geometry
                        // or an orphan's type alone does not prove ownership.
                        // Reach charges references and observes cancellation.
                        // Memoize once for all labels, never once per face/edge.
                        if (!selectedCaptionTopology)
                        {
                            std::vector<std::size_t> roots;
                            for (const auto& shape : m_data->part.shapes)
                            {
                                roots.push_back(shape.shell);
                            }
                            selectedCaptionTopology = Reach(std::move(roots), false);
                        }
                        approved = (*selectedCaptionTopology)[record];
                    }
                }
                else
                {
                    if (!selectedUse)
                    {
                        std::vector<std::size_t> roots;
                        for (const auto& role : m_data->part.roles)
                        {
                            roots.push_back(role.recordIndex);
                        }
                        selectedUse = Reach(std::move(roots), false);
                    }
                    // Administrative attachment already proved ALL assignment
                    // targets. Do not attach orphan labels by spelling/type.
                    approved = !physical[record] && (*selectedUse)[record] && OpaqueAdministrativeField(owner, field) && IsA(*m_data, record, owner);
                }
                const Value* preservedValue = &FieldValue(record, field);
                if (issue.proof == SemanticData::PreservationProof::OpaqueAdministrativeLabel && OpaqueAdministrativeList(owner, field))
                {
                    // N2635/AP214 PERSON name/title lists remain ordered LIST
                    // values. Only the diagnosed one-based element may use this
                    // proof; aggregate cardinality/type checks still run normally.
                    const auto& path = issue.diagnostic.source.parameterPath;
                    if (preservedValue->kind != ValueKind::Aggregate || path.size() != 2 || path[1] == 0 || path[1] > preservedValue->children.size())
                    {
                        approved = false;
                    }
                    else
                    {
                        preservedValue = &preservedValue->children[path[1] - 1];
                    }
                }
                const auto span = issue.diagnostic.source.primary;
                if (m_data->geometryScope != GeometryScope::GeneralBrep || preservedValue->kind != ValueKind::OpaqueString ||
                    preservedValue->span.begin != span.begin || preservedValue->span.end != span.end || !approved)
                {
                    State(Phase::Types) = FailureProgress(issue.status);
                    State(Phase::Interoperability) = FailureProgress(issue.status);
                    throw Failure{ issue.status, std::move(issue.diagnostic) };
                }
                continue;
            }
            if (issue.proof == SemanticData::PreservationProof::OpaqueDisplayText)
            {
                // These THREE fields are labels/identifiers, not geometry or
                // unit definitions (pinned N2635/AP214 declarations).
                // Prove actual selected/attached ownership, not mere type/name.
                const auto& field = issue.diagnostic.field;
                const auto& owner = issue.diagnostic.declaredIn;
                const bool selectedProduct = owner == "PRODUCT" && (field == "id" || field == "name") &&
                                             std::any_of(
                                                 m_data->part.roles.begin(),
                                                 m_data->part.roles.end(),
                                                 [&](const auto& role)
                                                 {
                                                     return role.name == "product" && role.recordIndex == record;
                                                 }
                                             );
                const bool attachedLayer = owner == "PRESENTATION_LAYER_ASSIGNMENT" && field == "name" &&
                                           record < m_administrativeAttachments.size() && m_administrativeAttachments[record] && !physical[record];
                if (m_data->geometryScope != GeometryScope::GeneralBrep || FieldValue(record, field).kind != ValueKind::OpaqueString ||
                    !(selectedProduct || attachedLayer))
                {
                    State(Phase::Types) = FailureProgress(issue.status);
                    State(Phase::Interoperability) = FailureProgress(issue.status);
                    throw Failure{ issue.status, std::move(issue.diagnostic) };
                }
                continue;
            }
            if (issue.proof == SemanticData::PreservationProof::CurveStyleWidth)
            {
                if (!presentationUse)
                {
                    presentationUse = Reach(m_presentationRoots, false);
                }
                // Authorize by the typed proof and actual owner graph, never by
                // a diagnostic message or exported role label. An invalid width
                // cannot use detached style residue to acquire part ownership.
                if (!IsAp214SourceProfile(m_data->profile.kind) || m_data->geometryScope != GeometryScope::GeneralBrep ||
                    !IsA(*m_data, record, "CURVE_STYLE") || FieldValue(record, "curve_width").kind != ValueKind::String ||
                    !(*presentationUse)[record] || physical[record])
                {
                    State(Phase::Types) = FailureProgress(issue.status);
                    State(Phase::Interoperability) = FailureProgress(issue.status);
                    throw Failure{ issue.status, std::move(issue.diagnostic) };
                }
                continue;
            }
            if (issue.proof == SemanticData::PreservationProof::ForeignRealItem)
            {
                // The foreign leaf carries a raw scalar, never an inferred
                // measure or expression result. Actual selected-property item
                // membership and the complete physical closure decide approval.
                if (!propertyUse)
                {
                    propertyUse = Reach(propertyItems, false);
                }
                if (!BorrowedForeignReal(record) || m_data->geometryScope != GeometryScope::GeneralBrep || !metadata[record] ||
                    !(*propertyUse)[record] || physical[record] || !m_outgoing[record].empty())
                {
                    State(Phase::Types) = Progress::Unsupported;
                    State(Phase::Interoperability) = Progress::Unsupported;
                    throw Failure{ issue.status, std::move(issue.diagnostic) };
                }
                continue;
            }
            if (key == "UnsupportedEntityType")
            {
                // A foreign context descriptor is never a new root. Product()
                // must have proved this very PC/PDC is the selected part's
                // unique context, and it must be absent from physical closure.
                const bool selected = std::any_of(
                    m_data->part.roles.begin(),
                    m_data->part.roles.end(),
                    [&](const auto& role)
                    {
                        return role.recordIndex == record && ((role.name == "product_context" && IsA(*m_data, record, "MECHANICAL_CONTEXT")) ||
                                                              (role.name == "definition_context" && IsA(*m_data, record, "DESIGN_CONTEXT")));
                    }
                );
                const bool foreignLayer = m_data->profile.kind == ProfileKind::Ap203N916Source &&
                                          IsA(*m_data, record, "PRESENTATION_LAYER_ASSIGNMENT") && record < m_administrativeAttachments.size() &&
                                          m_administrativeAttachments[record];
                if ((!foreignLayer && !(m_data->profile.kind == ProfileKind::Ap214MbxSource && selected)) || physical[record])
                {
                    State(Phase::Types) = FailureProgress(issue.status);
                    State(Phase::Interoperability) = FailureProgress(issue.status);
                    throw Failure{ issue.status, std::move(issue.diagnostic) };
                }
            }
            if (key == "DerivedSlotMarkerRequired" || key == "SourceRuleViolation:SI_UNIT.WR1")
            {
                // Only proven property-item dependencies can retain these two
                // source unit errors. The full outgoing physical closure is an
                // independent exclusion: sharing even one bad factor with the
                // body's units makes the original error fatal. Derived source
                // dimensions remain diagnostic observations, not clean units.
                if (!propertyUse)
                {
                    propertyUse = Reach(propertyItems, false);
                }
                if (!(*propertyUse)[record] || physical[record])
                {
                    State(key == "DerivedSlotMarkerRequired" ? Phase::Types : Phase::UnitDependencies) = FailureProgress(issue.status);
                    State(Phase::Interoperability) = FailureProgress(issue.status);
                    throw Failure{ issue.status, std::move(issue.diagnostic) };
                }
            }
            if (key == "RequiredAttributeMissing" && issue.diagnostic.field == "rendering_method")
            {
                // Unlike a nondefining RI label, a missing rendering method is
                // preserved only on a proven selected-part presentation chain.
                // Unattached style residue or a physical dependency cannot use it.
                if (!presentationUse)
                {
                    presentationUse = Reach(m_presentationRoots, false);
                }
                if (!IsA(*m_data, record, "SURFACE_STYLE_RENDERING") || !(*presentationUse)[record] || physical[record])
                {
                    State(Phase::Types) = FailureProgress(issue.status);
                    State(Phase::Interoperability) = FailureProgress(issue.status);
                    throw Failure{ issue.status, std::move(issue.diagnostic) };
                }
            }
            if (key == "ReferenceTypeMismatch")
            {
                if (issue.diagnostic.declaredIn == "STYLED_ITEM" && issue.diagnostic.field == "item")
                {
                    if (!presentationUse)
                    {
                        presentationUse = Reach(m_presentationRoots, false);
                    }
                    const auto target = IndexOf(*m_data, issue.diagnostic.source.targetId);
                    if (!IsAp214SourceProfile(m_data->profile.kind) || m_data->geometryScope != GeometryScope::GeneralBrep ||
                        !IsA(*m_data, record, "STYLED_ITEM") || !SelectedBodyRepresentation(target) || !(*presentationUse)[record] ||
                        physical[record])
                    {
                        State(Phase::Types) = FailureProgress(issue.status);
                        State(Phase::Interoperability) = FailureProgress(issue.status);
                        throw Failure{ issue.status, std::move(issue.diagnostic) };
                    }
                    continue;
                }
                if (issue.diagnostic.field == "over_ridden_style")
                {
                    if (!presentationUse)
                    {
                        presentationUse = Reach(m_presentationRoots, false);
                    }
                    if (m_data->profile.kind != ProfileKind::Ap214MbxSource || !m_preservedStyleOverrides.contains(record) ||
                        !(*presentationUse)[record] || physical[record])
                    {
                        State(Phase::Types) = FailureProgress(issue.status);
                        State(Phase::Interoperability) = FailureProgress(issue.status);
                        throw Failure{ issue.status, std::move(issue.diagnostic) };
                    }
                    continue;
                }
                const auto target = IndexOf(*m_data, issue.diagnostic.source.targetId);
                const bool selected = std::any_of(
                    m_data->part.roles.begin(),
                    m_data->part.roles.end(),
                    [&](const auto& role)
                    {
                        return (role.name == "definition" || role.name == "formation") && role.recordIndex == target;
                    }
                );
                // Consult the ownership proof itself. Exported role labels are
                // report text and cannot authorize a source-type exception.
                const bool attached = record < m_administrativeAttachments.size() && m_administrativeAttachments[record];
                if (!selected || !attached || physical[record])
                {
                    State(Phase::Types) = FailureProgress(issue.status);
                    State(Phase::Interoperability) = FailureProgress(issue.status);
                    throw Failure{ issue.status, std::move(issue.diagnostic) };
                }
            }
            const bool measure = key == "SourceRuleViolation:MEASURE_WITH_UNIT.WR1" || key == "SourceRuleViolation:POSITIVE_RATIO_MEASURE.WR1";
            if (measure && (!metadata[record] || physical[record]))
            {
                State(Phase::UnitDependencies) = FailureProgress(issue.status);
                State(Phase::Interoperability) = FailureProgress(issue.status);
                throw Failure{ issue.status, std::move(issue.diagnostic) };
            }
        }
        for (auto& issue : m_data->pendingIssues)
        {
            if (issue.proof == SemanticData::PreservationProof::VerifiedExplicitUnitDimensions)
            {
                const auto record = IndexOf(*m_data, issue.diagnostic.source.recordId);
                m_data->unitAssumptions.push_back(
                    { record,
                      Target(FieldValue(record, "dimensions")),
                      IsA(*m_data, record, "SI_UNIT") ? std::optional<std::size_t>{} :
                                                        std::optional<std::size_t>{ Target(FieldValue(record, "conversion_factor")) } }
                );
                m_data->unitInterpretationDiagnostics.push_back(std::move(issue.diagnostic));
            }
            else
            {
                m_data->preservedDiagnostics.push_back(std::move(issue.diagnostic));
            }
        }
        m_data->pendingIssues.clear();
    }
    void UnitContext()
    {
        if (!IsA(*m_data, m_context, "GLOBAL_UNIT_ASSIGNED_CONTEXT"))
        {
            // PARAMETRIC_REPRESENTATION_CONTEXT may legally label a unitless
            // parameter space. It does not establish physical model units or
            // inherit them from some other representation sharing the file.
            Fail(
                "UnsupportedPhysicalUnitContext",
                "D2",
                "Selected physical representation requires explicit unit assignment",
                Record(m_context).span,
                Status::Unsupported,
                m_context,
                0,
                {},
                {},
                {},
                "project physical unit guard; no defaults"
            );
        }
        const auto& dimension = FieldValue(m_context, "coordinate_space_dimension");
        if (!PositiveInteger(dimension.text))
        {
            FieldFailure(
                "SourceRuleViolation:DIMENSION_COUNT.WR1",
                "D3",
                "dimension_count must be positive",
                dimension.span,
                Status::Rejected,
                m_context,
                "coordinate_space_dimension",
                FieldDetail::AttributePath,
                ">0",
                dimension.text,
                "DIMENSION_COUNT.WR1"
            );
        }
        if (!Three(dimension.text))
        {
            FieldFailure(
                "UnsupportedCoordinateDimension",
                "D2",
                "This profile requires a 3D representation context",
                dimension.span,
                Status::Unsupported,
                m_context,
                "coordinate_space_dimension",
                FieldDetail::AttributePath,
                "3",
                dimension.text,
                "profile dimension guard"
            );
        }
        std::vector<std::size_t> lengths, angles, solids;
        const auto& assignments = FieldValue(m_context, "units");
        for (const auto& unit : assignments.children)
        {
            const auto target = Target(unit);
            if (IsA(*m_data, target, "LENGTH_UNIT"))
            {
                lengths.push_back(target);
            }
            else if (IsA(*m_data, target, "PLANE_ANGLE_UNIT"))
            {
                angles.push_back(target);
            }
            else if (IsA(*m_data, target, "SOLID_ANGLE_UNIT"))
            {
                solids.push_back(target);
            }
        }
        if (lengths.size() != 1 || angles.size() != 1 || solids.size() > 1)
        {
            FieldFailure(
                "UnsupportedUnitAssignment",
                "D2",
                "Exactly one length and plane angle, at most one solid angle are required",
                assignments.span,
                Status::Unsupported,
                m_context,
                "units",
                FieldDetail::AttributePath,
                "length=1 angle=1 solid-angle<=1",
                "different nominal quantities/counts",
                "profile unit guard; no defaults"
            );
        }
        m_data->units.lengthUnit = lengths[0];
        m_data->units.planeAngleUnit = angles[0];
        if (!solids.empty())
        {
            m_data->units.solidAngleUnit = solids[0];
        }
        for (auto r : Population("SI_UNIT"))
        {
            Visit(Record(r).span);
            SiUnit definition;
            definition.recordIndex = r;
            definition.siName = FieldValue(r, "name").text;
            definition.prefix = FieldValue(r, "prefix").kind == ValueKind::Unset ? "" : FieldValue(r, "prefix").text;
            const bool length = IsA(*m_data, r, "LENGTH_UNIT");
            const bool angle = IsA(*m_data, r, "PLANE_ANGLE_UNIT");
            const bool mass = IsA(*m_data, r, "MASS_UNIT");
            definition.quantity = length ? "Length" : angle ? "PlaneAngle" : mass ? "Mass" : "SolidAngle";
            const std::string expected = length ? "METRE" : angle ? "RADIAN" : mass ? "GRAM" : "STERADIAN";
            if (definition.siName != expected)
            {
                FieldFailure(
                    "UnsupportedSiUnitKind",
                    "D2",
                    "SI name/nominal quantity pairing is outside this profile",
                    FieldValue(r, "name").span,
                    Status::Unsupported,
                    r,
                    "name",
                    FieldDetail::AttributePath,
                    expected,
                    definition.siName,
                    "profile nominal SI-unit guard"
                );
            }
            // Canonical mass metadata uses kg; the source SI enumeration remains GRAM
            // and original prefix/value data are retained. No derived scale is inferred.
            definition.canonicalPower10 = Schema(*m_data).PrefixPower10(definition.prefix) + (length ? 3 : mass ? -3 : 0);
            if (length)
            {
                definition.dimensions[0] = 1;
            }
            if (mass)
            {
                definition.dimensions[1] = 1;
            }
            if (r == lengths[0])
            {
                m_data->units.lengthPower10Mm = definition.canonicalPower10;
            }
            m_data->units.siDefinitions.push_back(std::move(definition));
        }
        // AP214 source dimensions/cycles were checked before this phase. Conversion
        // scale/positivity and uncertainty evaluation are still deferred; AP203 retains
        // its existing interpretation boundary for conversion dimensions and cycles.
    }
};
} // namespace

Snapshot::Snapshot(std::shared_ptr<const SemanticData> data) : m_data(std::move(data))
{
    if (!m_data || !m_data->source || !m_data->bindingsComplete || m_data->fields.size() != m_data->source->Records().size())
    {
        throw std::invalid_argument("Semantic snapshot requires complete type/part data");
    }
}
Snapshot::Snapshot(std::shared_ptr<const SemanticData> data, std::size_t partIndex) : Snapshot(std::move(data))
{
    if (partIndex >= m_data->independentParts.size())
    {
        throw std::out_of_range("Unknown qualified source definition");
    }
    m_partIndex = partIndex;
}
ProductDocument::ProductDocument(std::shared_ptr<const SemanticData> data) : m_data(std::move(data))
{
    if (!m_data || !m_data->bindingsComplete || m_data->independentParts.empty())
    {
        throw std::invalid_argument("Complete independent source definitions required");
    }
}
const step_source::SourceSnapshot& ProductDocument::Source() const
{
    return *m_data->source;
}
std::size_t ProductDocument::PartCount() const
{
    return m_data->independentParts.size();
}
const std::optional<SourceSelectionEvidence>& ProductDocument::SourceSelection() const
{
    return m_data->sourceSelection;
}
const std::vector<Role>& ProductDocument::Roles() const
{
    return m_data->part.roles;
}
std::shared_ptr<const Snapshot> ProductDocument::Part(std::size_t index) const
{
    return std::shared_ptr<const Snapshot>(new Snapshot(m_data, index));
}
const step_source::SourceSnapshot& Snapshot::Source() const
{
    return *m_data->source;
}
const char* UnitInterpretationName(UnitInterpretation interpretation)
{
    switch (interpretation)
    {
        case UnitInterpretation::DeclaredOnly:
            return "DeclaredOnly";
        case UnitInterpretation::VerifiedExplicitUnitDimensions:
            return "VerifiedExplicitUnitDimensions";
    }
    return "Unknown";
}
UnitInterpretation Snapshot::AppliedUnitInterpretation() const
{
    return m_data->unitAssumptions.empty() ? UnitInterpretation::DeclaredOnly : UnitInterpretation::VerifiedExplicitUnitDimensions;
}
const std::vector<UnitDimensionAssumption>& Snapshot::UnitDimensionAssumptions() const
{
    return m_data->unitAssumptions;
}
const std::vector<Diagnostic>& Snapshot::UnitInterpretationDiagnostics() const
{
    return m_data->unitInterpretationDiagnostics;
}
const char* ComplexMappingInterpretationName(ComplexMappingInterpretation interpretation)
{
    switch (interpretation)
    {
        case ComplexMappingInterpretation::DeclaredOnly:
            return "DeclaredOnly";
        case ComplexMappingInterpretation::RegisteredFacetNames:
            return "RegisteredFacetNames";
    }
    throw std::invalid_argument("Unknown complex mapping interpretation");
}
const std::vector<ComplexMappingAssumption>& Snapshot::ComplexMappingAssumptions() const
{
    return m_data->complexMappingAssumptions;
}
const std::vector<Diagnostic>& Snapshot::ComplexMappingDiagnostics() const
{
    return m_data->complexMappingDiagnostics;
}
const std::vector<CylindricalBandRepairDeferral>& Snapshot::CylindricalBandRepairDeferrals() const
{
    return m_data->cylindricalBandRepairs;
}
const std::vector<Diagnostic>& Snapshot::CylindricalBandRepairDiagnostics() const
{
    return m_data->cylindricalBandRepairDiagnostics;
}
const Part& Snapshot::PartDefinition() const
{
    return m_partIndex ? m_data->independentParts.at(*m_partIndex).part : m_data->part;
}
const QualifiedSchemaProfile& Snapshot::SelectedProfile() const
{
    return m_data->profile;
}
const SchemaIdentifier& Snapshot::SchemaIdentity() const
{
    return m_data->schemaIdentity;
}
SchemaInterpretation Snapshot::AppliedInterpretation() const
{
    return m_data->appliedInterpretation.policy;
}
const SchemaInterpretationRequest& Snapshot::AppliedInterpretationRequest() const
{
    return m_data->appliedInterpretation;
}
const std::vector<Diagnostic>& Snapshot::InterpretationDiagnostics() const
{
    return m_data->interpretationDiagnostics;
}
const Units& Snapshot::UnitContext() const
{
    return m_partIndex ? m_data->independentParts.at(*m_partIndex).units : m_data->units;
}
const std::optional<SourceSelectionEvidence>& Snapshot::SourceSelection() const
{
    return m_data->sourceSelection;
}
const std::vector<Diagnostic>& Snapshot::PreservedDiagnostics() const
{
    return m_data->preservedDiagnostics;
}
const std::vector<std::size_t>& Snapshot::UnattachedNonGeometricDeclarations() const
{
    return m_data->unattachedNonGeometric;
}
std::span<const std::string_view> Snapshot::RegisteredTypes() const
{
    return Schema(*m_data).names;
}
const std::vector<std::size_t>* Snapshot::Instances(std::string_view type) const
{
    const auto id = Schema(*m_data).Find(type);
    return id ? &m_data->populations[*id] : nullptr;
}
SourceBindings Snapshot::Bindings() const
{
    return SourceBindings(m_data);
}
bool Snapshot::IsA(std::size_t record, std::string_view type) const
{
    return IsBoundType(*m_data, record, type);
}

std::vector<std::string> Snapshot::TypesOf(std::size_t record) const
{
    std::vector<std::string> names;
    for (std::size_t t = 0; t < Schema(*m_data).names.size(); ++t)
    {
        if (m_data->types.at(record).test(t))
        {
            names.emplace_back(Schema(*m_data).names[t]);
        }
    }
    return names;
}
AttributeView Snapshot::Attribute(std::size_t r, std::size_t c, std::size_t p) const
{
    return AttributeAt(*m_data, r, c, p);
}
FieldView Snapshot::Field(std::size_t record, std::string_view name) const
{
    return FieldView(m_data, record, FindFieldBinding(*m_data, record, name));
}
FieldView Snapshot::Field(std::size_t record, std::string_view declaredIn, std::string_view name) const
{
    return FieldView(m_data, record, FindFieldBinding(*m_data, record, name, declaredIn));
}
std::size_t Snapshot::ReferenceTarget(const step_source::Value& value) const
{
    return ResolveBoundReference(*m_data, value);
}

FieldView::FieldView(std::shared_ptr<const SemanticData> data, std::size_t record, std::size_t binding)
    : m_data(std::move(data)), m_record(record), m_binding(binding)
{
}
const AttributeView& FieldView::Declaration() const
{
    return m_data->fields.at(m_record).attributes.at(m_binding).declaration;
}
const step_source::Value& FieldView::Value() const
{
    return *Declaration().value;
}
AttributeLocation FieldView::Location() const
{
    const auto& bound = m_data->fields.at(m_record).attributes.at(m_binding);
    const auto& record = m_data->source->Records().at(m_record);
    return { record.id, record.components.at(bound.component).name, Value().span, { bound.parameter + 1 } };
}
std::size_t FieldView::ReferenceTarget() const
{
    return ResolveBoundReference(*m_data, Value());
}
std::vector<std::size_t> FieldView::ReferenceTargets() const
{
    const auto& bound = m_data->fields.at(m_record).attributes.at(m_binding);
    if (!bound.referenceAggregate || Value().kind != step_source::ValueKind::Aggregate)
    {
        throw std::invalid_argument("Source slot is not an explicit reference aggregate");
    }
    std::vector<std::size_t> result;
    result.reserve(Value().children.size());
    for (const auto& member : Value().children)
    {
        result.push_back(ResolveBoundReference(*m_data, member));
    }
    return result;
}
std::vector<std::size_t> Snapshot::UsedIn(std::string_view target, std::string_view type, std::string_view field) const
{
    const auto typeId = Schema(*m_data).Find(type);
    if (!typeId || !Schema(*m_data).HasField(*typeId, field))
    {
        throw std::invalid_argument("Unregistered USEDIN type/attribute role");
    }
    IndexOf(*m_data, target);
    std::vector<std::size_t> result;
    for (auto index : m_data->source->Incoming(target))
    {
        const auto& reference = m_data->source->References()[index];
        if (IsA(reference.recordIndex, type) &&
            Attribute(reference.recordIndex, reference.componentIndex, reference.parameterPath[0] - 1).name == field)
        {
            result.push_back(index);
        }
    }
    return result;
}
bool Snapshot::UsedByRepresentation(std::size_t record) const
{
    return IsA(record, "REPRESENTATION_ITEM") && m_data->representationUse.at(record) != 0;
}
Outcome Check(const step_source::InspectOutcome& source, Limits limits)
{
    return Check(source, limits, SourcePolicy::Strict);
}
Outcome Check(const step_source::InspectOutcome& source, Limits limits, SourcePolicy policy, GeometryScope scope)
{
    return Check(source, limits, policy, scope, SchemaInterpretation::DeclaredOnly);
}
Outcome Check(
    const step_source::InspectOutcome& source,
    Limits limits,
    SourcePolicy policy,
    GeometryScope scope,
    const SchemaInterpretationRequest& interpretation
)
{
    return Check(source, limits, policy, scope, interpretation, UnitInterpretation::DeclaredOnly);
}
static Outcome CheckImpl(
    const step_source::InspectOutcome& source,
    Limits limits,
    SourcePolicy policy,
    GeometryScope scope,
    const SchemaInterpretationRequest& interpretation,
    UnitInterpretation unitInterpretation,
    ComplexMappingInterpretation complexMappingInterpretation,
    std::span<const CylindricalBandRepairDeferral> cylindricalBandRepairs,
    bool independentRequest,
    bool inspectDefinitions = false,
    const SelectedSourceRequest* selection = nullptr,
    std::shared_ptr<const AssemblyInputs>* assemblyInputs = nullptr,
    bool assemblyDefinitionChecks = false,
    std::shared_ptr<const AssemblyDocument>* assemblyDocument = nullptr,
    UnreferencedPlacementPolicy placementPolicy = UnreferencedPlacementPolicy::Reject
)
{
    if (!cylindricalBandRepairs.empty() && (scope != GeometryScope::GeneralBrep || cylindricalBandRepairs.size() > source.limits.records))
    {
        throw std::invalid_argument("Source repair deferrals require bounded explicit general scope");
    }
    if (complexMappingInterpretation != ComplexMappingInterpretation::DeclaredOnly &&
        (complexMappingInterpretation != ComplexMappingInterpretation::RegisteredFacetNames || policy != SourcePolicy::PreserveNonGeometric ||
         scope != GeometryScope::GeneralBrep))
    {
        throw std::invalid_argument("Registered complex facet interpretation requires explicit general interoperability");
    }
    if (unitInterpretation != UnitInterpretation::DeclaredOnly &&
        (unitInterpretation != UnitInterpretation::VerifiedExplicitUnitDimensions || policy != SourcePolicy::PreserveNonGeometric ||
         scope != GeometryScope::GeneralBrep))
    {
        throw std::invalid_argument("Verified unit interpretation requires explicit general interoperability");
    }
    if ((policy != SourcePolicy::Strict && policy != SourcePolicy::PreserveNonGeometric) ||
        (scope != GeometryScope::ExplicitOuterBounds && scope != GeometryScope::GeneralBrep))
    {
        throw std::invalid_argument("Unknown source checking policy or geometry scope");
    }
    if (interpretation != SchemaInterpretation::DeclaredOnly &&
        ((interpretation != SchemaInterpretation::UnregisteredAp214As2010 && interpretation != SchemaInterpretation::Ap214NameOnlyAs2010 &&
          interpretation != SchemaInterpretation::SourceBoundAp214As2010) ||
         policy != SourcePolicy::PreserveNonGeometric || scope != GeometryScope::GeneralBrep))
    {
        throw std::invalid_argument("Unverified schema interpretation requires explicit general interoperability");
    }
    // Trust the immutable source owner's actual assumption, never resettable
    // InspectOutcome notices/options. P0 cannot acquire this unverified carrier.
    if ((source.requestedCarrierInterpretation != step_source::CarrierInterpretation::DeclaredOnly ||
         (source.source && (source.source->AppliedCarrierAssumption() || source.source->HasAsciiInvariantDataAssumption()))) &&
        (policy != SourcePolicy::PreserveNonGeometric || scope != GeometryScope::GeneralBrep))
    {
        throw std::invalid_argument("Unverified carrier interpretation requires explicit general interoperability");
    }
    Outcome result;
    if (selection)
    {
        const auto canonical = [](const auto& id)
        {
            return !id.empty() && id.size() <= 64 && (id.size() == 1 || id[0] != '0') &&
                   std::all_of(
                       id.begin(),
                       id.end(),
                       [](char c)
                       {
                           return c >= '0' && c <= '9';
                       }
                   );
        };
        if (!independentRequest || inspectDefinitions || !cylindricalBandRepairs.empty() || selection->sourceSha256.size() != 64 ||
            !std::all_of(
                selection->sourceSha256.begin(),
                selection->sourceSha256.end(),
                [](char c)
                {
                    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                }
            ) ||
            selection->definitionIds.empty() || selection->definitionIds.size() > source.limits.records ||
            !std::all_of(selection->definitionIds.begin(), selection->definitionIds.end(), canonical) || !selection->maximumIsolatedFailures ||
            selection->maximumIsolatedFailures > source.limits.records || !selection->maximumInfluenceVisits ||
            selection->maximumInfluenceVisits > 10000000)
        {
            throw std::invalid_argument("Selected source requires explicit canonical identities and bounded positive isolation budgets");
        }
        result.sourceSelectionRequest = *selection;
    }
    result.limits = limits;
    result.policy = policy;
    result.geometryScope = scope;
    result.requestedInterpretation = interpretation;
    result.requestedUnitInterpretation = unitInterpretation;
    result.requestedComplexMappingInterpretation = complexMappingInterpretation;
    result.definitionInspectionRequested = inspectDefinitions;
    if (auto input = ValidateSchemaInterpretationInput(source, interpretation); !input)
    {
        result.status = Status::Unsupported;
        result.diagnostics.push_back(std::move(input.error()));
        return result;
    }
    if (!source.source || source.status != step_source::Status::Inspected)
    {
        for (const auto& diagnostic : source.diagnostics)
        {
            Diagnostic wrapped;
            wrapped.source = diagnostic;
            wrapped.classification = "SourceInspectionFailure";
            wrapped.responsibility = "ticket 02 syntax/index/reference check";
            result.diagnostics.push_back(std::move(wrapped));
        }
        return result;
    }
    if (limits.dependencyVisits > 10000000)
    {
        result.status = Status::ResourceLimit;
        Diagnostic diagnostic;
        diagnostic.source.key = "InvalidSemanticLimits";
        diagnostic.source.stage = "D2";
        diagnostic.source.message = "Semantic visit budget exceeds implementation ceiling";
        diagnostic.classification = "ResourceLimit";
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }
    if (selection && (selection->sourceSha256 != source.source->Sha256() || source.sha256 != source.source->Sha256() || !source.captured ||
                      (source.captured.get() != &source.source->Bytes() && *source.captured != source.source->Bytes())))
    {
        result.status = Status::Unsupported;
        Diagnostic diagnostic;
        diagnostic.source.key = "SourceSelectionInputMismatch";
        diagnostic.source.stage = "D2";
        diagnostic.source.message = "Source selection must bind the immutable original capture and its consistent receipt";
        diagnostic.classification = "ProfileUnsupported";
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }
    auto data = std::make_shared<SemanticData>();
    data->source = source.source;
    data->policy = policy;
    data->geometryScope = scope;
    data->requestedUnitInterpretation = unitInterpretation;
    data->requestedComplexMappingInterpretation = complexMappingInterpretation;
    data->cylindricalBandRepairs.assign(cylindricalBandRepairs.begin(), cylindricalBandRepairs.end());
    data->cylindricalBandRepairUses.resize(cylindricalBandRepairs.size());
    data->preservedDiagnosticLimit = limits.preservedDiagnostics;
    data->definitionInspectionRequested = inspectDefinitions;
    if (selection)
    {
        data->selectedSourceRequest = *selection;
    }
    Checker checker(data, result, independentRequest);
    std::shared_ptr<const AssemblyInputs> assemblyCandidate;
    try
    {
        if (assemblyInputs)
        {
            auto candidate = checker.InspectAssemblyContextInputs(assemblyDefinitionChecks);
            result.status = Status::Deferred;
            checker.PublishProgress();
            result.part = "NotQualifiedAssemblyInputs";
            result.siUnits = std::all_of(
                                 candidate->Contexts().begin(),
                                 candidate->Contexts().end(),
                                 [](const auto& context)
                                 {
                                     return context.units.lengthPower10Mm.has_value();
                                 }
                             ) ?
                                 "CheckedPerAssemblyContext" :
                                 "DeferredAssemblyContextConversion";
            *assemblyInputs = std::move(candidate);
            return result;
        }
        if (inspectDefinitions)
        {
            checker.InspectDefinitions();
            // Even all-green per-definition prerequisites have not run whole-
            // document ownership or final interoperability approval. There is
            // deliberately no qualified source snapshot/document in this mode.
            result.status = Status::Deferred;
            checker.PublishProgress();
            return result;
        }
        if (assemblyDocument)
        {
            assemblyCandidate = checker.InspectAssemblyContextInputs(true);
            checker.FinalizeAssemblySource(*assemblyCandidate, placementPolicy);
        }
        else
        {
            checker.Run();
        }
        if (independentRequest && data->independentParts.empty())
        {
            data->independentParts.push_back({ data->part, data->units });
        }
        const bool unitsResolved = data->independentParts.empty() ? data->units.lengthPower10Mm.has_value() :
                                                                    std::all_of(
                                                                        data->independentParts.begin(),
                                                                        data->independentParts.end(),
                                                                        [](const auto& part)
                                                                        {
                                                                            return part.units.lengthPower10Mm.has_value();
                                                                        }
                                                                    );
        result.status = unitsResolved ? Status::Checked : Status::Deferred;
        if (!data->part.deferredRegionFaces.empty() || std::any_of(
                                                           data->independentParts.begin(),
                                                           data->independentParts.end(),
                                                           [](const auto& part)
                                                           {
                                                               return !part.part.deferredRegionFaces.empty();
                                                           }
                                                       ))
        {
            result.status = Status::Deferred;
        }
        result.preservedDiagnostics = data->preservedDiagnostics;
        result.unitInterpretationDiagnostics = data->unitInterpretationDiagnostics;
        if (result.status == Status::Checked &&
            (!result.preservedDiagnostics.empty() || !data->unitInterpretationDiagnostics.empty() || !data->complexMappingAssumptions.empty() ||
             !data->interpretationDiagnostics.empty() || data->source->AppliedCarrierAssumption()))
        {
            result.status = Status::CheckedWithDiagnostics;
        }
        if (data->sourceSelection && (result.status == Status::Checked || result.status == Status::CheckedWithDiagnostics))
        {
            result.status = Status::CheckedSelectedDefinitions;
        }
        if (assemblyDocument)
        {
            *assemblyDocument = std::make_shared<const AssemblyDocument>(
                std::make_shared<const AssemblyDocumentData>(AssemblyDocumentData{ data, std::move(assemblyCandidate), checker.RetainedPlacements() })
            );
        }
        else if (data->independentParts.empty())
        {
            result.snapshot = std::make_shared<const Snapshot>(std::move(data));
        }
        else
        {
            result.document = std::make_shared<const ProductDocument>(std::move(data));
        }
    }
    catch (const Failure& failure)
    {
        result.status = failure.status;
        result.diagnostics.push_back(failure.diagnostic);
    }
    checker.PublishProgress();
    return result;
}
AssemblyInputOutcome InspectAssemblyInputs(
    const step_source::InspectOutcome& source,
    Limits limits,
    SourcePolicy policy,
    const SchemaInterpretationRequest& interpretation,
    UnitInterpretation unitInterpretation,
    ComplexMappingInterpretation complexMappingInterpretation
)
{
    AssemblyInputOutcome result;
    result.checks = CheckImpl(
        source,
        limits,
        policy,
        GeometryScope::GeneralBrep,
        interpretation,
        unitInterpretation,
        complexMappingInterpretation,
        {},
        false,
        false,
        nullptr,
        &result.inputs
    );
    return result;
}
AssemblyInputOutcome InspectAssemblyDefinitions(
    const step_source::InspectOutcome& source,
    Limits limits,
    SourcePolicy policy,
    const SchemaInterpretationRequest& interpretation,
    UnitInterpretation unitInterpretation,
    ComplexMappingInterpretation complexMappingInterpretation
)
{
    AssemblyInputOutcome result;
    result.checks = CheckImpl(
        source,
        limits,
        policy,
        GeometryScope::GeneralBrep,
        interpretation,
        unitInterpretation,
        complexMappingInterpretation,
        {},
        false,
        false,
        nullptr,
        &result.inputs,
        true
    );
    return result;
}
AssemblySourceOutcome CheckAssemblySource(
    const step_source::InspectOutcome& source,
    Limits limits,
    SourcePolicy policy,
    const SchemaInterpretationRequest& interpretation,
    UnitInterpretation unitInterpretation,
    ComplexMappingInterpretation complexMappingInterpretation,
    UnreferencedPlacementPolicy placementPolicy
)
{
    if (placementPolicy != UnreferencedPlacementPolicy::Reject && placementPolicy != UnreferencedPlacementPolicy::RetainDisconnected)
    {
        throw std::invalid_argument("Unknown unreferenced placement policy");
    }
    AssemblySourceOutcome result;
    result.checks = CheckImpl(
        source,
        limits,
        policy,
        GeometryScope::GeneralBrep,
        interpretation,
        unitInterpretation,
        complexMappingInterpretation,
        {},
        false,
        false,
        nullptr,
        nullptr,
        false,
        &result.document,
        placementPolicy
    );
    return result;
}
Outcome Check(
    const step_source::InspectOutcome& source,
    Limits limits,
    SourcePolicy policy,
    GeometryScope scope,
    const SchemaInterpretationRequest& interpretation,
    UnitInterpretation unitInterpretation,
    ComplexMappingInterpretation complexMappingInterpretation,
    std::span<const CylindricalBandRepairDeferral> cylindricalBandRepairs
)
{
    return CheckImpl(source, limits, policy, scope, interpretation, unitInterpretation, complexMappingInterpretation, cylindricalBandRepairs, false);
}
Outcome CheckIndependentProducts(
    const step_source::InspectOutcome& source,
    Limits limits,
    SourcePolicy policy,
    const SchemaInterpretationRequest& interpretation,
    UnitInterpretation unitInterpretation,
    ComplexMappingInterpretation complexMappingInterpretation,
    std::span<const CylindricalBandRepairDeferral> cylindricalBandRepairs
)
{
    return CheckImpl(
        source,
        limits,
        policy,
        GeometryScope::GeneralBrep,
        interpretation,
        unitInterpretation,
        complexMappingInterpretation,
        cylindricalBandRepairs,
        true
    );
}
Outcome InspectIndependentDefinitions(
    const step_source::InspectOutcome& source,
    Limits limits,
    SourcePolicy policy,
    const SchemaInterpretationRequest& interpretation,
    UnitInterpretation unitInterpretation,
    ComplexMappingInterpretation complexMappingInterpretation
)
{
    return CheckImpl(
        source,
        limits,
        policy,
        GeometryScope::GeneralBrep,
        interpretation,
        unitInterpretation,
        complexMappingInterpretation,
        {},
        true,
        true
    );
}
Outcome CheckSelectedIndependentProducts(
    const step_source::InspectOutcome& source,
    Limits limits,
    SourcePolicy policy,
    const SelectedSourceRequest& selection,
    const SchemaInterpretationRequest& interpretation,
    UnitInterpretation unitInterpretation,
    ComplexMappingInterpretation complexMappingInterpretation
)
{
    return CheckImpl(
        source,
        limits,
        policy,
        GeometryScope::GeneralBrep,
        interpretation,
        unitInterpretation,
        complexMappingInterpretation,
        {},
        true,
        false,
        &selection
    );
}
const char* StatusName(Status status)
{
    switch (status)
    {
        case Status::Checked:
            return "Checked";
        case Status::CheckedWithDiagnostics:
            return "CheckedWithDiagnostics";
        case Status::Deferred:
            return "Deferred";
        case Status::Rejected:
            return "Rejected";
        case Status::Unsupported:
            return "Unsupported";
        case Status::ResourceLimit:
            return "ResourceLimit";
        case Status::SourceFailure:
            return "SourceFailure";
        case Status::CheckedSelectedDefinitions:
            return "CheckedSelectedDefinitions";
    }
    return "Unknown";
}
} // namespace step_semantics
