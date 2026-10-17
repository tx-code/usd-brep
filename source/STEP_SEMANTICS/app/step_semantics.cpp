// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSemantics.h"

#include <charconv>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#if defined(_WIN32)
#    include <fcntl.h>
#    include <io.h>
#endif

int main(int argc, char** argv)
{
    try
    {
#if defined(_WIN32)
        if (_setmode(_fileno(stdout), _O_BINARY) == -1)
        {
            throw std::runtime_error("Cannot configure binary stdout");
        }
#endif
        auto limits = step_source::Limits::PlanarFixtures();
        step_semantics::Limits semanticLimits;
        auto policy = step_semantics::SourcePolicy::Strict;
        auto geometryScope = step_semantics::GeometryScope::ExplicitOuterBounds;
        const std::unordered_map<std::string_view, std::size_t*> options = { { "--max-input-bytes", &limits.inputBytes },
                                                                             { "--max-records", &limits.records },
                                                                             { "--max-values", &limits.values },
                                                                             { "--max-references", &limits.references },
                                                                             { "--max-depth", &limits.nesting },
                                                                             { "--max-string-bytes", &limits.stringBytes },
                                                                             { "--max-record-bytes", &limits.recordBytes },
                                                                             { "--max-report-bytes", &limits.reportBytes },
                                                                             { "--max-id-digits", &limits.idDigits },
                                                                             { "--max-semantic-visits", &semanticLimits.dependencyVisits },
                                                                             { "--max-preserved-diagnostics",
                                                                               &semanticLimits.preservedDiagnostics } };
        bool selected = false;
        std::string path;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg(argv[i]);
            if (arg == "--fixture-limits")
            {
                selected = true;
            }
            else if (arg == "--interop-nongeometric")
            {
                policy = step_semantics::SourcePolicy::PreserveNonGeometric;
            }
            else if (arg == "--general-brep-source")
            {
                geometryScope = step_semantics::GeometryScope::GeneralBrep;
            }
            else if (options.count(arg))
            {
                if (++i == argc)
                {
                    throw std::invalid_argument("Missing limit value");
                }
                const std::string_view value(argv[i]);
                std::size_t limit = 0;
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), limit);
                if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size())
                {
                    throw std::invalid_argument("Invalid limit integer");
                }
                *options.at(arg) = limit;
            }
            else if (path.empty() && arg.substr(0, 2) != "--")
            {
                path = arg;
            }
            else
            {
                throw std::invalid_argument("Unknown/repeated input argument");
            }
        }
        if (!selected || path.empty())
        {
            std::cerr << "Usage: step_semantic_inspect --fixture-limits <input.step> [--max-... N]\n";
            return 64;
        }
        const auto source = step_source::InspectFile(std::filesystem::path(path), limits);
        const auto semantics = step_semantics::Check(source, semanticLimits, policy, geometryScope);
        std::string output;
        if (!step_semantics::InspectJson(source, semantics, output))
        {
            std::cerr << "ReportByteLimit: no complete semantic report was published\n";
            return 5;
        }
        std::cout << output;
        if (!std::cout)
        {
            return 6;
        }
        switch (semantics.status)
        {
            case step_semantics::Status::Checked:
            case step_semantics::Status::CheckedWithDiagnostics:
                return 0;
            case step_semantics::Status::Deferred:
                return 7;
            case step_semantics::Status::Rejected:
                return 1;
            case step_semantics::Status::Unsupported:
                return 2;
            case step_semantics::Status::ResourceLimit:
                return 3;
            case step_semantics::Status::SourceFailure:
                switch (source.status)
                {
                    case step_source::Status::Rejected:
                        return 1;
                    case step_source::Status::Unsupported:
                        return 2;
                    case step_source::Status::ResourceLimit:
                        return 3;
                    case step_source::Status::IoFailure:
                        return 4;
                    default:
                        return 6;
                }
        }
        return 6;
    }
    catch (const std::invalid_argument& error)
    {
        std::cerr << "InvalidArguments: " << error.what() << '\n';
        return 64;
    }
    catch (const std::exception& error)
    {
        std::cerr << "InfrastructureFailure: " << error.what() << '\n';
        return 6;
    }
}
