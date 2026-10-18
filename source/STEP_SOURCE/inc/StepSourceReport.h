// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "StepSource.h"

#include <stdexcept>
#include <utility>

// Internal report composition; not a stable application interface.
namespace step_source::report
{
class Writer
{
public:

    // Only the outermost report publishes. Nested reports share its budget and buffer.
    template <class Serialize>
    static bool Publish(std::size_t maximum, std::string& output, Serialize serialize)
    {
        output.clear();
        try
        {
            // Reuse the caller's allocation privately, rather than holding the
            // previous report buffer while constructing another complete one.
            // Producers still observe empty output; only success publishes it.
            Writer writer(maximum, std::move(output));
            output.clear(); // A moved-from string is valid but its state need not be assumed.
            serialize(writer);
            output = std::move(writer.m_text);
            return true;
        }
        catch (const std::length_error&)
        {
            return false;
        }
    }

    void Add(std::string_view text);
    void String(std::string_view text);
    // Decoded source values are valid UTF-8; arbitrary raw bytes use String instead.
    void Utf8String(std::string_view text);
    void Number(std::size_t value);
    void Bool(bool value);
    void Range(Span span);
    void Path(const std::vector<std::size_t>& path);
    void Position(Location location);
    // Captured bytes suffice after parse failure; absent capture emits null. Offset is in [0, size].
    void Position(const InspectOutcome& input, std::size_t offset);

private:

    Writer(std::size_t maximum, std::string&& storage) noexcept : m_maximum(maximum), m_text(std::move(storage))
    {
    }
    std::size_t m_maximum;
    std::string m_text;
    void StringValue(std::string_view text, bool utf8);
};

void WriteJson(Writer& writer, const InspectOutcome& outcome);
} // namespace step_source::report
