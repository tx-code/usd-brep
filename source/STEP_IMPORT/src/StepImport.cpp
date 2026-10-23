// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportExecution.h"
#include "StepImportGroupedBuild.h"
#include "StepImportNativeData.h"
#include "StepImportOrientationRepair.h"
#include "StepImportPlan.h"
#include "StepImportPublication.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <tuple>
#include <type_traits>

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
    if (options.cylindricalBandRepair)
    {
        const auto& request = *options.cylindricalBandRepair;
        const auto id = [](const std::string& value)
        {
            return !value.empty() && value.size() <= 64 && (value.size() == 1 || value[0] != '0') &&
                   std::all_of(
                       value.begin(),
                       value.end(),
                       [](char c)
                       {
                           return c >= '0' && c <= '9';
                       }
                   );
        };
        if (!options.generalBrep || options.faceOrientationRepair || options.definitionSelection || options.restrictUnusedSupportDomains ||
            options.normalizePeriodicFaces || request.sourceSha256.size() != 64 ||
            !std::all_of(
                request.sourceSha256.begin(),
                request.sourceSha256.end(),
                [](char c)
                {
                    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                }
            ) ||
            !request.maximumFaces || request.maximumFaces > options.sourceLimits.records || request.faces.empty() ||
            request.faces.size() > request.maximumFaces || request.maximumBoundEdits < request.faces.size() * 2 ||
            request.maximumBoundEdits > 2 * options.sourceLimits.records || !request.maximumProofOperations ||
            request.maximumProofOperations > 100000000)
        {
            throw std::invalid_argument("Explicit cylindrical-band repair requires original identity, general-only scope and bounded nonempty work");
        }
        std::set<std::tuple<std::string, std::string, std::string>> unique;
        for (const auto& face : request.faces)
        {
            if (!id(face.definitionId) || !id(face.shapeId) || !id(face.faceId) || !id(face.sourceOuterBoundIds[0]) ||
                !id(face.sourceOuterBoundIds[1]) || face.sourceOuterBoundIds[0] == face.sourceOuterBoundIds[1] ||
                !unique.emplace(face.definitionId, face.shapeId, face.faceId).second)
            {
                throw std::invalid_argument("Cylindrical-band repair requires unique explicit face uses and original boundary pairs");
            }
        }
    }
    if (options.complexMappingInterpretation != step_semantics::ComplexMappingInterpretation::DeclaredOnly &&
        (options.complexMappingInterpretation != step_semantics::ComplexMappingInterpretation::RegisteredFacetNames || !options.generalBrep ||
         options.sourcePolicy != step_semantics::SourcePolicy::PreserveNonGeometric))
    {
        throw std::invalid_argument("Registered complex facet interpretation requires explicit general interoperability");
    }
    if (options.definitionSelection)
    {
        const auto& request = *options.definitionSelection;
        const auto digit = [](char c)
        {
            return c >= '0' && c <= '9';
        };
        if (!options.generalBrep || request.sourceSha256.size() != 64 ||
            !std::all_of(
                request.sourceSha256.begin(),
                request.sourceSha256.end(),
                [](char c)
                {
                    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                }
            ) ||
            request.definitionIds.empty() || request.definitionIds.size() > options.sourceLimits.records ||
            std::any_of(
                request.definitionIds.begin(),
                request.definitionIds.end(),
                [&](const auto& id)
                {
                    return id.empty() || id.size() > 64 || (id.size() > 1 && id[0] == '0') || !std::all_of(id.begin(), id.end(), digit);
                }
            ))
        {
            throw std::invalid_argument("Definition selection requires general geometry, an original SHA256 and bounded nonempty canonical IDs");
        }
    }
    if (options.sourceErrorIsolation &&
        (!options.definitionSelection || !options.generalBrep || options.faceOrientationRepair || options.cylindricalBandRepair ||
         !options.sourceErrorIsolation->maximumFailures || options.sourceErrorIsolation->maximumFailures > options.sourceLimits.records ||
         !options.sourceErrorIsolation->maximumInfluenceVisits || options.sourceErrorIsolation->maximumInfluenceVisits > 10000000))
    {
        throw std::invalid_argument("Source clause isolation requires explicit definition selection, positive budgets and no repair combination");
    }
    const auto& q = options.quality;
    if (options.faceOrientationRepair)
    {
        const auto& r = *options.faceOrientationRepair;
        const auto digits = [](const std::string& id)
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
        if (!options.generalBrep || options.normalizePeriodicFaces || options.restrictUnusedSupportDomains || r.sourceSha256.size() != 64 ||
            !std::all_of(
                r.sourceSha256.begin(),
                r.sourceSha256.end(),
                [](char c)
                {
                    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                }
            ) ||
            !r.maximumFaceEdits || r.maximumFaceEdits > options.sourceLimits.records || !r.maximumProofControls || r.maximumProofControls > 2000000 ||
            !r.maximumProofProducts || r.maximumProofProducts > 100000000 || r.edits.empty() || r.edits.size() > r.maximumFaceEdits)
        {
            throw std::invalid_argument("Face-orientation repair requires explicit general scope, source identity and a bounded nonempty edit plan");
        }
        std::set<std::tuple<std::string, std::string, std::string>> identities;
        for (const auto& edit : r.edits)
        {
            if (!digits(edit.definitionId) || !digits(edit.shapeId) || !digits(edit.faceId) || edit.expectedSourceSameSense == edit.targetSameSense ||
                !identities.emplace(edit.definitionId, edit.shapeId, edit.faceId).second)
            {
                throw std::invalid_argument("Repair edits require unique definition/shape/face identities and an explicit opposite target sense");
            }
        }
    }
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
          options.headerNamePolicy != step_source::HeaderNamePolicy::PreserveFileNameText) ||
         !options.generalBrep || options.sourcePolicy != step_semantics::SourcePolicy::PreserveNonGeometric))
    {
        throw std::invalid_argument("Opaque HEADER name requires explicit general interoperability");
    }
    if (options.reportEncoding != ReportEncoding::ExpandedV1 && (options.reportEncoding != ReportEncoding::CompactV2 || !options.generalBrep))
    {
        throw std::invalid_argument("Compact source report requires the explicit general import path");
    }
    if (options.dataStringPolicy != step_source::DataStringPolicy::StrictAscii &&
        ((options.dataStringPolicy != step_source::DataStringPolicy::PreserveOpaqueBytes &&
          options.dataStringPolicy != step_source::DataStringPolicy::PreserveAsciiInvariantBytes) ||
         !options.generalBrep || options.sourcePolicy != step_semantics::SourcePolicy::PreserveNonGeometric))
    {
        throw std::invalid_argument("Opaque DATA text requires explicit general interoperability");
    }
    if (options.unitInterpretation != step_semantics::UnitInterpretation::DeclaredOnly &&
        (options.unitInterpretation != step_semantics::UnitInterpretation::VerifiedExplicitUnitDimensions || !options.generalBrep ||
         options.sourcePolicy != step_semantics::SourcePolicy::PreserveNonGeometric))
    {
        throw std::invalid_argument("Verified unit interpretation requires explicit general interoperability");
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
template <class Result = Outcome, class Capture>
Result Import(const Options& options, Capture capture)
{
    if constexpr (!std::is_same_v<Result, DocumentOutcome> && !std::is_same_v<Result, FileOutcome>)
    {
        if (options.definitionSelection)
        {
            throw std::invalid_argument("Definition selection requires the file or independent-document entry");
        }
    }
    Result result;
    auto& report = result.report;
    report.materialCollection = std::is_same_v<Result, CollectionOutcome>;
    report.productDocument = std::is_same_v<Result, DocumentOutcome>;
    report.sourceStatus = report.semanticStatus = report.planarStatus = "NotRun";
    report.generalBrep = options.generalBrep;
    report.encoding = options.reportEncoding;
    report.repairRequest = options.faceOrientationRepair;
    report.definitionSelectionRequest = options.definitionSelection;
    report.sourceErrorIsolationRequest = options.sourceErrorIsolation;
    report.cylindricalBandRepairRequest = options.cylindricalBandRepair;
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
    // Translate the private result only at the existing public Outcome owner.
    // Successful publication has already committed and needs no further work.
    const auto publish = [&](detail::Result<void> accepted)
    {
        if (!accepted)
        {
            publication.Fail(accepted.error());
            publication.FinishFailure();
        }
    };
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
            if (options.cylindricalBandRepair && options.cylindricalBandRepair->sourceSha256 != raw.sha256)
            {
                throw detail::Failure{ Status::Unsupported,
                                       "CylindricalBandRepairInputMismatch",
                                       "Source repair does not bind the captured original bytes",
                                       {},
                                       "Repair" };
            }
            if (options.definitionSelection && options.definitionSelection->sourceSha256 != raw.sha256)
            {
                throw detail::Failure{ Status::Unsupported,
                                       "DefinitionSelectionInputMismatch",
                                       "Selection does not bind these captured original source bytes",
                                       {},
                                       "Selection" };
            }
            if (options.faceOrientationRepair && options.faceOrientationRepair->sourceSha256 != raw.sha256)
            {
                throw detail::Failure{ Status::Unsupported,
                                       "RepairPlanInputMismatch",
                                       "The repair plan does not bind these captured source bytes",
                                       {},
                                       "Repair" };
            }
        }
        if (options.generalBrep)
        {
            if constexpr (std::is_same_v<Result, DocumentOutcome> || std::is_same_v<Result, FileOutcome>)
            {
                if (options.sourceErrorIsolation)
                {
                    const auto& scope = *options.definitionSelection;
                    const auto& budgets = *options.sourceErrorIsolation;
                    report.generalSource = step_semantics::CheckSelectedIndependentProducts(
                        raw,
                        options.semanticLimits,
                        options.sourcePolicy,
                        { scope.sourceSha256, scope.definitionIds, budgets.maximumFailures, budgets.maximumInfluenceVisits },
                        options.schemaInterpretation,
                        options.unitInterpretation,
                        options.complexMappingInterpretation
                    );
                }
                else
                {
                    report.generalSource = step_semantics::CheckIndependentProducts(
                        raw,
                        options.semanticLimits,
                        options.sourcePolicy,
                        options.schemaInterpretation,
                        options.unitInterpretation,
                        options.complexMappingInterpretation,
                        options.cylindricalBandRepair ?
                            std::span<const step_semantics::CylindricalBandRepairDeferral>(options.cylindricalBandRepair->faces) :
                            std::span<const step_semantics::CylindricalBandRepairDeferral>{}
                    );
                }
            }
            else
            {
                report.generalSource = step_semantics::Check(
                    raw,
                    options.semanticLimits,
                    options.sourcePolicy,
                    step_semantics::GeometryScope::GeneralBrep,
                    options.schemaInterpretation,
                    options.unitInterpretation,
                    options.complexMappingInterpretation,
                    options.cylindricalBandRepair ?
                        std::span<const step_semantics::CylindricalBandRepairDeferral>(options.cylindricalBandRepair->faces) :
                        std::span<const step_semantics::CylindricalBandRepairDeferral>{}
                );
            }
            const auto& checked = *report.generalSource;
            report.sourceSchema = checked.schemaSelection;
            report.semanticStatus = step_semantics::StatusName(checked.status);
            report.planarStatus = "NotApplicable";
            report.diagnostics = checked.diagnostics;
            if (!checked.snapshot && !checked.document)
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
            detail::ValidateRepairTargets(checked, options, execution);
            if constexpr (std::is_same_v<Result, DocumentOutcome>)
            {
                if (!checked.document)
                {
                    throw std::logic_error("QualifiedProductDocumentRequired");
                }
                publish(publication.AcceptDocument(detail::BuildDocument(checked.document, options, report, execution)));
            }
            else if constexpr (std::is_same_v<Result, FileOutcome>)
            {
                if (!checked.document)
                {
                    throw std::logic_error("QualifiedProductDocumentRequired");
                }
                if (checked.document->PartCount() > 1 || options.definitionSelection)
                {
                    report.productDocument = true;
                    publish(publication.AcceptDocument(detail::BuildDocument(checked.document, options, report, execution)));
                }
                else
                {
                    // Select from the one qualified inventory, without a
                    // retry/re-read. One definition keeps the established
                    // builder, including associated physical geometry.
                    const auto single = checked.document->Part(0);
                    if (single->PartDefinition().shapes.size() > 1)
                    {
                        report.materialCollection = true;
                        publish(publication.AcceptCollection(single, detail::BuildMaterialCollection(single, options, report, execution)));
                    }
                    else
                    {
                        publish(publication.Accept(detail::BuildGeneral(single, options, report, execution)));
                    }
                }
            }
            else if constexpr (std::is_same_v<Result, CollectionOutcome>)
            {
                publish(publication.AcceptCollection(checked.snapshot, detail::BuildMaterialCollection(checked.snapshot, options, report, execution)));
            }
            else if constexpr (std::is_same_v<Result, PartOutcome>)
            {
                if (checked.snapshot->PartDefinition().shapes.size() > 1)
                {
                    report.materialCollection = true;
                    publish(
                        publication.AcceptCollection(checked.snapshot, detail::BuildMaterialCollection(checked.snapshot, options, report, execution))
                    );
                }
                else
                {
                    publish(publication.Accept(detail::BuildGeneral(checked.snapshot, options, report, execution)));
                }
            }
            else
            {
                publish(publication.Accept(detail::BuildGeneral(checked.snapshot, options, report, execution)));
            }
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
            publish(publication.Accept(detail::Build(plan, options, report, execution)));
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
CollectionOutcome ImportMaterialCollectionBytes(std::string bytes, const Options& options)
{
    const Options frozen = options;
    Validate(frozen);
    if (!frozen.generalBrep)
    {
        throw std::invalid_argument("Material collection requires the general source path");
    }
    return Import<CollectionOutcome>(
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
CollectionOutcome ImportMaterialCollectionFile(const std::filesystem::path& path, const Options& options)
{
    const Options frozen = options;
    const auto capturedPath = path;
    Validate(frozen);
    if (!frozen.generalBrep)
    {
        throw std::invalid_argument("Material collection requires the general source path");
    }
    return Import<CollectionOutcome>(
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
PartOutcome ImportPartFile(const std::filesystem::path& path, const Options& options)
{
    const Options frozen = options;
    const auto capturedPath = path;
    Validate(frozen);
    if (!frozen.generalBrep)
    {
        throw std::invalid_argument("Part import requires the general source path");
    }
    return Import<PartOutcome>(
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
PartOutcome ImportPartBytes(std::string bytes, const Options& options)
{
    const Options frozen = options;
    Validate(frozen);
    if (!frozen.generalBrep)
    {
        throw std::invalid_argument("Part import requires the general source path");
    }
    return Import<PartOutcome>(
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
FileOutcome ImportModelFile(const std::filesystem::path& path, const Options& options)
{
    const Options frozen = options;
    const auto capturedPath = path;
    Validate(frozen);
    if (!frozen.generalBrep)
    {
        throw std::invalid_argument("Whole-file model import requires the general source path");
    }
    return Import<FileOutcome>(
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
FileOutcome ImportModelBytes(std::string bytes, const Options& options)
{
    const Options frozen = options;
    Validate(frozen);
    if (!frozen.generalBrep)
    {
        throw std::invalid_argument("Whole-file model import requires the general source path");
    }
    return Import<FileOutcome>(
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
DocumentOutcome ImportDocumentFile(const std::filesystem::path& path, const Options& options)
{
    const Options frozen = options;
    const auto capturedPath = path;
    Validate(frozen);
    if (!frozen.generalBrep)
    {
        throw std::invalid_argument("Document import requires the general source path");
    }
    return Import<DocumentOutcome>(
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
DocumentOutcome ImportDocumentBytes(std::string bytes, const Options& options)
{
    const Options frozen = options;
    Validate(frozen);
    if (!frozen.generalBrep)
    {
        throw std::invalid_argument("Document import requires the general source path");
    }
    return Import<DocumentOutcome>(
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
const char* StatusName(Status status)
{
    switch (status)
    {
        case Status::Accepted:
            return "Accepted";
        case Status::AcceptedUnderInterpretation:
            return "AcceptedUnderInterpretation";
        case Status::AcceptedWithRepairs:
            return "AcceptedWithRepairs";
        case Status::AcceptedWithSourceIsolation:
            return "AcceptedWithSourceIsolation";
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
