// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSource.h"

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
        // ReportByteLimit counts actual emitted bytes, including the final LF.
        if (_setmode(_fileno(stdout), _O_BINARY) == -1)
        {
            throw std::runtime_error("Cannot configure binary stdout");
        }
#endif
        auto limits = step_source::Limits::PlanarFixtures();
        const std::unordered_map<std::string_view, std::size_t*> options = { { "--max-input-bytes", &limits.inputBytes },
                                                                             { "--max-records", &limits.records },
                                                                             { "--max-values", &limits.values },
                                                                             { "--max-references", &limits.references },
                                                                             { "--max-depth", &limits.nesting },
                                                                             { "--max-string-bytes", &limits.stringBytes },
                                                                             { "--max-record-bytes", &limits.recordBytes },
                                                                             { "--max-report-bytes", &limits.reportBytes },
                                                                             { "--max-id-digits", &limits.idDigits } };
        bool selectedLimits = false;
        auto headerNamePolicy = step_source::HeaderNamePolicy::StrictAscii;
        auto dataStringPolicy = step_source::DataStringPolicy::StrictAscii;
        auto carrierInterpretation = step_source::CarrierInterpretation::DeclaredOnly;
        std::string input;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view argument(argv[i]);
            if (argument == "--fixture-limits")
            {
                selectedLimits = true;
            }
            else if (argument == "--preserve-opaque-header-name" || argument == "--preserve-opaque-file-name-text")
            {
                const auto headerChoice = argument == "--preserve-opaque-header-name" ? step_source::HeaderNamePolicy::PreserveOpaqueBytes :
                                                                                        step_source::HeaderNamePolicy::PreserveFileNameText;
                if (headerNamePolicy != step_source::HeaderNamePolicy::StrictAscii && headerNamePolicy != headerChoice)
                {
                    throw std::invalid_argument("Choose one FILE_NAME preservation policy");
                }
                headerNamePolicy = headerChoice;
            }
            else if (argument == "--assume-ascii-invariant-data" || argument == "--preserve-opaque-data-strings")
            {
                const auto choice = argument == "--assume-ascii-invariant-data" ? step_source::DataStringPolicy::PreserveAsciiInvariantBytes :
                                                                                  step_source::DataStringPolicy::PreserveOpaqueBytes;
                if (dataStringPolicy != step_source::DataStringPolicy::StrictAscii && dataStringPolicy != choice)
                {
                    throw std::invalid_argument("Choose one DATA text policy");
                }
                dataStringPolicy = choice;
            }
            else if (argument == "--interpret-level1-as-corrected")
            {
                if (carrierInterpretation != step_source::CarrierInterpretation::DeclaredOnly &&
                    carrierInterpretation != step_source::CarrierInterpretation::Level1AsCorrectedEncoding)
                {
                    throw std::invalid_argument("Choose one carrier interpretation");
                }
                carrierInterpretation = step_source::CarrierInterpretation::Level1AsCorrectedEncoding;
            }
            else if (argument == "--interpret-level1-with-lowercase-hex")
            {
                if (carrierInterpretation != step_source::CarrierInterpretation::DeclaredOnly &&
                    carrierInterpretation != step_source::CarrierInterpretation::Level1AsCorrectedEncodingWithLowercaseHex)
                {
                    throw std::invalid_argument("Choose one carrier interpretation");
                }
                carrierInterpretation = step_source::CarrierInterpretation::Level1AsCorrectedEncodingWithLowercaseHex;
            }
            else if (options.count(argument))
            {
                if (++i == argc)
                {
                    throw std::invalid_argument("Missing limit value");
                }
                const std::string_view number(argv[i]);
                std::size_t value = 0;
                const auto parsed = std::from_chars(number.data(), number.data() + number.size(), value);
                if (parsed.ec != std::errc() || parsed.ptr != number.data() + number.size())
                {
                    throw std::invalid_argument("Limit must be a nonnegative decimal integer");
                }
                *options.at(argument) = value;
            }
            else if (input.empty() && argument.substr(0, 2) != "--")
            {
                input = argument;
            }
            else
            {
                throw std::invalid_argument("Unknown or repeated input argument");
            }
        }
        if (!selectedLimits || input.empty())
        {
            std::cerr << "Usage: step_inspect --fixture-limits <input.step> [--max-... N]\n";
            return 64;
        }
        const auto outcome = step_source::InspectFile(std::filesystem::path(input), limits, headerNamePolicy, dataStringPolicy, carrierInterpretation);
        std::string report;
        if (!step_source::InspectJson(outcome, report))
        {
            std::cerr << "ReportByteLimit: no complete inspect report was published\n";
            return 5;
        }
        std::cout << report;
        if (!std::cout)
        {
            return 6;
        }
        switch (outcome.status)
        {
            case step_source::Status::Inspected:
                return 0;
            case step_source::Status::Rejected:
                return 1;
            case step_source::Status::Unsupported:
                return 2;
            case step_source::Status::ResourceLimit:
                return 3;
            case step_source::Status::IoFailure:
                return 4;
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
