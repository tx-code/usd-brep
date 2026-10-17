// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSourceReport.h"

namespace step_source::report
{
void Writer::Add(std::string_view text)
{
    if (text.size() > m_maximum - m_text.size())
    {
        throw std::length_error("ReportByteLimit");
    }
    m_text.append(text);
}
void Writer::String(std::string_view text)
{
    StringValue(text, false);
}
void Writer::Utf8String(std::string_view text)
{
    StringValue(text, true);
}
void Writer::StringValue(std::string_view text, bool utf8)
{
    Add("\"");
    const char hex[] = "0123456789abcdef";
    for (const unsigned char c : text)
    {
        if (c == '"')
        {
            Add("\\\"");
        }
        else if (c == '\\')
        {
            Add("\\\\");
        }
        else if (c < 32 || c == 127 || (!utf8 && c >= 128))
        {
            const char escaped[] = { '\\', 'u', '0', '0', hex[c >> 4], hex[c & 15] };
            Add(std::string_view(escaped, 6));
        }
        else
        {
            const char value = static_cast<char>(c);
            Add(std::string_view(&value, 1));
        }
    }
    Add("\"");
}
void Writer::Number(std::size_t value)
{
    Add(std::to_string(value));
}
void Writer::Bool(bool value)
{
    Add(value ? "true" : "false");
}
void Writer::Range(Span span)
{
    Add("[");
    Number(span.begin);
    Add(",");
    Number(span.end);
    Add("]");
}
void Writer::Path(const std::vector<std::size_t>& path)
{
    Add("[");
    for (std::size_t i = 0; i < path.size(); ++i)
    {
        if (i)
        {
            Add(",");
        }
        Number(path[i]);
    }
    Add("]");
}
void Writer::Position(Location location)
{
    Add("{\"line\":");
    Number(location.line);
    Add(",\"column\":");
    Number(location.column);
    Add("}");
}
void Writer::Position(const InspectOutcome& input, std::size_t offset)
{
    if (!input.captured)
    {
        Add("null");
        return;
    }
    if (input.source)
    {
        Position(input.source->Locate(offset));
        return;
    }
    const auto& bytes = *input.captured;
    if (offset > bytes.size())
    {
        throw std::out_of_range("Source byte offset");
    }
    Location location;
    for (std::size_t i = 0; i < offset; ++i)
    {
        // A CRLF belongs to its preceding line until both bytes have been consumed,
        // matching SourceSnapshot's indexed locations even at the LF byte itself.
        if (bytes[i] == '\r' && i + 1 < bytes.size() && bytes[i + 1] == '\n')
        {
            if (i + 1 == offset)
            {
                ++location.column;
                break;
            }
            ++i;
            ++location.line;
            location.column = 1;
        }
        else if (bytes[i] == '\r' || bytes[i] == '\n')
        {
            ++location.line;
            location.column = 1;
        }
        else
        {
            ++location.column;
        }
    }
    Position(location);
}
} // namespace step_source::report
