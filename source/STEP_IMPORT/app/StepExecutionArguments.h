// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

#include <charconv>
#include <cstdint>
#include <limits>
#include <tl/expected.hpp>

namespace step_import_app
{
// Application execution resources only. Defaults are the existing viewer
// policy; neither these arguments nor exhaustion select a geometry profile,
// repair policy or automatic retry. No SDK default is changed.
struct ExecutionArguments
{
    std::optional<std::size_t> exactWorkUnits, deadlineMs;
    bool Explicit() const
    {
        return exactWorkUnits || deadlineMs;
    }
    std::size_t Work() const
    {
        return exactWorkUnits.value_or(2000000);
    }
    std::size_t DeadlineMs() const
    {
        return deadlineMs.value_or(90000);
    }
    tl::expected<bool, std::string_view> Parse(std::string_view key, std::string_view value)
    {
        if (key != "--max-exact-work-units" && key != "--deadline-ms")
        {
            return false;
        }
        auto& field = key == "--deadline-ms" ? deadlineMs : exactWorkUnits;
        if (field || value.empty() || value.front() == '0')
        {
            return tl::unexpected("InvalidOrDuplicateExecutionResourceArgument");
        }
        std::uint64_t number = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
        const auto maximum = key == "--deadline-ms" ? std::uint64_t(2147483647) : std::uint64_t(9223372036854775807ULL);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !number || number > maximum ||
            number > std::numeric_limits<std::size_t>::max())
        {
            return tl::unexpected("InvalidOrDuplicateExecutionResourceArgument");
        }
        field = std::size_t(number);
        return true;
    }
    void Apply(step_import::Options& options) const
    {
        options.nativeLimits.exactWorkUnits = Work();
        options.execution.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(DeadlineMs());
    }
};
} // namespace step_import_app
