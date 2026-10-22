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
        bool independentProducts = false;
        auto geometryScope = step_semantics::GeometryScope::ExplicitOuterBounds;
        auto headerNamePolicy = step_source::HeaderNamePolicy::StrictAscii;
        auto dataStringPolicy = step_source::DataStringPolicy::StrictAscii;
        auto unitInterpretation = step_semantics::UnitInterpretation::DeclaredOnly;
        auto complexMappingInterpretation = step_semantics::ComplexMappingInterpretation::DeclaredOnly;
        auto interpretation = step_semantics::SchemaInterpretation::DeclaredOnly;
        auto carrierInterpretation = step_source::CarrierInterpretation::DeclaredOnly;
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
            else if (arg == "--independent-products")
            {
                independentProducts = true;
                geometryScope = step_semantics::GeometryScope::GeneralBrep;
            }
            else if (arg == "--general-brep-source")
            {
                geometryScope = step_semantics::GeometryScope::GeneralBrep;
            }
            else if (arg == "--interpret-unregistered-ap214-2010")
            {
                if (interpretation != step_semantics::SchemaInterpretation::DeclaredOnly &&
                    interpretation != step_semantics::SchemaInterpretation::UnregisteredAp214As2010)
                {
                    throw std::invalid_argument("Choose one schema interpretation");
                }
                interpretation = step_semantics::SchemaInterpretation::UnregisteredAp214As2010;
            }
            else if (arg == "--interpret-ap214-name-only-as-2010")
            {
                if (interpretation != step_semantics::SchemaInterpretation::DeclaredOnly &&
                    interpretation != step_semantics::SchemaInterpretation::Ap214NameOnlyAs2010)
                {
                    throw std::invalid_argument("Choose one schema interpretation");
                }
                interpretation = step_semantics::SchemaInterpretation::Ap214NameOnlyAs2010;
            }
            else if (arg == "--preserve-opaque-header-name" || arg == "--preserve-opaque-file-name-text")
            {
                const auto headerChoice = arg == "--preserve-opaque-header-name" ? step_source::HeaderNamePolicy::PreserveOpaqueBytes :
                                                                                   step_source::HeaderNamePolicy::PreserveFileNameText;
                if (headerNamePolicy != step_source::HeaderNamePolicy::StrictAscii && headerNamePolicy != headerChoice)
                {
                    throw std::invalid_argument("Choose one FILE_NAME preservation policy");
                }
                headerNamePolicy = headerChoice;
            }
            else if (arg == "--assume-ascii-invariant-data" || arg == "--preserve-opaque-data-strings")
            {
                const auto choice = arg == "--assume-ascii-invariant-data" ? step_source::DataStringPolicy::PreserveAsciiInvariantBytes :
                                                                             step_source::DataStringPolicy::PreserveOpaqueBytes;
                if (dataStringPolicy != step_source::DataStringPolicy::StrictAscii && dataStringPolicy != choice)
                {
                    throw std::invalid_argument("Choose one DATA text policy");
                }
                dataStringPolicy = choice;
            }
            else if (arg == "--complex-mapping-interpretation")
            {
                if (i + 1 == argc || complexMappingInterpretation != step_semantics::ComplexMappingInterpretation::DeclaredOnly)
                {
                    throw std::invalid_argument("One complex mapping interpretation required");
                }
                const std::string_view choice(argv[++i]);
                if (choice == "RegisteredFacetNames")
                {
                    complexMappingInterpretation = step_semantics::ComplexMappingInterpretation::RegisteredFacetNames;
                }
                else if (choice != "DeclaredOnly")
                {
                    throw std::invalid_argument("Unknown complex mapping interpretation");
                }
            }
            else if (arg == "--interpret-explicit-unit-dimensions")
            {
                unitInterpretation = step_semantics::UnitInterpretation::VerifiedExplicitUnitDimensions;
            }
            else if (arg == "--interpret-blank-level-as-corrected")
            {
                if (carrierInterpretation != step_source::CarrierInterpretation::DeclaredOnly &&
                    carrierInterpretation != step_source::CarrierInterpretation::BlankLevelAsCorrectedEncoding)
                {
                    throw std::invalid_argument("Choose one carrier interpretation");
                }
                carrierInterpretation = step_source::CarrierInterpretation::BlankLevelAsCorrectedEncoding;
            }
            else if (arg == "--interpret-level1-as-corrected")
            {
                if (carrierInterpretation != step_source::CarrierInterpretation::DeclaredOnly &&
                    carrierInterpretation != step_source::CarrierInterpretation::Level1AsCorrectedEncoding)
                {
                    throw std::invalid_argument("Choose one carrier interpretation");
                }
                carrierInterpretation = step_source::CarrierInterpretation::Level1AsCorrectedEncoding;
            }
            else if (arg == "--interpret-level1-with-lowercase-hex")
            {
                if (carrierInterpretation != step_source::CarrierInterpretation::DeclaredOnly &&
                    carrierInterpretation != step_source::CarrierInterpretation::Level1AsCorrectedEncodingWithLowercaseHex)
                {
                    throw std::invalid_argument("Choose one carrier interpretation");
                }
                carrierInterpretation = step_source::CarrierInterpretation::Level1AsCorrectedEncodingWithLowercaseHex;
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
        const auto source = step_source::InspectFile(std::filesystem::path(path), limits, headerNamePolicy, dataStringPolicy, carrierInterpretation);
        const auto semantics = independentProducts ? step_semantics::CheckIndependentProducts(
                                                         source,
                                                         semanticLimits,
                                                         policy,
                                                         interpretation,
                                                         unitInterpretation,
                                                         complexMappingInterpretation
                                                     ) :
                                                     step_semantics::Check(
                                                         source,
                                                         semanticLimits,
                                                         policy,
                                                         geometryScope,
                                                         interpretation,
                                                         unitInterpretation,
                                                         complexMappingInterpretation
                                                     );
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
