// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportLegacyFailure.h"
#include "StepImportObservation.h"
#include "StepImportOrientationRepair.h"

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace step_import;
using namespace step_import::detail;
namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
std::string Read(const char* filename)
{
    std::ifstream input(filename, std::ios::binary);
    Require(bool(input), "public fixture exists");
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}
void BodyError(const step_semantics::Snapshot& source, Status errorStatus)
{
    auto options = Options::PlanarFixture();
    Report parent;
    parent.sourceSha256 = source.Source().Sha256();
    parent.encoding = ReportEncoding::CompactV2;
    parent.sourceEvidenceScope = "TestOwningParent";
    parent.sourceSchema.emplace();
    parent.periodicNormalizationRequested = parent.supportDomainRestrictionRequested = true;
    parent.numericalIntegration = NumericalIntegrationObservation{};
    Execution execution(options, parent);
    unsigned entered = 0;
    bool finalized = false;
    auto result = ObserveBodies(
        source,
        parent,
        execution,
        [&](const step_semantics::Part::Shape& shape, Report& checks) -> Result<void>
        {
            Require(checks.sourceSha256 == parent.sourceSha256, "child inherits original source identity");
            Require(
                checks.generalBrep && checks.encoding == parent.encoding && checks.sourceSchema.has_value() &&
                    checks.sourceEvidenceScope == parent.sourceEvidenceScope && checks.periodicNormalizationRequested &&
                    checks.supportDomainRestrictionRequested,
                "child inherits the complete observation context"
            );
            if (++entered == 2)
            {
                return tl::unexpected(Failure{ errorStatus, "TestBodyStop", "observed error", shape.shape, "Execution" });
            }
            if (auto charged = execution.ChargeNative(checks, shape.shape); !charged)
            {
                return tl::unexpected(std::move(charged.error()));
            }
            checks.numericalIntegration->completed = true;
            return {};
        },
        [&]() -> Result<int>
        {
            finalized = true;
            return 7;
        }
    );
    Require(
        !result && result.error().status == errorStatus && result.error().key == "TestBodyStop",
        "a returned error keeps its category and diagnostic identity"
    );
    Require(!finalized && entered == 2 && parent.bodies.size() == 2, "error stops the batch before candidate finalization");
    Require(
        parent.bodies[0].checksPassed && parent.bodies[0].checks->candidateDiscarded,
        "completed earlier member is discarded after a returned error"
    );
    Require(
        !parent.bodies[1].checksPassed && !parent.bodies[1].checks->candidateDiscarded,
        "an uncharged failed member remains incomplete without invented work"
    );
    Require(
        parent.nativeCalls == 1 && parent.checkpoints == 1 && parent.candidateDiscarded,
        "observation bookkeeping adds neither native calls nor checkpoints"
    );
    Require(!parent.numericalIntegration->completed, "failed batch does not invent completed aggregation");
}
void FinalizerError(const step_semantics::Snapshot& source, bool exceptional)
{
    auto options = Options::PlanarFixture();
    Report parent;
    parent.numericalIntegration = NumericalIntegrationObservation{};
    Execution execution(options, parent);
    bool threw = false;
    try
    {
        auto result = ObserveBodies(
            source,
            parent,
            execution,
            [&](const step_semantics::Part::Shape& shape, Report& checks) -> Result<void>
            {
                if (auto charged = execution.ChargeNative(checks, shape.shape); !charged)
                {
                    return tl::unexpected(std::move(charged.error()));
                }
                checks.numericalIntegration->completed = true;
                checks.numericalIntegration->appliedRelativeAccuracy = 1e-4;
                return {};
            },
            [&]() -> Result<int>
            {
                Require(
                    parent.numericalIntegration->completed && parent.numericalIntegration->appliedRelativeAccuracy == 1e-4,
                    "integration aggregation precedes candidate sealing"
                );
                if (exceptional)
                {
                    throw std::logic_error("candidate sealing failed");
                }
                return tl::unexpected(Failure{ Status::PostcheckFailure, "TestSealStop", "returned finalization error", {} });
            }
        );
        Require(
            !exceptional && !result && result.error().status == Status::PostcheckFailure,
            "candidate sealing error returns without reclassification"
        );
    }
    catch (const std::logic_error&)
    {
        threw = true;
    }
    Require(threw == exceptional, "unexpected exceptions retain their existing outer contract");
    Require(
        parent.candidateDiscarded && parent.numericalIntegration->completed,
        "late failure preserves completed integration facts and discards unpublished candidates"
    );
    for (const auto& body : parent.bodies)
    {
        Require(body.checksPassed && body.checks->candidateDiscarded, "late failure preserves every completed member's facts");
    }
}
void UnexpectedMember(const step_semantics::Snapshot& source)
{
    auto options = Options::PlanarFixture();
    Report parent;
    Execution execution(options, parent);
    unsigned entered = 0;
    bool caught = false;
    try
    {
        auto result = ObserveBodies(
            source,
            parent,
            execution,
            [&](const step_semantics::Part::Shape& shape, Report& checks) -> Result<void>
            {
                if (++entered == 2)
                {
                    throw std::bad_alloc();
                }
                if (auto charged = execution.ChargeNative(checks, shape.shape); !charged)
                {
                    return tl::unexpected(std::move(charged.error()));
                }
                return {};
            },
            []() -> Result<int>
            {
                return 1;
            }
        );
        static_cast<void>(result);
    }
    catch (const std::bad_alloc&)
    {
        caught = true;
    }
    Require(
        caught && parent.candidateDiscarded && parent.bodies[0].checks->candidateDiscarded && !parent.bodies[1].checksPassed,
        "allocation failure retains cleanup without becoming a configured quota error"
    );
}
void LegacyErrors()
{
    auto known = LegacyFailureAdapter(
        []() -> int
        {
            throw Failure{ Status::Unsupported, "LegacyKind", "preserved legacy failure", 17, "GeneralBrep" };
        }
    );
    Require(
        !known && known.error().status == Status::Unsupported && known.error().record == 17 && known.error().stage == "GeneralBrep",
        "legacy adapter converts only the existing domain failure payload"
    );
    bool allocation = false, argument = false, witness = false;
    try
    {
        auto result = LegacyFailureAdapter(
            []() -> int
            {
                throw std::bad_alloc();
            }
        );
        static_cast<void>(result);
    }
    catch (const std::bad_alloc&)
    {
        allocation = true;
    }
    try
    {
        auto result = LegacyFailureAdapter(
            []() -> int
            {
                throw std::invalid_argument("existing caller contract");
            }
        );
        static_cast<void>(result);
    }
    catch (const std::invalid_argument&)
    {
        argument = true;
    }
    try
    {
        auto result = LegacyFailureAdapter(
            []() -> int
            {
                throw OrientationFailureCandidate{ Failure{ Status::PostcheckFailure, "Witness", "repair control", {} }, {}, { 17 } };
            }
        );
        static_cast<void>(result);
    }
    catch (const OrientationFailureCandidate& control)
    {
        witness = control.targetFaces == std::vector<std::size_t>{ 17 };
    }
    Require(allocation && argument && witness, "legacy adapter preserves allocation, argument and native-witness channels");
}
struct Finisher
{
    Result<int> operator()() const;
};
struct VoidMember
{
    void operator()(const step_semantics::Part::Shape&, Report&) const;
};
struct OtherErrorMember
{
    tl::expected<void, int> operator()(const step_semantics::Part::Shape&, Report&) const;
};
struct DefinitionWork
{
    Result<void> operator()(const DefinitionMember&, Report&) const;
};
struct AlreadyResult
{
    Result<int> operator()() const;
};
struct ReferencedCandidate
{
    Result<int>& operator()() const;
};
struct ThrowingMoveCandidate
{
    ThrowingMoveCandidate(ThrowingMoveCandidate&&) noexcept(false);
};
struct ThrowingMoveFinisher
{
    Result<ThrowingMoveCandidate> operator()() const;
};
template <class Work>
concept BodyAccepted = requires(const step_semantics::Snapshot& source, Report& parent, Execution& execution, Work work, Finisher finish) {
    ObserveBodies(source, parent, execution, work, finish);
};
template <class Work>
concept LegacyAccepted = requires(Work work) { LegacyFailureAdapter(work); };
static_assert(!BodyAccepted<VoidMember> && !BodyAccepted<OtherErrorMember> && !BodyAccepted<DefinitionWork>);
static_assert(!LegacyAccepted<AlreadyResult>);
static_assert(!observation_impl::CandidateWork<ReferencedCandidate> && !observation_impl::CandidateWork<ThrowingMoveFinisher>);
void DefinitionError(const step_semantics::ProductDocument& source)
{
    auto options = Options::PlanarFixture();
    Report parent;
    parent.numericalIntegration = NumericalIntegrationObservation{};
    Execution execution(options, parent);
    const std::array<std::size_t, 2> selected{ 0, 1 };
    auto result = ObserveDefinitions(
        source,
        selected,
        parent,
        execution,
        [&](const DefinitionMember& member, Report& checks) -> Result<void>
        {
            Require(checks.sourceEvidenceScope == "ProductDocumentRoot", "definition binds its root evidence scope");
            if (member.ordinal == 0)
            {
                if (auto charged = execution.ChargeNative(checks, member.sourceDefinitionRecord); !charged)
                {
                    return tl::unexpected(std::move(charged.error()));
                }
                checks.numericalIntegration->completed = true;
                return {};
            }
            auto bodies = ObserveBodies(
                *member.source,
                checks,
                execution,
                [&](const step_semantics::Part::Shape& shape, Report& leaf) -> Result<void>
                {
                    if (auto charged = execution.ChargeNative(leaf, shape.shape); !charged)
                    {
                        return tl::unexpected(std::move(charged.error()));
                    }
                    return tl::unexpected(Failure{ Status::ResourceLimit, "TestNestedStop", "returned leaf error", shape.shape });
                },
                []() -> Result<int>
                {
                    return 1;
                }
            );
            Require(!bodies, "inner returned error reaches its definition");
            return tl::unexpected(std::move(bodies.error()));
        },
        []() -> Result<int>
        {
            return 7;
        }
    );
    Require(!result && result.error().key == "TestNestedStop", "nested error is not reclassified");
    Require(parent.nativeCalls == 2 && parent.checkpoints == 2, "nested observation does not charge extra work");
    Require(
        parent.definitions.size() == 2 && parent.definitions[0].checksPassed && !parent.definitions[1].checksPassed,
        "failed definition cannot be marked incorporated"
    );
    const auto& first = *parent.definitions[0].checks;
    const auto& second = *parent.definitions[1].checks;
    Require(first.nativeCalls == 1 && second.nativeCalls == 1, "definition settles inclusive root difference on error");
    Require(
        parent.candidateDiscarded && first.candidateDiscarded && second.candidateDiscarded && second.bodies[0].checks->candidateDiscarded,
        "returned error discards the whole observed prefix"
    );
}
void DefinitionFinalizerError(const step_semantics::ProductDocument& source, bool exceptional)
{
    auto options = Options::PlanarFixture();
    Report parent;
    parent.numericalIntegration = NumericalIntegrationObservation{};
    Execution execution(options, parent);
    const std::array<std::size_t, 2> selected{ 0, 1 };
    bool allocation = false;
    try
    {
        auto result = ObserveDefinitions(
            source,
            selected,
            parent,
            execution,
            [&](const DefinitionMember& member, Report& checks) -> Result<void>
            {
                if (auto charged = execution.ChargeNative(checks, member.sourceDefinitionRecord); !charged)
                {
                    return tl::unexpected(std::move(charged.error()));
                }
                checks.numericalIntegration->completed = true;
                checks.numericalIntegration->appliedRelativeAccuracy = 1e-4;
                return {};
            },
            [&]() -> Result<int>
            {
                Require(
                    parent.numericalIntegration->completed && parent.numericalIntegration->appliedRelativeAccuracy == 1e-4,
                    "document integration is settled before final snapshot allocation"
                );
                if (exceptional)
                {
                    throw std::bad_alloc();
                }
                return tl::unexpected(Failure{ Status::PostcheckFailure, "DocumentSealStop", "document finalization failed", {} });
            }
        );
        Require(!exceptional && !result && result.error().key == "DocumentSealStop", "document finalization returns its original error");
    }
    catch (const std::bad_alloc&)
    {
        allocation = true;
    }
    Require(allocation == exceptional, "document snapshot allocation failure is not a configured budget error");
    Require(
        parent.candidateDiscarded && parent.nativeCalls == 2 && parent.numericalIntegration->completed,
        "document sealing failure preserves completed facts and discards the complete candidate"
    );
    Require(parent.definitions.size() == 2, "finalization observes the complete selected definition inventory");
    for (const auto& definition : parent.definitions)
    {
        Require(
            definition.checksPassed && definition.checks->candidateDiscarded && definition.checks->nativeCalls == 1,
            "completed definitions keep their checks and inclusive accounting after document sealing failure"
        );
    }
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "two-body and independent-definition fixtures required");
        auto options = Options::PlanarFixture();
        const auto raw = step_source::InspectBytes(Read(argv[1]), options.sourceLimits);
        const auto checked = step_semantics::Check(raw, options.semanticLimits, options.sourcePolicy, step_semantics::GeometryScope::GeneralBrep);
        Require(checked.snapshot && checked.snapshot->PartDefinition().shapes.size() == 2, "qualified source with two real members");
        for (auto error : { Status::Cancelled, Status::Unsupported, Status::ResourceLimit })
        {
            BodyError(*checked.snapshot, error);
        }
        FinalizerError(*checked.snapshot, false);
        FinalizerError(*checked.snapshot, true);
        UnexpectedMember(*checked.snapshot);
        LegacyErrors();
        const auto documentRaw = step_source::InspectBytes(Read(argv[2]), options.sourceLimits);
        const auto document = step_semantics::CheckIndependentProducts(documentRaw, options.semanticLimits, options.sourcePolicy);
        Require(document.document && document.document->PartCount() == 2, "qualified independent definitions");
        DefinitionError(*document.document);
        DefinitionFinalizerError(*document.document, false);
        DefinitionFinalizerError(*document.document, true);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
