// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
// Windows types must be declared before Psapi; preserve the SDK include order.
// clang-format off
#    include <Windows.h>
#    include <Psapi.h>
// clang-format on
#endif

namespace bench
{
using Clock = std::chrono::steady_clock;
inline double Ms(Clock::time_point begin)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}
inline std::string Q(const std::string& value)
{
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : value)
    {
        if (c == '"' || c == '\\')
        {
            out << '\\' << c;
        }
        else if (c < 32)
        {
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c) << std::dec;
        }
        else
        {
            out << c;
        }
    }
    out << '"';
    return out.str();
}
inline std::string N(double value)
{
    if (!std::isfinite(value))
    {
        return "null";
    }
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}
inline std::string Utf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return { reinterpret_cast<const char*>(value.data()), value.size() };
}
inline std::string Loaded(const wchar_t* name)
{
#ifdef _WIN32
    wchar_t buffer[32768];
    const auto module = GetModuleHandleW(name);
    if (module && GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer))))
    {
        return Utf8(std::filesystem::path(buffer));
    }
#else
    (void)name;
#endif
    return {};
}
inline std::string Memory()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX memory{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
    {
        return "{\"process_private_bytes\":" + std::to_string(memory.PrivateUsage) +
               ",\"process_lifetime_peak_commit_bytes\":" + std::to_string(memory.PeakPagefileUsage) +
               ",\"process_lifetime_peak_working_set_bytes\":" + std::to_string(memory.PeakWorkingSetSize) + "}";
    }
#endif
    return "null";
}
struct Args
{
    std::filesystem::path input, output;
    std::string mode;
    std::string quality, sourcePolicy, headerNamePolicy; // Explicit native-import choices; empty preserves the strict P0 run.
    std::optional<bool> nativeBoundaryTiming; // Presence is distinct from enabled; never silently ignore a foreign engine's flag.
    std::size_t warmup = 0, repeat = 1, maximumInput = 64 * 1024 * 1024;
};
inline Args Parse(const std::vector<std::string>& tokens)
{
    Args args;
    for (std::size_t i = 1; i < tokens.size(); i += 2)
    {
        if (i + 1 == tokens.size())
        {
            throw std::invalid_argument("Every benchmark option requires a value");
        }
        const auto& key = tokens[i];
        const auto& value = tokens[i + 1];
        if (key == "--input" || key == "--output")
        {
            (key == "--input" ?
                 args.input :
                 args.output) = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(value.data()), value.size()));
        }
        else if (key == "--mode")
        {
            args.mode = value;
        }
        else if (key == "--quality" || key == "--source-policy" || key == "--header-name-policy")
        {
            if (value.empty())
            {
                throw std::invalid_argument("Empty explicit native configuration");
            }
            (key == "--quality" ? args.quality : key == "--source-policy" ? args.sourcePolicy : args.headerNamePolicy) = value;
        }
        else
        {
            std::size_t number = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
            {
                throw std::invalid_argument("Unsigned benchmark limit required");
            }
            if (key == "--warmup")
            {
                args.warmup = number;
            }
            else if (key == "--repeat")
            {
                args.repeat = number;
            }
            else if (key == "--max-input-bytes")
            {
                args.maximumInput = number;
            }
            else if (key == "--native-boundary-timing" && number <= 1)
            {
                args.nativeBoundaryTiming = number != 0;
            }
            else
            {
                throw std::invalid_argument("Unknown benchmark option");
            }
        }
    }
    if (args.input.empty() || args.output.empty() || args.mode.empty() || args.repeat == 0 || args.repeat > 50 || args.warmup > 5)
    {
        throw std::invalid_argument("Input, output, mode and bounded repeat count required");
    }
    return args;
}
struct Trial
{
    std::string status, key, stage, model = "null", extra;
    std::size_t records = 0;
    bool hasModel = false;
    std::optional<double> operationMs;
    double cleanupMs = 0;
    std::map<std::string, double> timings, milestones;
    std::string aliveMemory = "null";
};
inline void Times(std::ostream& out, const std::map<std::string, double>& times)
{
    out << '{';
    bool first = true;
    for (const auto& [key, value] : times)
    {
        out << (first ? "" : ",") << Q(key) << ':' << N(value);
        first = false;
    }
    out << '}';
}
template <class Run>
int Main(const std::vector<std::string>& tokens, const char* engine, const std::string& identity, Run run)
{
    try
    {
        const auto args = Parse(tokens);
        // Never accept a native policy flag and silently ignore it on the other
        // reader. Invalid policies are rejected even for an input-budget skip.
        if (!args.quality.empty() || !args.sourcePolicy.empty() || !args.headerNamePolicy.empty() || args.nativeBoundaryTiming.has_value())
        {
            if (std::string_view(engine) != "self-native" || args.mode != "import")
            {
                throw std::invalid_argument("Quality and source policy apply only to self-native import");
            }
            if ((!args.quality.empty() && args.quality != "Q-P0-mm-01" && args.quality != "Q-AP214-development-mm-01" &&
                 args.quality != "Q-AP214-tolerance-study-mm-01") ||
                (!args.sourcePolicy.empty() && args.sourcePolicy != "Strict" && args.sourcePolicy != "PreserveNonGeometric") ||
                (!args.headerNamePolicy.empty() && args.headerNamePolicy != "StrictAscii" && args.headerNamePolicy != "PreserveOpaqueBytes"))
            {
                throw std::invalid_argument("Unknown explicit native quality or source policy");
            }
            if (args.headerNamePolicy == "PreserveOpaqueBytes" &&
                (args.sourcePolicy != "PreserveNonGeometric" ||
                 (args.quality != "Q-AP214-development-mm-01" && args.quality != "Q-AP214-tolerance-study-mm-01")))
            {
                throw std::invalid_argument("Opaque header names require explicit general quality and nongeometric interoperability");
            }
        }
        const auto bytes = std::filesystem::file_size(args.input);
        std::ostringstream out;
        out << "{\"format\":\"step-import-benchmark.v1\",\"engine\":" << Q(engine) << ",\"mode\":" << Q(args.mode) << ",\"build_mode\":\""
#ifdef NDEBUG
            << "Release"
#else
            << "Debug"
#endif
            << "\",\"build_id\":" << Q(STEP_BENCH_BUILD_ID) << ",\"input\":" << Q(Utf8(args.input)) << ",\"input_bytes\":" << bytes
            << ",\"max_input_bytes\":" << args.maximumInput << ",\"identity\":" << identity
            << ",\"memory_scope\":\"process absolute and lifetime peaks; not per-import allocation\",\"trials\":[";
        for (std::size_t i = 0; i < args.warmup + args.repeat; ++i)
        {
            const auto before = Memory();
            Trial row;
            if (bytes > args.maximumInput)
            {
                row.status = "NotRunInputBudget";
                row.key = "BenchmarkInputBudget";
            }
            else
            {
                row = run(args);
            }
            out << (i ? "," : "") << "{\"warmup\":" << (i < args.warmup ? "true" : "false") << ",\"status\":" << Q(row.status)
                << ",\"diagnostic_key\":" << Q(row.key) << ",\"diagnostic_stage\":" << Q(row.stage)
                << ",\"has_model\":" << (row.hasModel ? "true" : "false") << ",\"records\":" << row.records
                << ",\"operation_ms\":" << (row.operationMs ? N(*row.operationMs) : "null") << ",\"cleanup_ms\":" << N(row.cleanupMs)
                << ",\"memory_before\":" << before << ",\"memory_model_alive\":" << row.aliveMemory << ",\"memory_after\":" << Memory()
                << ",\"model\":" << row.model << ",\"timings_ms\":";
            Times(out, row.timings);
            out << ",\"milestones_ms_since_start\":";
            Times(out, row.milestones);
            out << row.extra << '}';
        }
        out << "]}\n";
        std::ofstream output(args.output, std::ios::binary | std::ios::trunc);
        const auto text = out.str();
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output)
        {
            throw std::runtime_error("Cannot publish benchmark result");
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#ifdef _WIN32
inline std::vector<std::string> Tokens(int argc, wchar_t** argv)
{
    std::vector<std::string> result;
    for (int i = 0; i < argc; ++i)
    {
        result.push_back(Utf8(std::filesystem::path(argv[i])));
    }
    return result;
}
#else
inline std::vector<std::string> Tokens(int argc, char** argv)
{
    return { argv, argv + argc };
}
#endif
} // namespace bench
