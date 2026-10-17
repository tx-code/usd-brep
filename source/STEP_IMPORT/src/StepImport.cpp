// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportExecution.h"
#include "StepImportPlan.h"

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
Options Options::PlanarFixture()
{
    return { step_source::Limits::PlanarFixtures(), {}, {}, Quality::PlanarFixture(), {}, {} };
}
namespace
{
void Validate(const Options& options)
{
    const auto& q = options.quality;
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
}
void Diagnostic(Report& report, const detail::Failure& failure)
{
    step_semantics::Diagnostic diagnostic;
    diagnostic.source.key = failure.key;
    diagnostic.source.stage = failure.stage;
    diagnostic.source.message = failure.message;
    diagnostic.classification = failure.status == Status::Cancelled     ? "Cancelled" :
                                failure.status == Status::Unsupported   ? "TargetUnsupported" :
                                failure.status == Status::ResourceLimit ? "ResourceLimit" :
                                failure.status == Status::Indeterminate ? "Indeterminate" :
                                                                          "NativeCandidateFailure";
    if (failure.stage == "Execution" && failure.status == Status::Unsupported)
    {
        diagnostic.classification = "ExecutionUnsupported";
    }
    if (failure.stage == "Execution" && failure.status == Status::BuildFailure)
    {
        diagnostic.classification = "ExecutionFailure";
    }
    diagnostic.responsibility = failure.stage == "Execution" ? "in-process cooperative execution" :
                                                               "native candidate preparation/construction/postcheck; Repair=None";
    if (failure.record && report.source)
    {
        const auto& record = report.source->Records().at(*failure.record);
        diagnostic.source.recordId = record.id;
        diagnostic.source.primary = record.span;
    }
    report.diagnostics.push_back(std::move(diagnostic));
}
template <class Capture>
Outcome Import(const Options& options, Capture capture)
{
    Outcome result;
    auto& report = result.report;
    report.sourceStatus = report.semanticStatus = report.planarStatus = "NotRun";
    step_source::InspectOutcome raw;
    raw.limits = options.sourceLimits;
    detail::Execution execution(options, report);
    std::shared_ptr<detail::ModelData> candidate;
    std::shared_ptr<const ModelSnapshot> prepared;
    try
    {
        execution.Check(Phase::BeforeCapture);
        raw = capture();
        report.source = raw.source;
        report.captured = raw.captured;
        report.sourceSha256 = raw.sha256;
        report.sourceStatus = step_source::StatusName(raw.status);
        if (raw.status == step_source::Status::Inspected)
        {
            execution.Check(Phase::Captured);
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
            candidate = detail::Build(plan, options, report, execution);
            prepared = detail::Access::Snapshot(std::move(candidate));
            execution.Check(Phase::NativeReady);
            result.status = Status::Accepted;
            execution.Check(Phase::BeforeReport);
            detail::Serialize(result, options.quality, raw, std::min(options.nativeLimits.reportBytes, raw.limits.reportBytes), report.json);
            execution.Check(Phase::ReportReady);
            execution.Check(Phase::BeforeCommit);
            detail::Serialize(result, options.quality, raw, std::min(options.nativeLimits.reportBytes, raw.limits.reportBytes), report.json);
            execution.Commit();
            result.model = std::move(prepared); // No allocation, callback or native operation after the successful atomic commit.
            return result;
        }
    }
    catch (const detail::Failure& failure)
    {
        result.status = failure.status;
        Diagnostic(report, failure);
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
        result.status = Status::Indeterminate;
        Diagnostic(report, { result.status, error.what(), "Bounded native numerical evidence is unestablished", {} });
    }
    catch (const std::domain_error& error)
    {
        result.status = Status::Indeterminate;
        Diagnostic(report, { result.status, error.what(), "Native derived evidence is unestablished", {} });
    }
    catch (const std::exception& error)
    {
        result.status = Status::BuildFailure;
        Diagnostic(report, { result.status, "NativeReturnOrConstructionFailure", error.what(), {} });
    }
    catch (...)
    {
        result.status = Status::BuildFailure;
        Diagnostic(report, { result.status, "UnknownImportFailure", "Unknown internal exception; no source-invalid claim", {}, "Execution" });
    }
    const bool held = bool(candidate) || bool(prepared);
    prepared.reset();
    candidate.reset();
    report.candidateDiscarded = report.candidateDiscarded || held;
    try
    {
        detail::Serialize(result, options.quality, raw, std::min(options.nativeLimits.reportBytes, raw.limits.reportBytes), report.json);
    }
    catch (const detail::Failure& failure)
    {
        if (result.status != Status::Cancelled)
        {
            result.status = failure.status;
        }
        Diagnostic(report, failure);
        report.json.clear();
    }
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
            return step_source::InspectBytes(std::move(bytes), frozen.sourceLimits);
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
            return step_source::InspectFile(capturedPath, frozen.sourceLimits);
        }
    );
}
const char* StatusName(Status status)
{
    switch (status)
    {
        case Status::Accepted:
            return "Accepted";
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
