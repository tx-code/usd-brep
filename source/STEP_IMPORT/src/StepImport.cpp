// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportExecution.h"
#include "StepImportNativeData.h"
#include "StepImportPlan.h"
#include "StepImportPublication.h"

#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace step_import
{
Quality Quality::PlanarFixture()
{
    return { "Q-P0-mm-01", Fraction(1, 100000000), Fraction(1, 10000000), Fraction(1, 10000000), Fraction(1, 100000), Fraction(1, 10000),
             Fraction(),   Fraction(1, 10000000),  Fraction(1, 1000000),  Fraction(1, 100000) };
}
Quality Quality::GeneralDevelopment()
{
    // Explicit calibration candidate for real-file development. Source uncertainty
    // never chooses these budgets, and this is not a production default.
    return { "Q-AP214-development-mm-01", Fraction(1, 1000000000), Fraction(1, 1000), Fraction(1, 1000),
             Fraction(1, 1000),           Fraction(1, 1000),       Fraction(),        Fraction(1, 100000),
             Fraction(1, 1000),           Fraction(1, 100) };
}
Quality Quality::GeneralToleranceStudy()
{
    auto quality = GeneralDevelopment();
    quality.id = "Q-AP214-tolerance-study-mm-01";
    quality.consistencyMm = quality.derivedUvMm = quality.initialWorkToleranceMm = quality.maximumWorkToleranceMm = Fraction(1, 100);
    return quality;
}
Quality Quality::GeneralWorkToleranceStudy()
{
    auto quality = GeneralToleranceStudy();
    quality.id = "Q-AP214-work-tolerance-study-mm-01";
    quality.maximumWorkToleranceMm = Fraction(1, 50);
    quality.toleranceGrowthMm = Fraction(1, 100);
    return quality;
}
Options Options::PlanarFixture()
{
    return { step_source::Limits::PlanarFixtures(), {}, {}, Quality::PlanarFixture(), {}, {} };
}
namespace
{
void Validate(const Options& options)
{
    const auto& q = options.quality;
    if (options.normalizePeriodicFaces && !options.generalBrep)
    {
        throw std::invalid_argument("Periodic topology normalization requires explicit general geometry");
    }
    if (options.numericalIntegration)
    {
        const auto accuracy = options.numericalIntegration->relativeAccuracy;
        if (!options.generalBrep || !std::isfinite(accuracy) || accuracy < 1e-8 || accuracy > 1e-4)
        {
            throw std::invalid_argument("Numerical integration requires general geometry and finite relative accuracy in [1e-8, 1e-4]");
        }
    }
    if (options.restrictUnusedSupportDomains && !options.generalBrep)
    {
        throw std::invalid_argument("Support-domain restriction requires the explicit general geometry path");
    }
    const std::array positive = { q.definitionMm,           q.consistencyMm, q.derivedUvMm, q.initialWorkToleranceMm,
                                  q.maximumWorkToleranceMm, q.queryMm,       q.areaMm2,     q.volumeMm3 };
    if (q.id.empty() ||
        !std::all_of(
            positive.begin(),
            positive.end(),
            [](Fraction value)
            {
                return value > Fraction();
            }
        ) ||
        q.toleranceGrowthMm < Fraction() || q.maximumWorkToleranceMm < q.initialWorkToleranceMm || options.nativeLimits.nativeCalls > 1000000 ||
        options.nativeLimits.reportBytes > 256 * 1024 * 1024)
    {
        throw std::invalid_argument("Explicit positive import quality and bounded native/report limits required");
    }
    const auto fields = [](const Quality& quality)
    {
        return std::tie(
            quality.definitionMm,
            quality.consistencyMm,
            quality.derivedUvMm,
            quality.initialWorkToleranceMm,
            quality.maximumWorkToleranceMm,
            quality.toleranceGrowthMm,
            quality.queryMm,
            quality.areaMm2,
            quality.volumeMm3
        );
    };
    if (q.id == "Q-P0-mm-01" && fields(q) != fields(Quality::PlanarFixture()))
    {
        throw std::invalid_argument("Frozen quality identity cannot be reused for different native parameters");
    }
    if ((q.id == "Q-AP214-development-mm-01" && fields(q) != fields(Quality::GeneralDevelopment())) ||
        (q.id == "Q-AP214-tolerance-study-mm-01" && fields(q) != fields(Quality::GeneralToleranceStudy())) ||
        (q.id == "Q-AP214-work-tolerance-study-mm-01" && fields(q) != fields(Quality::GeneralWorkToleranceStudy())) ||
        (options.generalBrep && q.id == "Q-P0-mm-01") || (!options.generalBrep && options.sourcePolicy != step_semantics::SourcePolicy::Strict))
    {
        throw std::invalid_argument("Explicit general geometry quality and source policy must match the selected import path");
    }
    // Opaque metadata is an explicit interoperability choice, not a side effect
    // of selecting a geometry quality. Strict/P0 requests never relax carriers.
    if (options.headerNamePolicy != step_source::HeaderNamePolicy::StrictAscii &&
        ((options.headerNamePolicy != step_source::HeaderNamePolicy::PreserveOpaqueBytes &&
          options.headerNamePolicy != step_source::HeaderNamePolicy::PreserveFileNameText) || !options.generalBrep ||
         options.sourcePolicy != step_semantics::SourcePolicy::PreserveNonGeometric))
    {
        throw std::invalid_argument("Opaque HEADER name requires explicit general interoperability");
    }
    if (options.dataStringPolicy != step_source::DataStringPolicy::StrictAscii &&
        (options.dataStringPolicy != step_source::DataStringPolicy::PreserveOpaqueBytes || !options.generalBrep ||
         options.sourcePolicy != step_semantics::SourcePolicy::PreserveNonGeometric))
    {
        throw std::invalid_argument("Opaque DATA text requires explicit general interoperability");
    }
    if (options.schemaInterpretation != step_semantics::SchemaInterpretation::DeclaredOnly &&
        ((options.schemaInterpretation != step_semantics::SchemaInterpretation::UnregisteredAp214As2010 &&
          options.schemaInterpretation != step_semantics::SchemaInterpretation::Ap214NameOnlyAs2010) ||
         !options.generalBrep || options.sourcePolicy != step_semantics::SourcePolicy::PreserveNonGeometric))
    {
        throw std::invalid_argument("Unverified schema interpretation requires explicit general interoperability");
    }
    if (options.carrierInterpretation != step_source::CarrierInterpretation::DeclaredOnly &&
        (!step_source::IsExplicitCarrierInterpretation(options.carrierInterpretation) || !options.generalBrep ||
         options.sourcePolicy != step_semantics::SourcePolicy::PreserveNonGeometric))
    {
        throw std::invalid_argument("Unverified carrier interpretation requires explicit general interoperability");
    }
}
template <class Capture>
Outcome Import(const Options& options, Capture capture)
{
    Outcome result;
    auto& report = result.report;
    report.sourceStatus = report.semanticStatus = report.planarStatus = "NotRun";
    report.generalBrep = options.generalBrep;
    report.supportDomainRestrictionRequested = options.restrictUnusedSupportDomains;
    report.periodicNormalizationRequested = options.normalizePeriodicFaces;
    if (options.numericalIntegration)
    {
        report.numericalIntegration = NumericalIntegrationObservation{ options.numericalIntegration->relativeAccuracy, {}, false };
    }
    step_source::InspectOutcome raw;
    raw.limits = options.sourceLimits;
    raw.headerNamePolicy = options.headerNamePolicy;
    raw.dataStringPolicy = options.dataStringPolicy;
    raw.requestedCarrierInterpretation = options.carrierInterpretation;
    detail::Execution execution(options, report);
    detail::Publication publication(options, result, raw, execution);
    try
    {
        execution.Check(Phase::BeforeCapture);
        raw = capture();
        report.source = raw.source;
        report.captured = raw.captured;
        // Capture owns the immutable receipt even if a later parse/reference
        // failure withheld the source graph. A successful snapshot also owns
        // this evidence; a caller clearing its mutable report cannot erase it.
        report.carrierAssumption = raw.carrierAssumption;
        report.sourceSha256 = raw.sha256;
        report.sourceStatus = step_source::StatusName(raw.status);
        if (raw.status == step_source::Status::Inspected)
        {
            execution.Check(Phase::Captured);
        }
        if (options.generalBrep)
        {
            report.generalSource = step_semantics::Check(
                raw,
                options.semanticLimits,
                options.sourcePolicy,
                step_semantics::GeometryScope::GeneralBrep,
                options.schemaInterpretation
            );
            const auto& checked = *report.generalSource;
            report.sourceSchema = checked.schemaSelection;
            report.semanticStatus = step_semantics::StatusName(checked.status);
            report.planarStatus = "NotApplicable";
            report.diagnostics = checked.diagnostics;
            if (!checked.snapshot)
            {
                // Raw capture failures precede semantic classification. Preserve
                // I/O, carrier and resource categories, as on the planar path.
                const auto status = raw.status == step_source::Status::IoFailure ? Status::SourceFailure :
                                    (raw.status == step_source::Status::ResourceLimit || checked.status == step_semantics::Status::ResourceLimit) ?
                                                                                   Status::ResourceLimit :
                                    (raw.status == step_source::Status::Unsupported || checked.status == step_semantics::Status::Unsupported) ?
                                                                                   Status::Unsupported :
                                                                                   Status::Rejected;
                throw detail::Failure{ status, "GeneralSourceUnavailable", "Required typed source was not published", {}, "GeneralBrep" };
            }
            execution.Check(Phase::SemanticsChecked);
            publication.Accept(detail::BuildGeneral(checked.snapshot, options, report, execution));
            return result;
        }
        auto semantics = step_semantics::Check(raw, options.semanticLimits);
        report.sourceSchema = semantics.schemaSelection;
        report.semanticStatus = step_semantics::StatusName(semantics.status);
        if (semantics.status == step_semantics::Status::Checked || semantics.status == step_semantics::Status::Deferred)
        {
            execution.Check(Phase::SemanticsChecked);
        }
        auto planar = step_planar::Check(semantics, { options.quality.id, options.quality.consistencyMm }, options.planarLimits);
        report.planarStatus = step_planar::StatusName(planar.status);
        report.diagnostics = planar.diagnostics;
        if (planar.status != step_planar::Status::Checked)
        {
            switch (planar.status)
            {
                case step_planar::Status::Deferred:
                    result.status = Status::Deferred;
                    break;
                case step_planar::Status::Unsupported:
                    result.status = Status::Unsupported;
                    break;
                case step_planar::Status::Indeterminate:
                    result.status = Status::Indeterminate;
                    break;
                case step_planar::Status::ResourceLimit:
                    result.status = Status::ResourceLimit;
                    break;
                default:
                    result.status = Status::Rejected;
                    break;
            }
            if (raw.status == step_source::Status::IoFailure)
            {
                result.status = Status::SourceFailure;
            }
            else if (raw.status == step_source::Status::Unsupported || semantics.status == step_semantics::Status::Unsupported)
            {
                result.status = Status::Unsupported;
            }
            else if (raw.status == step_source::Status::ResourceLimit || semantics.status == step_semantics::Status::ResourceLimit)
            {
                result.status = Status::ResourceLimit;
            }
        }
        else
        {
            execution.Check(Phase::PlanarChecked);
            report.declarations = detail::DescribeSource(planar, options.quality);
            const auto plan = detail::MakePlan(planar);
            publication.Accept(detail::Build(plan, options, report, execution));
            return result;
        }
    }
    catch (const detail::Failure& failure)
    {
        publication.Fail(failure);
    }
    catch (const std::bad_alloc&)
    {
        throw; // Unexpected allocation failure is not a configured quota or a proved source error.
    }
    catch (const std::invalid_argument&)
    {
        throw; // Preserve the existing invalid-configuration exception contract of the source stages.
    }
    catch (const std::overflow_error& error)
    {
        publication.Fail({ Status::Indeterminate, error.what(), "Bounded native numerical evidence is unestablished", {} });
    }
    catch (const std::domain_error& error)
    {
        publication.Fail({ Status::Indeterminate, error.what(), "Native derived evidence is unestablished", {} });
    }
    catch (const std::exception& error)
    {
        publication.Fail({ Status::BuildFailure, "NativeReturnOrConstructionFailure", error.what(), {} });
    }
    catch (...)
    {
        publication.Fail({ Status::BuildFailure, "UnknownImportFailure", "Unknown internal exception; no source-invalid claim", {}, "Execution" });
    }
    publication.FinishFailure();
    return result;
}
} // namespace
Outcome ImportBytes(std::string bytes, const Options& options)
{
    const Options frozen = options;
    Validate(frozen);
    return Import(
        frozen,
        [&]
        {
            return step_source::InspectBytes(
                std::move(bytes),
                frozen.sourceLimits,
                frozen.headerNamePolicy,
                frozen.dataStringPolicy,
                frozen.carrierInterpretation
            );
        }
    );
}
Outcome ImportFile(const std::filesystem::path& path, const Options& options)
{
    const Options frozen = options;
    const auto capturedPath = path;
    Validate(frozen);
    return Import(
        frozen,
        [&]
        {
            return step_source::InspectFile(
                capturedPath,
                frozen.sourceLimits,
                frozen.headerNamePolicy,
                frozen.dataStringPolicy,
                frozen.carrierInterpretation
            );
        }
    );
}
const char* StatusName(Status status)
{
    switch (status)
    {
        case Status::Accepted:
            return "Accepted";
        case Status::AcceptedUnderInterpretation:
            return "AcceptedUnderInterpretation";
        case Status::Rejected:
            return "Rejected";
        case Status::Unsupported:
            return "Unsupported";
        case Status::Deferred:
            return "Deferred";
        case Status::Indeterminate:
            return "Indeterminate";
        case Status::ResourceLimit:
            return "ResourceLimit";
        case Status::BuildFailure:
            return "BuildFailure";
        case Status::PostcheckFailure:
            return "PostcheckFailure";
        case Status::SourceFailure:
            return "SourceFailure";
        case Status::Cancelled:
            return "Cancelled";
    }
    throw std::logic_error("Unknown import status");
}
const char* MaterialPointClassName(MaterialPointClass classification)
{
    switch (classification)
    {
        case MaterialPointClass::Outside:
            return "Outside";
        case MaterialPointClass::Inside:
            return "Inside";
        case MaterialPointClass::Face:
            return "OnFace";
        case MaterialPointClass::Edge:
            return "OnEdge";
        case MaterialPointClass::Vertex:
            return "OnVertex";
        case MaterialPointClass::Indeterminate:
            return "Indeterminate";
    }
    throw std::logic_error("Unknown material point classification");
}
} // namespace step_import
