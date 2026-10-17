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
            options.sourceLimits = SourceLimits(args.maximumInput);
            options.execution.progress = [&](step_import::Progress progress)
            {
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
                const auto info = result.model->Info();
                std::ostringstream model;
                model << "{\"faces\":" << info.faces << ",\"edges\":" << info.edges << ",\"vertices\":" << info.vertices
                      << ",\"area_mm2\":" << bench::N(info.areaMm2)
                      << ",\"volume_mm3\":" << (info.materialVolumeMm3 ? bench::N(*info.materialVolumeMm3) : "null") << '}';
                row.model = model.str();
            }
            row.records = result.report.source ? result.report.source->Records().size() : 0;
            row.extra = ",\"source_status\":" + bench::Q(result.report.sourceStatus) +
                        ",\"semantic_status\":" + bench::Q(result.report.semanticStatus) +
                        ",\"planar_status\":" + bench::Q(result.report.planarStatus) + ",\"source_sha256\":" + bench::Q(result.report.sourceSha256) +
                        ",\"native_calls\":" + std::to_string(result.report.nativeCalls) +
                        ",\"report_bytes\":" + std::to_string(result.report.json.size());
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
