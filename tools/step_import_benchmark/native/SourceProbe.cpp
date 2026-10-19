// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "ProbeCommon.h"
#include "StepSource.h"
#ifdef STEP_BENCH_NATIVE
#    include "StepImport.h"
#endif

namespace
{
step_source::Limits SourceLimits(std::size_t maximum)
{
    if (maximum > 64 * 1024 * 1024)
    {
        throw std::invalid_argument("Current source reader safety ceiling is 64 MiB");
    }
    return { maximum, 1000000, 2000000, 2000000, 128, 1024 * 1024, 16 * 1024 * 1024, 16 * 1024 * 1024, 1024 };
}
bench::Trial Run(const bench::Args& args)
{
    bench::Trial row;
    const auto begin = bench::Clock::now();
    auto cleanup = begin;
    if (args.mode == "source")
    {
        {
            const auto result = step_source::InspectFile(args.input, SourceLimits(args.maximumInput));
            row.operationMs = bench::Ms(begin);
            row.status = step_source::StatusName(result.status);
            row.records = result.statistics.records;
            if (!result.diagnostics.empty())
            {
                row.key = result.diagnostics.front().key;
                row.stage = result.diagnostics.front().stage;
            }
            row.extra = ",\"source_sha256\":" + bench::Q(result.sha256);
            row.aliveMemory = bench::Memory();
            cleanup = bench::Clock::now();
        }
    }
#ifdef STEP_BENCH_NATIVE
    else if (args.mode == "import")
    {
        {
            auto options = step_import::Options::PlanarFixture();
            if (args.schemaInterpretation == "UnregisteredAp214As2010")
            {
                options.schemaInterpretation = step_semantics::SchemaInterpretation::UnregisteredAp214As2010;
            }
            options.sourceLimits = SourceLimits(args.maximumInput);
            if (bench::GeneralQuality(args.quality))
            {
                // This is the public general importer, including its required
                // source checks, native validators, measured queries and report.
                // The larger report cap changes capacity, not checked accuracy.
                options.generalBrep = true;
                options.quality = args.quality == "Q-AP214-development-mm-01"          ? step_import::Quality::GeneralDevelopment() :
                                  args.quality == "Q-AP214-work-tolerance-study-mm-01" ? step_import::Quality::GeneralWorkToleranceStudy() :
                                                                                         step_import::Quality::GeneralToleranceStudy();
                options.sourceLimits.reportBytes = 256 * 1024 * 1024;
                options.nativeLimits.reportBytes = options.sourceLimits.reportBytes;
                options.nativeLimits.nativeCalls = 100000;
                options.semanticLimits.dependencyVisits = 10000000;
                options.semanticLimits.preservedDiagnostics = 16384;
            }
            if (args.sourcePolicy == "PreserveNonGeometric")
            {
                options.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
            }
            if (args.headerNamePolicy == "PreserveOpaqueBytes")
            {
                options.headerNamePolicy = step_source::HeaderNamePolicy::PreserveOpaqueBytes;
            }
            if (args.dataStringPolicy == "PreserveOpaqueBytes")
            {
                options.dataStringPolicy = step_source::DataStringPolicy::PreserveOpaqueBytes;
            }
            options.restrictUnusedSupportDomains = args.restrictUnusedSupportDomains.value_or(false);
            std::vector<std::pair<std::size_t, double>> intervals;
            std::optional<std::pair<std::size_t, bench::Clock::time_point>> previous;
            if (args.nativeBoundaryTiming.value_or(false))
            {
                intervals.reserve(options.nativeLimits.nativeCalls);
            }
            options.execution.progress = [&](step_import::Progress progress)
            {
                if (args.nativeBoundaryTiming.value_or(false) &&
                    (progress.phase == step_import::Phase::BeforeNativeCall || progress.phase == step_import::Phase::NativeReady))
                {
                    const auto now = bench::Clock::now();
                    if (previous && progress.nativeCalls > previous->first)
                    {
                        // These public counters describe completed calls at the
                        // safe boundary, before the next charged operation. An
                        // interval also includes intervening read-only work;
                        // it is not a pure kernel-call timer or a new progress phase.
                        intervals.emplace_back(previous->first + 1, std::chrono::duration<double, std::milli>(now - previous->second).count());
                    }
                    if (progress.phase == step_import::Phase::BeforeNativeCall)
                    {
                        previous = std::pair{ progress.nativeCalls, now };
                    }
                    else
                    {
                        previous.reset();
                    }
                }
                if (progress.phase != step_import::Phase::BeforeNativeCall)
                {
                    row.milestones.try_emplace(step_import::PhaseName(progress.phase), bench::Ms(begin));
                }
            };
            const auto result = step_import::ImportFile(args.input, options);
            row.operationMs = bench::Ms(begin);
            row.status = step_import::StatusName(result.status);
            row.hasModel = bool(result.model);
            if (!result.report.diagnostics.empty())
            {
                row.key = result.report.diagnostics.front().source.key;
                row.stage = result.report.diagnostics.front().source.stage;
            }
            if (result.model)
            {
                const auto infoStart = bench::Clock::now();
                const auto info = result.model->Info();
                row.timings["model_info_after_import"] = bench::Ms(infoStart);
                std::ostringstream model;
                model << "{\"faces\":" << info.faces << ",\"edges\":" << info.edges << ",\"vertices\":" << info.vertices
                      << ",\"area_mm2\":" << bench::N(info.areaMm2)
                      << ",\"volume_mm3\":" << (info.materialVolumeMm3 ? bench::N(*info.materialVolumeMm3) : "null") << '}';
                row.model = model.str();
            }
            const auto preserved = result.report.generalSource && result.report.generalSource->snapshot ?
                                       result.report.generalSource->snapshot->PreservedDiagnostics().size() :
                                       0;
            row.records = result.report.source ? result.report.source->Records().size() : 0;
            row.extra =
                ",\"source_status\":" + bench::Q(result.report.sourceStatus) + ",\"semantic_status\":" + bench::Q(result.report.semanticStatus) +
                ",\"planar_status\":" + bench::Q(result.report.planarStatus) + ",\"source_sha256\":" + bench::Q(result.report.sourceSha256) +
                ",\"native_calls\":" + std::to_string(result.report.nativeCalls) + ",\"report_bytes\":" + std::to_string(result.report.json.size()) +
                ",\"preserved_source_diagnostics\":" + std::to_string(preserved) + ",\"settings\":{\"quality_id\":" + bench::Q(options.quality.id) +
                ",\"source_policy\":" + bench::Q(options.sourcePolicy == step_semantics::SourcePolicy::Strict ? "Strict" : "PreserveNonGeometric") +
                ",\"general_brep\":" + (options.generalBrep ? "true" : "false") +
                ",\"report_limit_bytes\":" + std::to_string(options.nativeLimits.reportBytes) +
                ",\"native_call_limit\":" + std::to_string(options.nativeLimits.nativeCalls) + ",\"consistency_mm\":" +
                bench::N(double(options.quality.consistencyMm.Numerator()) / double(options.quality.consistencyMm.Denominator())) +
                ",\"derived_uv_mm\":" +
                bench::N(double(options.quality.derivedUvMm.Numerator()) / double(options.quality.derivedUvMm.Denominator())) +
                ",\"work_tolerance_mm\":" +
                bench::N(double(options.quality.initialWorkToleranceMm.Numerator()) / double(options.quality.initialWorkToleranceMm.Denominator())) +
                ",\"operation_scope\":\"public ImportFile including source validation, construction, postchecks, integrals, mandatory report and commit\"}";
            if (args.restrictUnusedSupportDomains.has_value())
            {
                row.extra += ",\"support_domain_restriction_requested\":" + std::string(options.restrictUnusedSupportDomains ? "true" : "false");
            }
            if (args.quality == "Q-AP214-work-tolerance-study-mm-01")
            {
                row.extra += ",\"native_work_budget\":{\"maximum_mm\":" +
                             bench::N(
                                 double(options.quality.maximumWorkToleranceMm.Numerator()) /
                                 double(options.quality.maximumWorkToleranceMm.Denominator())
                             ) +
                             ",\"growth_mm\":" +
                             bench::N(
                                 double(options.quality.toleranceGrowthMm.Numerator()) / double(options.quality.toleranceGrowthMm.Denominator())
                             ) +
                             "}";
            }
            if (!args.schemaInterpretation.empty())
            {
                const bool applied = result.report.sourceSchema &&
                                     result.report.sourceSchema->appliedInterpretation != step_semantics::SchemaInterpretation::DeclaredOnly;
                row.extra += ",\"requested_schema_interpretation\":" + bench::Q(args.schemaInterpretation) +
                             ",\"schema_interpretation_applied\":" + (applied ? "true" : "false");
                if (applied)
                {
                    row.extra += ",\"source_binding_verified\":false";
                }
            }
            if (!args.headerNamePolicy.empty())
            {
                row.extra += ",\"header_name_policy\":" + bench::Q(args.headerNamePolicy);
            }
            if (!args.dataStringPolicy.empty())
            {
                row.extra += ",\"data_string_policy\":" + bench::Q(args.dataStringPolicy);
            }
            if (args.nativeBoundaryTiming.value_or(false))
            {
                std::ostringstream profile;
                profile
                    << ",\"native_boundary_profile\":{\"scope\":\"time between public native-call safe boundaries; includes read-only work and profiling overhead\","
                       "\"complete\":"
                    << (row.milestones.contains("NativeReady") ? "true" : "false") << ",\"intervals\":[";
                bool first = true;
                for (const auto& [call, elapsed] : intervals)
                {
                    if (!first)
                    {
                        profile << ',';
                    }
                    first = false;
                    profile << "{\"call\":" << call << ",\"elapsed_ms\":" << bench::N(elapsed) << '}';
                }
                profile << "]}";
                row.extra += profile.str();
            }
            row.aliveMemory = bench::Memory();
            cleanup = bench::Clock::now();
        }
    }
#endif
    else
    {
        throw std::invalid_argument("Unsupported self-reader probe mode");
    }
    row.cleanupMs = bench::Ms(cleanup);
    return row;
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
#ifdef STEP_BENCH_NATIVE
    return bench::Main(bench::Tokens(argc, argv), "self-native", "{\"SMLib\":" + bench::Q(bench::Loaded(L"SMLib.dll")) + "}", Run);
#else
    return bench::Main(bench::Tokens(argc, argv), "self-source", "{}", Run);
#endif
}
