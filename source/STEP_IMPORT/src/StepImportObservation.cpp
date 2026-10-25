// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportObservation.h"

#include <algorithm>
#include <stdexcept>

namespace step_import::detail::observation_impl
{
namespace
{
std::shared_ptr<Report> Child(const Report& parent, std::string_view evidenceScope)
{
    auto checks = std::make_shared<Report>();
    checks->generalBrep = true;
    checks->encoding = parent.encoding;
    checks->sourceEvidenceScope = evidenceScope;
    checks->source = parent.source;
    checks->sourceSha256 = parent.sourceSha256;
    checks->sourceSchema = parent.sourceSchema;
    checks->numericalIntegration = parent.numericalIntegration;
    checks->periodicNormalizationRequested = parent.periodicNormalizationRequested;
    checks->supportDomainRestrictionRequested = parent.supportDomainRestrictionRequested;
    return checks;
}
template <class Members>
void Aggregate(Report& parent, const Members& members)
{
    if (parent.numericalIntegration)
    {
        const bool completed = std::all_of(
            members.begin(),
            members.end(),
            [](const auto& member)
            {
                return member.checks->sourceEvidenceScope == "AssemblyPlacementWrapper" ||
                       member.checks->sourceEvidenceScope == "AssemblyWireframeDefinition" ||
                       (member.checks->numericalIntegration && member.checks->numericalIntegration->completed);
            }
        );
        const auto geometry = std::find_if(
            members.begin(),
            members.end(),
            [](const auto& member)
            {
                return member.checks->sourceEvidenceScope != "AssemblyPlacementWrapper" &&
                       member.checks->sourceEvidenceScope != "AssemblyWireframeDefinition";
            }
        );
        if (geometry == members.end())
        {
            // A complete geometry-only assembly has no face integral at all.
            // Do not report a completed zero or an unexplained pending integral.
            parent.numericalIntegration.reset();
            return;
        }
        parent.numericalIntegration->completed = geometry != members.end() && completed;
        if (parent.numericalIntegration->completed)
        {
            parent.numericalIntegration->appliedRelativeAccuracy = geometry->checks->numericalIntegration->appliedRelativeAccuracy;
        }
    }
}
void DiscardBodies(Report& parent) noexcept
{
    for (auto& body : parent.bodies)
    {
        if (body.checks && body.checks->nativeCalls)
        {
            body.checks->candidateDiscarded = true;
        }
    }
}
enum class Kind
{
    Bodies,
    Definitions
};
class Batch
{
public:

    Batch(Report& report, Kind kind) : m_report(report), m_kind(kind)
    {
    }
    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;
    ~Batch() noexcept
    {
        // Returning an expected error does not unwind an exception. An
        // uncompleted batch also settles observations on that normal exit.
        if (!m_completed)
        {
            m_report.candidateDiscarded = m_report.nativeCalls > 0 || (m_kind == Kind::Bodies && m_incorporated > 0);
            if (m_kind == Kind::Bodies)
            {
                DiscardBodies(m_report);
            }
            else
            {
                for (auto& definition : m_report.definitions)
                {
                    if (definition.checks)
                    {
                        if (definition.checks->nativeCalls)
                        {
                            definition.checks->candidateDiscarded = true;
                        }
                        DiscardBodies(*definition.checks);
                    }
                }
            }
        }
    }
    void Incorporated()
    {
        ++m_incorporated;
    }
    void Complete()
    {
        m_completed = true;
    }

private:

    Report& m_report;
    Kind m_kind;
    std::size_t m_incorporated = 0;
    bool m_completed = false;
};
class DefinitionCounter
{
public:

    DefinitionCounter(Report& checks, const Execution& execution) : m_checks(checks), m_execution(execution), m_before(execution.NativeCalls())
    {
    }
    DefinitionCounter(const DefinitionCounter&) = delete;
    DefinitionCounter& operator=(const DefinitionCounter&) = delete;
    ~DefinitionCounter() noexcept
    {
        // Execution charges only root and current leaf. A definition owns the
        // inclusive difference, including partial repair attempts on failure.
        m_checks.nativeCalls = m_execution.NativeCalls() - m_before;
        if (!m_incorporated)
        {
            m_checks.candidateDiscarded = m_checks.nativeCalls > 0;
        }
    }
    void Incorporated() noexcept
    {
        m_incorporated = true;
    }

private:

    Report& m_checks;
    const Execution& m_execution;
    std::size_t m_before;
    bool m_incorporated = false;
};
} // namespace
[[nodiscard]] Result<void> Bodies(
    const step_semantics::Snapshot& source,
    Report& parent,
    Execution& execution,
    MemberOperation<step_semantics::Part::Shape> member,
    CandidateOperation candidate
)
{
    // Execution is the one request ledger. Body counters are updated by the
    // native builder, including its repair protocol; never replace them with a
    // root difference or add an originalAttempt a second time.
    static_cast<void>(execution);
    Batch batch(parent, Kind::Bodies);
    for (const auto& shape : source.PartDefinition().shapes)
    {
        auto checks = Child(parent, parent.sourceEvidenceScope);
        parent.bodies.push_back({ shape.shape, shape.shell, false, checks });
        const auto index = parent.bodies.size() - 1;
        auto result = member.invoke(member.state, shape, *checks);
        if (!result)
        {
            return tl::unexpected(std::move(result.error()));
        }
        parent.bodies[index].checksPassed = true;
        batch.Incorporated();
    }
    Aggregate(parent, parent.bodies);
    auto result = candidate.invoke(candidate.state);
    if (!result)
    {
        return tl::unexpected(std::move(result.error()));
    }
    batch.Complete();
    return {};
}
template <class Source>
[[nodiscard]] Result<void> DefinitionSequence(
    const Source& source,
    std::span<const std::size_t> selected,
    Report& parent,
    Execution& execution,
    MemberOperation<DefinitionMember> member,
    CandidateOperation candidate
)
{
    Batch batch(parent, Kind::Definitions);
    for (std::size_t i = 0; i < selected.size(); ++i)
    {
        // Resolve identities only when entering this member, preserving the
        // prior observed prefix if an internal source contract fails later.
        constexpr bool assembly = std::is_same_v<Source, step_semantics::AssemblyDocument>;
        auto part = [&]
        {
            if constexpr (assembly)
            {
                return source.Definition(selected[i]);
            }
            else
            {
                return source.Part(selected[i]);
            }
        }();
        const auto& definition = part->PartDefinition();
        const auto owner = std::find_if(
            definition.roles.begin(),
            definition.roles.end(),
            [](const auto& role)
            {
                return role.name == "definition";
            }
        );
        const bool wrapper = assembly && definition.shapeRole == "AssemblyPlacementWrapper" && definition.shapes.empty();
        const bool wireframe = assembly && definition.shapeRole == "WireframeModel" && definition.shapes.empty();
        if (owner == definition.roles.end() || (definition.shapes.empty() && !wrapper && !wireframe))
        {
            throw std::logic_error("UnqualifiedDocumentDefinition");
        }
        auto checks = Child(
            parent,
            wrapper   ? "AssemblyPlacementWrapper" :
            wireframe ? "AssemblyWireframeDefinition" :
            assembly  ? "AssemblyDefinitionRoot" :
                        "ProductDocumentRoot"
        );
        parent.definitions.push_back({ owner->recordIndex, false, checks });
        const auto index = parent.definitions.size() - 1;
        DefinitionCounter counter(*checks, execution);
        auto result = member.invoke(member.state, DefinitionMember{ i, owner->recordIndex, std::move(part) }, *checks);
        if (!result)
        {
            return tl::unexpected(std::move(result.error()));
        }
        parent.definitions[index].checksPassed = true;
        counter.Incorporated();
        batch.Incorporated();
    }
    Aggregate(parent, parent.definitions);
    auto result = candidate.invoke(candidate.state);
    if (!result)
    {
        return tl::unexpected(std::move(result.error()));
    }
    batch.Complete();
    return {};
}
Result<void> Definitions(
    const step_semantics::ProductDocument& source,
    std::span<const std::size_t> selected,
    Report& parent,
    Execution& execution,
    MemberOperation<DefinitionMember> member,
    CandidateOperation candidate
)
{
    return DefinitionSequence(source, selected, parent, execution, member, candidate);
}
Result<void> Definitions(
    const step_semantics::AssemblyDocument& source,
    std::span<const std::size_t> selected,
    Report& parent,
    Execution& execution,
    MemberOperation<DefinitionMember> member,
    CandidateOperation candidate
)
{
    return DefinitionSequence(source, selected, parent, execution, member, candidate);
}
} // namespace step_import::detail::observation_impl
