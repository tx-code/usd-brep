// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepSourceReport.h"

#include <charconv>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

namespace
{
void Number(step_source::report::Writer& writer, double value)
{
    std::array<char, 64> buffer{};
    const auto converted = std::to_chars(
        buffer.data(),
        buffer.data() + buffer.size(),
        value,
        std::chars_format::general,
        std::numeric_limits<double>::max_digits10
    );
    if (!std::isfinite(value) || converted.ec != std::errc{})
    {
        throw std::runtime_error("Unreportable native query value");
    }
    writer.Add(std::string_view(buffer.data(), static_cast<std::size_t>(converted.ptr - buffer.data())));
}
void Point(step_source::report::Writer& writer, const step_import::Point& point)
{
    writer.Add("[");
    for (std::size_t i = 0; i < 3; ++i)
    {
        if (i)
        {
            writer.Add(",");
        }
        Number(writer, point[i]);
    }
    writer.Add("]");
}
int Exit(step_import::Status status)
{
    using step_import::Status;
    switch (status)
    {
        case Status::Accepted:
            return 0;
        case Status::Rejected:
            return 1;
        case Status::Unsupported:
            return 2;
        case Status::ResourceLimit:
            return 3;
        case Status::SourceFailure:
            return 4;
        case Status::BuildFailure:
            return 6;
        case Status::Deferred:
            return 7;
        case Status::Indeterminate:
            return 8;
        case Status::PostcheckFailure:
            return 9;
        case Status::Cancelled:
            return 10;
    }
    return 6;
}
} // namespace
int main(int argc, char** argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    try
    {
        auto options = step_import::Options::PlanarFixture();
        std::optional<step_import::Point> query;
        std::string file;
        std::optional<std::filesystem::path> nativeOutput;
        bool limits = false, quality = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg(argv[i]);
            if (arg == "--fixture-limits")
            {
                limits = true;
            }
            else if (arg == "--quality" && i + 1 < argc)
            {
                const std::string_view id(argv[++i]);
                quality = id == "Q-P0-mm-01" || id == "Q-AP214-development-mm-01" || id == "Q-AP214-tolerance-study-mm-01";
                if (id == "Q-AP214-development-mm-01" || id == "Q-AP214-tolerance-study-mm-01")
                {
                    options.generalBrep = true;
                    options.quality = id == "Q-AP214-development-mm-01" ? step_import::Quality::GeneralDevelopment() :
                                                                          step_import::Quality::GeneralToleranceStudy();
                }
                if (!quality)
                {
                    throw std::invalid_argument("Unsupported explicit quality identifier");
                }
            }
            else if (arg == "--interop-nongeometric")
            {
                options.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
            }
            else if (arg == "--write-native" && i + 1 < argc)
            {
                nativeOutput = std::filesystem::path(argv[++i]);
            }
            else if (arg == "--query" && i + 3 < argc)
            {
                query = step_import::Point{};
                for (auto& value : *query)
                {
                    const std::string_view token(argv[++i]);
                    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
                    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || !std::isfinite(value))
                    {
                        throw std::invalid_argument("Finite query coordinates required");
                    }
                }
            }
            else if ((arg.starts_with("--max-") || arg == "--deadline-ms" || arg == "--hard-memory-bytes") && i + 1 < argc)
            {
                std::size_t value = 0;
                const std::string_view token(argv[++i]);
                const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
                if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
                {
                    throw std::invalid_argument("Unsigned limit required");
                }
                const std::pair<std::string_view, std::size_t*> counters[] = { { "--max-native-calls", &options.nativeLimits.nativeCalls },
                                                                               { "--max-report-bytes", &options.nativeLimits.reportBytes },
                                                                               { "--max-input-bytes", &options.sourceLimits.inputBytes },
                                                                               { "--max-records", &options.sourceLimits.records },
                                                                               { "--max-references", &options.sourceLimits.references },
                                                                               { "--max-values", &options.sourceLimits.values },
                                                                               { "--max-nesting", &options.sourceLimits.nesting },
                                                                               { "--max-string-bytes", &options.sourceLimits.stringBytes },
                                                                               { "--max-record-bytes", &options.sourceLimits.recordBytes },
                                                                               { "--max-id-digits", &options.sourceLimits.idDigits },
                                                                               { "--max-semantic-visits", &options.semanticLimits.dependencyVisits },
                                                                               { "--max-preserved-diagnostics",
                                                                                 &options.semanticLimits.preservedDiagnostics },
                                                                               { "--max-planar-work", &options.planarLimits.work } };
                bool assigned = false;
                for (const auto& [name, target] : counters)
                {
                    if (arg == name)
                    {
                        *target = value;
                        assigned = true;
                        break;
                    }
                }
                if (assigned)
                {
                    continue;
                }
                if (arg == "--max-process-private-bytes")
                {
                    options.nativeLimits.processPrivateBytes = value;
                }
                else if (arg == "--hard-memory-bytes")
                {
                    options.nativeLimits.hardMemoryBytes = value;
                }
                else if (arg == "--deadline-ms")
                {
                    if (value > 24 * 60 * 60 * 1000)
                    {
                        throw std::invalid_argument("CLI deadline must be within 24 hours");
                    }
                    options.execution.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(value);
                }
                else
                {
                    throw std::invalid_argument("Unknown declared limit");
                }
            }
            else if (!arg.starts_with("--") && file.empty())
            {
                file = arg;
            }
            else
            {
                throw std::invalid_argument("Unexpected argument");
            }
        }
        if (!limits || !quality || file.empty())
        {
            throw std::invalid_argument("Use --fixture-limits --quality Q-P0-mm-01 input.step [--query x y z]");
        }
        if (options.generalBrep)
        {
            // The general report embeds the full owning source inspection.
            // One explicit CLI report cap must bound both nested and outer output.
            options.sourceLimits.reportBytes = options.nativeLimits.reportBytes;
        }
        auto result = step_import::ImportFile(file, options);
        if (result.report.json.empty())
        {
            std::cerr << "Mandatory import report was not published\n";
            return 5;
        }
        std::string output = result.report.json;
        if (nativeOutput && result.model)
        {
            result.model->WriteNative(*nativeOutput);
        }
        if (query && result.model)
        {
            const auto closest = result.model->Closest(*query);
            const auto material = result.model->ClassifyMaterial(*query);
            const bool complete = step_source::report::Writer::Publish(
                options.nativeLimits.reportBytes,
                output,
                [&](step_source::report::Writer& writer)
                {
                    writer.Add("{\"import\":");
                    writer.Add(result.report.json);
                    writer.Add(",\"query\":{\"point_mm\":");
                    Point(writer, *query);
                    writer.Add(",\"closest_point_mm\":");
                    Point(writer, closest.pointMm);
                    writer.Add(",\"distance_mm\":");
                    Number(writer, closest.distanceMm);
                    writer.Add(",\"material_location\":");
                    if (material)
                    {
                        writer.String(step_import::MaterialPointClassName(*material));
                    }
                    else
                    {
                        writer.String("NotApplicable");
                    }
                    writer.Add("}}\n");
                }
            );
            if (!complete)
            {
                return 5;
            }
        }
        std::cout.write(output.data(), static_cast<std::streamsize>(output.size()));
        return Exit(result.status);
    }
    catch (const std::invalid_argument& error)
    {
        std::cerr << error.what() << '\n';
        return 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 6;
    }
}
