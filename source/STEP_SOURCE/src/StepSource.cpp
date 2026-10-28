// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSource.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <gsl/pointers>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace step_source
{
struct SourceData
{
    std::shared_ptr<const std::string> bytes;
    std::string sha256;
    std::shared_ptr<const CarrierAssumption> carrierAssumption;
    std::vector<Diagnostic> preservedHeaderIssues;
    std::vector<Span> asciiInvariantDataTokens; // Immutable receipt; resettable outcome options cannot grant this assumption.
    std::vector<Component> header;
    std::vector<Record> records;
    std::vector<Reference> references;
    std::vector<std::size_t> lines;
    std::unordered_map<std::string, std::size_t> index;
    std::unordered_map<std::string, std::vector<std::size_t>> incoming;
};

namespace
{
bool Upper(char c)
{
    return (c >= 'A' && c <= 'Z') || c == '_';
}
bool Digit(char c)
{
    return c >= '0' && c <= '9';
}
bool Name(std::string_view word)
{
    return !word.empty() && Upper(word.front()) &&
           std::all_of(
               word.begin(),
               word.end(),
               [](char c)
               {
                   return Upper(c) || Digit(c);
               }
           );
}

// Content fingerprint only. Tested against independent hashlib vectors and every frozen fixture.
std::string Sha256(const std::string& input)
{
    static constexpr std::uint32_t constants[64] = { 0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
                                                     0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
                                                     0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                                                     0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
                                                     0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                                                     0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                                                     0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                                                     0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
    std::array<std::uint32_t, 8> hash = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    auto rotate = [](std::uint32_t x, unsigned n)
    {
        return (x >> n) | (x << (32 - n));
    };
    const auto blocks = (input.size() + 9 + 63) / 64;
    const auto bits = static_cast<std::uint64_t>(input.size()) * 8;
    for (std::size_t block = 0; block < blocks; ++block)
    {
        std::uint32_t words[64] = {};
        for (std::size_t i = 0; i < 64; ++i)
        {
            const auto pos = block * 64 + i;
            unsigned char byte = 0;
            if (pos < input.size())
            {
                byte = static_cast<unsigned char>(input[pos]);
            }
            else if (pos == input.size())
            {
                byte = 0x80;
            }
            else if (pos >= blocks * 64 - 8)
            {
                byte = static_cast<unsigned char>(bits >> (8 * (blocks * 64 - pos - 1)));
            }
            words[i / 4] |= static_cast<std::uint32_t>(byte) << (24 - 8 * (i % 4));
        }
        for (std::size_t i = 16; i < 64; ++i)
        {
            const auto a = words[i - 15], b = words[i - 2];
            words[i] = words[i - 16] + (rotate(a, 7) ^ rotate(a, 18) ^ (a >> 3)) + words[i - 7] + (rotate(b, 17) ^ rotate(b, 19) ^ (b >> 10));
        }
        auto state = hash;
        for (std::size_t i = 0; i < 64; ++i)
        {
            const auto first = state[7] + (rotate(state[4], 6) ^ rotate(state[4], 11) ^ rotate(state[4], 25)) +
                               ((state[4] & state[5]) ^ (~state[4] & state[6])) + constants[i] + words[i];
            const auto second = (rotate(state[0], 2) ^ rotate(state[0], 13) ^ rotate(state[0], 22)) +
                                ((state[0] & state[1]) ^ (state[0] & state[2]) ^ (state[1] & state[2]));
            for (std::size_t j = 7; j > 0; --j)
            {
                state[j] = state[j - 1];
            }
            state[4] += first;
            state[0] = first + second;
        }
        for (std::size_t i = 0; i < 8; ++i)
        {
            hash[i] += state[i];
        }
    }
    const char hex[] = "0123456789abcdef";
    std::string result;
    for (auto word : hash)
    {
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            result += hex[(word >> shift) & 15];
        }
    }
    return result;
}

struct Failure
{
    Status status;
    Diagnostic diagnostic;
};

bool ValidLimits(const Limits& limits)
{
    // Independent implementation safety ceilings; callers can always request smaller limits.
    return limits.inputBytes <= 64 * 1024 * 1024 && limits.records <= 1000000 && limits.values <= 2000000 && limits.references <= 2000000 &&
           limits.nesting <= 128 && limits.stringBytes <= 1024 * 1024 && limits.recordBytes <= 16 * 1024 * 1024 &&
           limits.reportBytes <= 256 * 1024 * 1024 && limits.idDigits <= 1024;
}

enum class TokenKind
{
    Word,
    Integer,
    Real,
    String,
    Enumeration,
    Reference,
    Left,
    Right,
    Comma,
    Equal,
    Semicolon,
    Unset,
    Derived,
    End,
    OpaqueString
};
struct Token
{
    TokenKind kind;
    Span span;
    std::string text;
};

class Parser
{
public:

    Parser(std::shared_ptr<SourceData> data, InspectOutcome& result) : m_data(std::move(data)), m_result(result)
    {
    }

    void Parse()
    {
        const auto& input = *m_data->bytes;
        if (input.compare(0, 2, "PK") == 0 || (!input.empty() && (input.front() == '<' || input.front() == '\x1f')))
        {
            Fail(
                "UnsupportedCarrier",
                "Only self-contained Part21 text is supported",
                { 0, std::min(input.size(), std::size_t(4)) },
                Status::Unsupported
            );
        }
        for (std::size_t i = 0; i < input.size(); ++i)
        {
            const auto byte = static_cast<unsigned char>(input[i]);
            if (byte == 127 ||
                (byte >= 128 && m_result.headerNamePolicy == HeaderNamePolicy::StrictAscii &&
                 m_result.dataStringPolicy == DataStringPolicy::StrictAscii) ||
                (byte < 32 && byte != '\t' && byte != '\r' && byte != '\n'))
            {
                Fail("UnsupportedEncoding", "This reader slice accepts ASCII bytes only", { i, i + 1 }, Status::Unsupported);
            }
        }
        Word("ISO-10303-21");
        Expect(TokenKind::Semicolon);
        Word("HEADER");
        Expect(TokenKind::Semicolon);
        for (const auto* name : { "FILE_DESCRIPTION", "FILE_NAME", "FILE_SCHEMA" })
        {
            m_data->header.push_back(ParseComponent());
            const auto& component = m_data->header.back();
            if (component.name != name)
            {
                Fail("InvalidHeader", "Required header records must be present in order", component.span);
            }
            const auto end = Expect(TokenKind::Semicolon).span.end;
            Bound(end - component.span.begin, m_result.limits.recordBytes, "RecordByteLimit", { component.span.begin, end });
            m_result.statistics.maxRecordBytes = std::max(m_result.statistics.maxRecordBytes, end - component.span.begin);
            CheckHeader(component);
        }
        m_component.clear(); // The whole-carrier check has no parsed DATA owner.
        // Only completed FILE_NAME string spans authorize the selected exception.
        // Check every byte before DATA parsing, including skipped comments and
        // unapproved HEADER fields; no encoding relaxation can reach geometry.
        if (m_result.headerNamePolicy != HeaderNamePolicy::StrictAscii && m_result.dataStringPolicy == DataStringPolicy::StrictAscii)
        {
            std::size_t header = 0;
            for (std::size_t i = 0; i < input.size(); ++i)
            {
                while (header < m_opaqueHeaderStrings.size() && m_opaqueHeaderStrings[header].end <= i)
                {
                    ++header;
                }
                if (static_cast<unsigned char>(input[i]) >= 128 &&
                    (header == m_opaqueHeaderStrings.size() || i < m_opaqueHeaderStrings[header].begin))
                {
                    Fail(
                        "UnsupportedEncoding",
                        m_result.headerNamePolicy == HeaderNamePolicy::PreserveFileNameText ?
                            "Non-ASCII bytes are allowed only in explicitly opaque FILE_NAME text" :
                            "Non-ASCII bytes are allowed only in the opaque FILE_NAME.name token",
                        { i, i + 1 },
                        Status::Unsupported
                    );
                }
            }
        }
        if (Peek().text != "ENDSEC")
        {
            Fail("UnsupportedHeaderRecord", "Additional HEADER records are not supported in this slice", Peek().span, Status::Unsupported);
        }
        Word("ENDSEC");
        Expect(TokenKind::Semicolon);
        if (Peek().text == "ANCHOR" || Peek().text == "REFERENCE")
        {
            Fail("UnsupportedSection", "External reference/anchor sections are not supported", Peek().span, Status::Unsupported);
        }
        Word("DATA");
        if (Peek().kind == TokenKind::Left)
        {
            Fail("UnsupportedDataSection", "Parameterized DATA sections are not supported", Peek().span, Status::Unsupported);
        }
        Expect(TokenKind::Semicolon);
        m_inData = true;
        while (Peek().kind == TokenKind::Reference)
        {
            ParseRecord();
        }
        m_recordId.clear();
        m_component.clear();
        m_inData = false;
        Word("ENDSEC");
        Expect(TokenKind::Semicolon);
        if (Peek().text == "DATA" || Peek().text == "SIGNATURE" || Peek().text == "ANCHOR" || Peek().text == "REFERENCE")
        {
            Fail("UnsupportedSection", "Only one DATA section and no extensions are supported", Peek().span, Status::Unsupported);
        }
        Word("END-ISO-10303-21");
        Expect(TokenKind::Semicolon);
        if (Peek().kind != TokenKind::End)
        {
            Fail("TrailingContent", "Content after the end marker is not accepted", Peek().span);
        }
        if (m_result.dataStringPolicy != DataStringPolicy::StrictAscii)
        {
            // Lexing establishes token spans, not text or field meaning. Before
            // publishing ANY source graph, reject every high byte outside an
            // explicitly opaque token, including comments and unapproved HEADER text.
            // Spans arrive in lexical order: keep this whole-carrier pass linear.
            std::size_t opaque = 0;
            std::size_t header = 0;
            for (std::size_t i = 0; i < input.size(); ++i)
            {
                while (opaque < m_opaqueDataStrings.size() && m_opaqueDataStrings[opaque].end <= i)
                {
                    ++opaque;
                }
                const bool inData = opaque < m_opaqueDataStrings.size() && m_opaqueDataStrings[opaque].begin <= i &&
                                    i < m_opaqueDataStrings[opaque].end;
                while (header < m_opaqueHeaderStrings.size() && m_opaqueHeaderStrings[header].end <= i)
                {
                    ++header;
                }
                const bool inHeader = header < m_opaqueHeaderStrings.size() && m_opaqueHeaderStrings[header].begin <= i;
                if (static_cast<unsigned char>(input[i]) >= 128 && !inData && !inHeader)
                {
                    Fail("UnsupportedEncoding", "Non-ASCII bytes are outside explicitly preserved STRING tokens", { i, i + 1 }, Status::Unsupported);
                }
            }
        }
        Resolve();
        if (!m_opaqueDataStrings.empty())
        {
            m_result.diagnostics.push_back(
                { "OpaqueDataStringsRetained",
                  "D0",
                  "Nonstandard DATA strings retained as original bytes; text and encoding are unestablished",
                  m_opaqueDataStrings.front(),
                  {},
                  {},
                  {},
                  {},
                  {} }
            );
        }
    }

private:

    gsl::not_null<std::shared_ptr<SourceData>> m_data;
    InspectOutcome& m_result;
    std::size_t m_pos = 0;
    bool m_haveToken = false;
    Token m_token{};
    std::string m_recordId;
    std::string m_component;
    std::vector<std::size_t> m_path;
    bool m_headerNameExpected = false;
    bool m_fileNameHeader = false;
    std::vector<Span> m_opaqueHeaderStrings;
    bool m_inData = false;
    std::vector<Span> m_opaqueDataStrings;
    // The parser alone may extend this receipt before returning. Public
    // outcomes/snapshots share const owners; no mutation survives publication.
    std::shared_ptr<CarrierAssumption> m_carrierBuilder;
    std::optional<std::size_t> m_activeStringHex;

    Diagnostic ErrorDiagnostic(std::string key, std::string message, Span span, std::string stage = "D0") const
    {
        Diagnostic diagnostic;
        diagnostic.key = std::move(key);
        diagnostic.stage = std::move(stage);
        diagnostic.message = std::move(message);
        diagnostic.primary = span;
        diagnostic.recordId = m_recordId;
        diagnostic.component = m_component;
        diagnostic.parameterPath = m_path;
        return diagnostic;
    }
    [[noreturn]] void Fail(std::string key, std::string message, Span span, Status status = Status::Rejected, std::string stage = "D0")
    {
        if (m_activeStringHex)
        {
            m_carrierBuilder->stringHexInterpretations[*m_activeStringHex].tokenSpan.end = m_pos;
        }
        throw Failure{ status, ErrorDiagnostic(std::move(key), std::move(message), span, std::move(stage)) };
    }

    void Bound(std::size_t value, std::size_t maximum, const char* key, Span span)
    {
        if (value > maximum)
        {
            Fail(key, "Configured source resource limit exceeded", span, Status::ResourceLimit);
        }
    }

    void StringCodePoint(std::string& decoded, std::uint32_t value, Span span)
    {
        if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
        {
            Fail("InvalidStringCodePoint", "String directive must encode a Unicode scalar value", span);
        }
        if (value <= 0x7f)
        {
            decoded += static_cast<char>(value);
        }
        else if (value <= 0x7ff)
        {
            decoded += static_cast<char>(0xc0 | (value >> 6));
            decoded += static_cast<char>(0x80 | (value & 0x3f));
        }
        else if (value <= 0xffff)
        {
            decoded += static_cast<char>(0xe0 | (value >> 12));
            decoded += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
            decoded += static_cast<char>(0x80 | (value & 0x3f));
        }
        else
        {
            decoded += static_cast<char>(0xf0 | (value >> 18));
            decoded += static_cast<char>(0x80 | ((value >> 12) & 0x3f));
            decoded += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
            decoded += static_cast<char>(0x80 | (value & 0x3f));
        }
    }

    std::size_t StringPosition(std::size_t position) const
    {
        const auto& text = *m_data->bytes;
        while (position < text.size() && (text[position] == '\r' || text[position] == '\n' || text[position] == '\t'))
        {
            ++position;
        }
        return position;
    }
    std::optional<Token> OpaqueString(std::size_t start)
    {
        const bool header = !m_inData && ((m_headerNameExpected && m_result.headerNamePolicy == HeaderNamePolicy::PreserveOpaqueBytes) ||
                                          (m_fileNameHeader && m_result.headerNamePolicy == HeaderNamePolicy::PreserveFileNameText));
        const bool data = m_inData && m_result.dataStringPolicy != DataStringPolicy::StrictAscii;
        if (!header && !data)
        {
            return {};
        }
        const auto saved = m_pos;
        bool high = false;
        while (m_pos < m_data->bytes->size())
        {
            const auto position = StringPosition(m_pos);
            if (position == m_data->bytes->size())
            {
                break;
            }
            if (position - start + 1 > m_result.limits.stringBytes && !high)
            {
                // No opaque byte has been found within the allowed token budget.
                // Let the ordinary decoder choose its original failure (e.g. a
                // malformed directive before a later size limit), without an
                // unbounded preliminary scan changing diagnostic precedence.
                break;
            }
            const auto item = StringCharacter(start, start);
            high = high || static_cast<unsigned char>(item) >= 128;
            if (item == '\'')
            {
                const auto next = StringPosition(m_pos);
                if (next < m_data->bytes->size() && (*m_data->bytes)[next] == '\'')
                {
                    if (next - start + 1 > m_result.limits.stringBytes && !high)
                    {
                        break;
                    }
                    StringCharacter(start, start);
                }
                else
                {
                    if (high)
                    {
                        // Decode neither legacy bytes nor escape directives in
                        // this opaque value. A legacy multibyte trail may itself
                        // be '\\'; its meaning cannot be inferred as P21 syntax.
                        const Span span{ start, m_pos };
                        if (header)
                        {
                            m_opaqueHeaderStrings.push_back(span);
                        }
                        else
                        {
                            m_opaqueDataStrings.push_back(span);
                            // Application interpretation, not an inferred code page. A high byte
                            // denotes non-ASCII only under this explicit policy. Escapes, control
                            // bytes and state-changing encodings have no such proof here. Retain
                            // the original token and unknown text; do not synthesize Unicode.
                            const auto raw = std::string_view(*m_data->bytes).substr(start, m_pos - start);
                            const bool literalBytes = std::none_of(
                                raw.begin(),
                                raw.end(),
                                [](unsigned char ch)
                                {
                                    return ch == '\\' || ch < 32 || ch == 127;
                                }
                            );
                            if (m_result.dataStringPolicy == DataStringPolicy::PreserveAsciiInvariantBytes && literalBytes)
                            {
                                m_data->asciiInvariantDataTokens.push_back(span);
                            }
                        }
                        m_result.statistics.maxStringBytes = std::max(m_result.statistics.maxStringBytes, m_pos - start);
                        return Token{ TokenKind::OpaqueString, span, {} };
                    }
                    break;
                }
            }
        }
        m_pos = saved; // Ordinary ASCII strings retain the exact existing decoder and diagnostics.
        return {};
    }

    char StringCharacter(std::size_t start, std::size_t directive)
    {
        const auto& text = *m_data->bytes;
        m_pos = StringPosition(m_pos);
        Bound(m_pos - start, m_result.limits.stringBytes, "StringByteLimit", { start, m_pos });
        if (m_pos == text.size())
        {
            Fail("InvalidStringEncoding", "Incomplete string control directive", { directive, m_pos });
        }
        Bound(m_pos - start + 1, m_result.limits.stringBytes, "StringByteLimit", { start, m_pos + 1 });
        return text[m_pos++];
    }

    void StringExpected(char expected, std::size_t start, std::size_t directive)
    {
        if (StringCharacter(start, directive) != expected)
        {
            Fail("InvalidStringEncoding", "Malformed string control directive", { directive, m_pos });
        }
    }

    std::uint32_t StringHex(int digits, std::size_t start, std::size_t directive)
    {
        std::uint32_t value = 0;
        for (int digit = 0; digit < digits; ++digit)
        {
            const char c = StringCharacter(start, directive);
            const bool lowercase = c >= 'a' && c <= 'f';
            const bool interpreted = lowercase && m_inData && m_carrierBuilder &&
                                     m_carrierBuilder->interpretation == CarrierInterpretation::Level1AsCorrectedEncodingWithLowercaseHex;
            if (!(Digit(c) || (c >= 'A' && c <= 'F') || interpreted))
            {
                Fail("InvalidStringEncoding", "Expected uppercase hexadecimal digits", { directive, m_pos });
            }
            if (interpreted)
            {
                // Table2 HEX in the fixed N304 reference is uppercase. This
                // explicit alternative interprets numeric digits only; it is
                // neither a rewritten token nor an arbitrary escape recovery.
                auto& evidence = m_carrierBuilder->stringHexInterpretations;
                if (!m_activeStringHex)
                {
                    Bound(evidence.size() + 1, m_result.limits.values, "ValueLimit", { start, m_pos });
                    evidence.push_back({ { start, m_pos }, { directive, m_pos }, 0, false });
                    m_activeStringHex = evidence.size() - 1;
                }
                ++evidence[*m_activeStringHex].lowercaseDigits;
                evidence[*m_activeStringHex].tokenSpan.end = m_pos;
            }
            value = value * 16 + static_cast<unsigned>(Digit(c) ? c - '0' : c - (lowercase ? 'a' : 'A') + 10);
        }
        return value;
    }

    void StringDirective(std::string& decoded, std::size_t start, std::size_t directive)
    {
        const auto& text = *m_data->bytes;
        const char control = StringCharacter(start, directive);
        if (control == '\\')
        {
            decoded += '\\';
            return;
        }
        if (control == 'N' || control == 'F')
        {
            StringExpected('\\', start, directive);
            Fail("UnsupportedStringEncoding", "Print control directives are outside this slice", { directive, m_pos }, Status::Unsupported);
        }
        if (control == 'S')
        {
            StringExpected('\\', start, directive);
            const auto c = static_cast<unsigned char>(StringCharacter(start, directive));
            if (c < 0x20 || c > 0x7e)
            {
                Fail("InvalidStringEncoding", "PAGE requires a graphic ASCII character", { directive, m_pos });
            }
            StringCodePoint(decoded, c + 128, { directive, m_pos });
            return;
        }
        if (control == 'P')
        {
            const char alphabet = StringCharacter(start, directive);
            StringExpected('\\', start, directive);
            if (alphabet < 'A' || alphabet > 'I')
            {
                Fail("InvalidStringEncoding", "Alphabet must select ISO 8859 part 1 through 9", { directive, m_pos });
            }
            if (alphabet != 'A')
            {
                Fail("UnsupportedStringEncoding", "Only the ISO 8859-1 string alphabet is supported", { directive, m_pos }, Status::Unsupported);
            }
            return;
        }
        if (control != 'X')
        {
            Fail("InvalidStringEncoding", "Unknown string control directive", { directive, m_pos });
        }
        const char mode = StringCharacter(start, directive);
        if (mode == '\\')
        {
            const auto value = StringHex(2, start, directive);
            StringCodePoint(decoded, value, { directive, m_pos });
            return;
        }
        if (mode != '2' && mode != '4')
        {
            Fail("InvalidStringEncoding", "Expected X, X2 or X4 string directive", { directive, m_pos });
        }
        StringExpected('\\', start, directive);
        bool any = false;
        while (true)
        {
            const auto group = StringPosition(m_pos);
            if (group == text.size() || text[group] == '\\')
            {
                break;
            }
            const auto value = StringHex(mode == '2' ? 4 : 8, start, directive);
            StringCodePoint(decoded, value, { group, m_pos });
            any = true;
        }
        if (!any)
        {
            Fail("InvalidStringEncoding", "Extended string directive requires a code point", { directive, m_pos });
        }
        StringExpected('\\', start, directive);
        StringExpected('X', start, directive);
        StringExpected('0', start, directive);
        StringExpected('\\', start, directive);
    }

    Token Lex()
    {
        const auto& text = *m_data->bytes;
        while (m_pos < text.size())
        {
            const char c = text[m_pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                ++m_pos;
            }
            else if (text.compare(m_pos, 2, "/*") == 0)
            {
                const auto start = m_pos;
                const auto close = text.find("*/", m_pos + 2);
                if (close == std::string::npos)
                {
                    Fail("UnterminatedComment", "Block comment has no closing delimiter", { start, text.size() });
                }
                m_pos = close + 2;
            }
            else
            {
                break;
            }
        }
        const auto start = m_pos;
        if (m_pos == text.size())
        {
            return { TokenKind::End, { m_pos, m_pos }, {} };
        }
        const char c = text[m_pos++];
        TokenKind kind = TokenKind::End;
        switch (c)
        {
            case '(':
                kind = TokenKind::Left;
                break;
            case ')':
                kind = TokenKind::Right;
                break;
            case ',':
                kind = TokenKind::Comma;
                break;
            case '=':
                kind = TokenKind::Equal;
                break;
            case ';':
                kind = TokenKind::Semicolon;
                break;
            case '$':
                kind = TokenKind::Unset;
                break;
            case '*':
                kind = TokenKind::Derived;
                break;
            case '\'':
            {
                Expects(!m_activeStringHex);
                if (const auto opaque = OpaqueString(start))
                {
                    return *opaque;
                }
                std::string decoded;
                while (m_pos < text.size())
                {
                    if (StringPosition(m_pos) == text.size())
                    {
                        Bound(text.size() - start, m_result.limits.stringBytes, "StringByteLimit", { start, text.size() });
                        break;
                    }
                    const char item = StringCharacter(start, start);
                    if (item == '\'')
                    {
                        const auto next = StringPosition(m_pos);
                        if (next < text.size() && text[next] == '\'')
                        {
                            StringCharacter(start, start);
                        }
                        else
                        {
                            m_result.statistics.maxStringBytes = std::max(m_result.statistics.maxStringBytes, m_pos - start);
                            if (m_activeStringHex)
                            {
                                auto& evidence = m_carrierBuilder->stringHexInterpretations[*m_activeStringHex];
                                evidence.tokenSpan = { start, m_pos };
                                evidence.complete = true;
                                m_activeStringHex.reset();
                            }
                            return { TokenKind::String, { start, m_pos }, std::move(decoded) };
                        }
                    }
                    else if (item == '\\')
                    {
                        StringDirective(decoded, start, m_pos - 1);
                        continue;
                    }
                    decoded += item;
                }
                Fail("UnterminatedString", "String has no closing apostrophe", { start, text.size() });
            }
            case '#':
            {
                const auto digits = m_pos;
                while (m_pos < text.size() && Digit(text[m_pos]))
                {
                    ++m_pos;
                }
                if (digits == m_pos)
                {
                    Fail("InvalidInstanceId", "Instance ID requires decimal digits", { start, m_pos });
                }
                Bound(m_pos - digits, m_result.limits.idDigits, "IdDigitLimit", { start, m_pos });
                const auto nonzero = text.find_first_not_of('0', digits);
                if (nonzero >= m_pos)
                {
                    Fail("InvalidInstanceId", "Instance ID must be positive", { start, m_pos });
                }
                return { TokenKind::Reference, { start, m_pos }, text.substr(nonzero, m_pos - nonzero) };
            }
            case '.':
            {
                const auto word = m_pos;
                while (m_pos < text.size() && (Upper(text[m_pos]) || Digit(text[m_pos])))
                {
                    ++m_pos;
                }
                if (m_pos == word || !Upper(text[word]) || m_pos == text.size() || text[m_pos++] != '.')
                {
                    Fail("InvalidEnumeration", "Expected an uppercase enumeration between full stops", { start, m_pos });
                }
                return { TokenKind::Enumeration, { start, m_pos }, text.substr(word, m_pos - word - 1) };
            }
            case '"':
            case '!':
            case '@':
            case '\\':
                Fail("UnsupportedToken", "Binary, user-defined and value-instance forms are not supported", { start, m_pos }, Status::Unsupported);
            default:
            {
                if (static_cast<unsigned char>(c) >= 128)
                {
                    Fail("UnsupportedEncoding", "Non-ASCII byte outside a STRING token", { start, m_pos }, Status::Unsupported);
                }
                if (Upper(c))
                {
                    while (m_pos < text.size() && (Upper(text[m_pos]) || Digit(text[m_pos]) || text[m_pos] == '-'))
                    {
                        ++m_pos;
                    }
                    return { TokenKind::Word, { start, m_pos }, text.substr(start, m_pos - start) };
                }
                if (Digit(c) || c == '+' || c == '-')
                {
                    if (!Digit(c) && (m_pos == text.size() || !Digit(text[m_pos])))
                    {
                        Fail("InvalidNumber", "Sign must be immediately followed by digits", { start, m_pos });
                    }
                    while (m_pos < text.size() && Digit(text[m_pos]))
                    {
                        ++m_pos;
                    }
                    kind = TokenKind::Integer;
                    if (m_pos < text.size() && text[m_pos] == '.')
                    {
                        kind = TokenKind::Real;
                        ++m_pos;
                        while (m_pos < text.size() && Digit(text[m_pos]))
                        {
                            ++m_pos;
                        }
                        if (m_pos < text.size() && text[m_pos] == 'E')
                        {
                            ++m_pos;
                            if (m_pos < text.size() && (text[m_pos] == '+' || text[m_pos] == '-'))
                            {
                                ++m_pos;
                            }
                            const auto exponent = m_pos;
                            while (m_pos < text.size() && Digit(text[m_pos]))
                            {
                                ++m_pos;
                            }
                            if (m_pos == exponent)
                            {
                                Fail("InvalidNumber", "Exponent requires digits", { start, m_pos });
                            }
                        }
                    }
                    return { kind, { start, m_pos }, text.substr(start, m_pos - start) };
                }
                Fail("UnexpectedCharacter", "Character is not a supported Part21 token", { start, m_pos });
            }
        }
        return { kind, { start, m_pos }, text.substr(start, 1) };
    }

    const Token& Peek()
    {
        if (!m_haveToken)
        {
            m_token = Lex();
            m_haveToken = true;
        }
        return m_token;
    }
    Token Take()
    {
        const auto token = Peek();
        m_haveToken = false;
        return token;
    }
    Token Expect(TokenKind kind)
    {
        if (Peek().kind != kind)
        {
            Fail("SyntaxError", "Unexpected token in exchange structure", Peek().span);
        }
        return Take();
    }
    void Word(const char* word)
    {
        if (Peek().kind != TokenKind::Word || Peek().text != word)
        {
            Fail("SyntaxError", std::string("Expected ") + word, Peek().span);
        }
        Take();
    }
    std::vector<Value> Parameters(std::size_t depth)
    {
        Expect(TokenKind::Left);
        std::vector<Value> values;
        if (Peek().kind != TokenKind::Right)
        {
            while (true)
            {
                m_path.push_back(values.size() + 1);
                values.push_back(ParseValue(depth + 1));
                m_headerNameExpected = false;
                m_path.pop_back();
                if (Peek().kind != TokenKind::Comma)
                {
                    break;
                }
                Take();
            }
        }
        Expect(TokenKind::Right);
        return values;
    }
    Value ParseValue(std::size_t depth)
    {
        const auto token = Peek();
        Bound(depth, m_result.limits.nesting, "NestingLimit", token.span);
        Bound(++m_result.statistics.values, m_result.limits.values, "ValueLimit", token.span);
        m_result.statistics.nesting = std::max(m_result.statistics.nesting, depth);
        Value value;
        value.span = token.span;
        value.text = token.text;
        switch (token.kind)
        {
            case TokenKind::Integer:
                value.kind = ValueKind::Integer;
                break;
            case TokenKind::Real:
                value.kind = ValueKind::Real;
                break;
            case TokenKind::String:
                value.kind = ValueKind::String;
                break;
            case TokenKind::OpaqueString:
                value.kind = ValueKind::OpaqueString;
                break;
            case TokenKind::Enumeration:
                value.kind = ValueKind::Enumeration;
                break;
            case TokenKind::Reference:
                value.kind = ValueKind::Reference;
                Bound(++m_result.statistics.references, m_result.limits.references, "ReferenceLimit", token.span);
                break;
            case TokenKind::Unset:
                value.kind = ValueKind::Unset;
                break;
            case TokenKind::Derived:
                value.kind = ValueKind::Derived;
                break;
            case TokenKind::Left:
                value.kind = ValueKind::Aggregate;
                value.text.clear();
                value.children = Parameters(depth);
                value.span.end = m_token.span.end;
                return value;
            case TokenKind::Word:
                if (!Name(token.text))
                {
                    Fail("SyntaxError", "Invalid typed parameter name", token.span);
                }
                Take();
                value.kind = ValueKind::Typed;
                value.children = Parameters(depth);
                value.span.end = m_token.span.end;
                if (value.children.size() != 1)
                {
                    Fail("SyntaxError", "Typed parameters require exactly one value", value.span);
                }
                return value;
            default:
                Fail("SyntaxError", "Expected a parameter value", token.span);
        }
        Take();
        return value;
    }
    Component ParseComponent()
    {
        const auto name = Expect(TokenKind::Word);
        if (!Name(name.text))
        {
            Fail("SyntaxError", "Invalid entity keyword", name.span);
        }
        m_component = name.text;
        m_headerNameExpected = m_recordId.empty() && m_data->header.size() == 1 && name.text == "FILE_NAME";
        m_fileNameHeader = m_headerNameExpected;
        auto values = Parameters(0);
        m_fileNameHeader = false;
        return { name.text, { name.span.begin, m_token.span.end }, std::move(values) };
    }
    static bool StringList(const Value& value, bool allowOpaque = false)
    {
        return value.kind == ValueKind::Aggregate && !value.children.empty() &&
               std::all_of(
                   value.children.begin(),
                   value.children.end(),
                   [allowOpaque](const Value& v)
                   {
                       return v.kind == ValueKind::String || (allowOpaque && v.kind == ValueKind::OpaqueString);
                   }
               );
    }
    void CheckHeader(const Component& component)
    {
        const auto& p = component.parameters;
        bool valid = false;
        if (component.name == "FILE_DESCRIPTION")
        {
            valid = p.size() == 2 && StringList(p[0]) && p[1].kind == ValueKind::String;
            if (valid && p[1].text != "2;1")
            {
                constexpr auto message = "Only FILE_DESCRIPTION implementation level 2;1 is supported";
                const auto choice = m_result.requestedCarrierInterpretation;
                const bool level1 = p[1].text == "1" && (choice == CarrierInterpretation::Level1AsCorrectedEncoding ||
                                                         choice == CarrierInterpretation::Level1AsCorrectedEncodingWithLowercaseHex);
                // Empty/ASCII-space-only declarations carry no encoding claim.
                // Only this explicit application choice selects corrected syntax;
                // keep the original declaration/span and unverified receipt. Do
                // not trim a nonblank value or derive a level from schema/year.
                const bool blank = choice == CarrierInterpretation::BlankLevelAsCorrectedEncoding && std::all_of(
                                                                                                         p[1].text.begin(),
                                                                                                         p[1].text.end(),
                                                                                                         [](char c)
                                                                                                         {
                                                                                                             return c == ' ';
                                                                                                         }
                                                                                                     );
                if (!level1 && !blank)
                {
                    Fail("UnsupportedImplementationLevel", message, p[1].span, Status::Unsupported);
                }
                // ISO10303-21:1994/Cor1:1996 replacement9.2.1 distinguishes
                // declaration1 from corrected2;1. This application assumption
                // selects the existing corrected grammar without rewriting any
                // byte/value. Declared/missing encoding semantics remain unverified; all
                // syntax/reference/budget and downstream typed checks still run.
                auto assumption = std::make_shared<CarrierAssumption>();
                assumption->interpretation = m_result.requestedCarrierInterpretation;
                assumption->declaredLevel = p[1].text;
                assumption->declaredSpan = p[1].span;
                assumption->originalDiagnostics.push_back(ErrorDiagnostic("UnsupportedImplementationLevel", message, p[1].span));
                m_result.carrierAssumption = assumption;
                m_data->carrierAssumption = assumption;
                m_carrierBuilder = std::move(assumption);
            }
        }
        else if (component.name == "FILE_NAME")
        {
            const bool opaqueText = m_result.headerNamePolicy == HeaderNamePolicy::PreserveFileNameText;
            valid = p.size() == 7 && StringList(p[2], opaqueText) && StringList(p[3], opaqueText);
            for (std::size_t i : { std::size_t(0), std::size_t(1), std::size_t(4), std::size_t(5), std::size_t(6) })
            {
                // The separate metadata policy preserves one known schema
                // error, not malformed syntax or text decoding. All other
                // FILE_NAME fields, arity and header selectors stay strict.
                const bool preserve = i == 6 && m_result.headerMetadataPolicy == HeaderMetadataPolicy::PreserveUnsetAuthorization && p.size() == 7 &&
                                      p[i].kind == ValueKind::Unset;
                valid = valid && (p[i].kind == ValueKind::String || ((i == 0 || opaqueText) && p[i].kind == ValueKind::OpaqueString) || preserve);
            }
            if (valid && p[6].kind == ValueKind::Unset)
            {
                // ISO 10303-21 file_name (8.2.3, fixed project carrier contract):
                // authorization is mandatory STRING human-readable metadata.
                // Keep $ and its original InvalidHeader rather than inventing
                // an empty string or a permission/authentication claim.
                Diagnostic issue{ "InvalidHeader", "D0", "Header parameter shape is invalid", component.span, { p[6].span }, {}, "FILE_NAME",
                                  { 7 },           {} };
                m_data->preservedHeaderIssues.push_back(issue);
                m_result.preservedHeaderIssues.push_back(std::move(issue));
            }
        }
        else if (component.name == "FILE_SCHEMA")
        {
            valid = p.size() == 1 && StringList(p[0]);
        }
        if (!valid)
        {
            Fail("InvalidHeader", "Header parameter shape is invalid", component.span);
        }
    }
    void ParseRecord()
    {
        const auto id = Take();
        m_recordId = id.text;
        m_component.clear();
        Bound(m_data->records.size() + 1, m_result.limits.records, "RecordLimit", id.span);
        const auto previous = m_data->index.find(id.text);
        if (previous != m_data->index.end())
        {
            Diagnostic diagnostic{ "DuplicateInstanceId",
                                   "D0",
                                   "Two DATA occurrences have the same instance ID",
                                   id.span,
                                   { m_data->records[previous->second].idSpan },
                                   id.text,
                                   {},
                                   {},
                                   id.text };
            throw Failure{ Status::Rejected, std::move(diagnostic) };
        }
        Expect(TokenKind::Equal);
        Record record;
        record.id = id.text;
        record.idSpan = id.span;
        record.span.begin = id.span.begin;
        record.complex = Peek().kind == TokenKind::Left;
        if (record.complex)
        {
            Take();
            do
            {
                record.components.push_back(ParseComponent());
            } while (Peek().kind == TokenKind::Word);
            Expect(TokenKind::Right);
        }
        else
        {
            record.components.push_back(ParseComponent());
        }
        record.span.end = Expect(TokenKind::Semicolon).span.end;
        Bound(record.span.end - record.span.begin, m_result.limits.recordBytes, "RecordByteLimit", record.span);
        m_result.statistics.maxRecordBytes = std::max(m_result.statistics.maxRecordBytes, record.span.end - record.span.begin);
        m_data->index.emplace(record.id, m_data->records.size());
        m_data->records.push_back(std::move(record));
        m_result.statistics.records = m_data->records.size();
        m_recordId.clear();
        m_component.clear();
    }
    void Collect(const Value& value, std::size_t record, std::size_t component, std::vector<std::size_t>& path)
    {
        if (value.kind == ValueKind::Reference)
        {
            const auto target = m_data->index.find(value.text);
            if (target == m_data->index.end())
            {
                const auto& owner = m_data->records[record];
                Diagnostic diagnostic{ "UnresolvedRequiredReference",
                                       "D1",
                                       "DATA reference has no target in the complete file",
                                       value.span,
                                       {},
                                       owner.id,
                                       owner.components[component].name,
                                       path,
                                       value.text };
                throw Failure{ Status::Rejected, std::move(diagnostic) };
            }
            const auto referenceIndex = m_data->references.size();
            m_data->references.push_back({ value.text, value.span, record, component, path, target->second });
            m_data->incoming[value.text].push_back(referenceIndex);
        }
        for (std::size_t i = 0; i < value.children.size(); ++i)
        {
            path.push_back(i + 1);
            Collect(value.children[i], record, component, path);
            path.pop_back();
        }
    }
    void Resolve()
    {
        for (std::size_t r = 0; r < m_data->records.size(); ++r)
        {
            for (std::size_t c = 0; c < m_data->records[r].components.size(); ++c)
            {
                const auto& component = m_data->records[r].components[c];
                for (std::size_t p = 0; p < component.parameters.size(); ++p)
                {
                    std::vector<std::size_t> path{ p + 1 };
                    Collect(component.parameters[p], r, c, path);
                }
            }
        }
    }
};
} // namespace

Limits Limits::PlanarFixtures()
{
    return { 1024 * 1024, 10000, 200000, 100000, 64, 32769, 65536, 16 * 1024 * 1024, 128 };
}

SourceSnapshot::SourceSnapshot(std::shared_ptr<const SourceData> data) : m_data(std::move(data))
{
    if (!m_data)
    {
        throw std::invalid_argument("SourceSnapshot requires complete source data");
    }
}
const std::string& SourceSnapshot::Bytes() const
{
    return *m_data->bytes;
}
const std::string& SourceSnapshot::Sha256() const
{
    return m_data->sha256;
}
const std::vector<Component>& SourceSnapshot::Header() const
{
    return m_data->header;
}
const std::vector<Record>& SourceSnapshot::Records() const
{
    return m_data->records;
}
const std::vector<Reference>& SourceSnapshot::References() const
{
    return m_data->references;
}
const Record* SourceSnapshot::Find(std::string_view canonicalId) const
{
    const auto found = m_data->index.find(std::string(canonicalId));
    return found == m_data->index.end() ? nullptr : &m_data->records[found->second];
}
const std::vector<std::size_t>& SourceSnapshot::Incoming(std::string_view canonicalId) const
{
    static const std::vector<std::size_t> empty;
    const auto found = m_data->incoming.find(std::string(canonicalId));
    return found == m_data->incoming.end() ? empty : found->second;
}
std::string_view SourceSnapshot::Raw(Span span) const
{
    if (span.begin > span.end || span.end > m_data->bytes->size())
    {
        throw std::out_of_range("Source byte span");
    }
    return std::string_view(*m_data->bytes).substr(span.begin, span.end - span.begin);
}
Location SourceSnapshot::Locate(std::size_t offset) const
{
    if (offset > m_data->bytes->size())
    {
        throw std::out_of_range("Source byte offset");
    }
    const auto line = std::upper_bound(m_data->lines.begin(), m_data->lines.end(), offset) - m_data->lines.begin();
    return { static_cast<std::size_t>(line), offset - m_data->lines[static_cast<std::size_t>(line) - 1] + 1 };
}

bool SourceSnapshot::HasAsciiInvariantDataAssumption() const
{
    return !m_data->asciiInvariantDataTokens.empty();
}
bool SourceSnapshot::IsAssumedNonAsciiData(const Value& value) const
{
    if (value.kind != ValueKind::OpaqueString)
    {
        return false;
    }
    // Parser receipts are ordered by source position. Compare against captured
    // token identities rather than trusting a caller's policy or empty text.
    const auto& spans = m_data->asciiInvariantDataTokens;
    const auto found = std::lower_bound(
        spans.begin(),
        spans.end(),
        value.span.begin,
        [](Span span, std::size_t begin)
        {
            return span.begin < begin;
        }
    );
    return found != spans.end() && found->begin == value.span.begin && found->end == value.span.end;
}
const CarrierAssumption* SourceSnapshot::AppliedCarrierAssumption() const
{
    return m_data->carrierAssumption.get();
}
const std::vector<Diagnostic>& SourceSnapshot::PreservedHeaderIssues() const
{
    return m_data->preservedHeaderIssues;
}
const char* CarrierInterpretationName(CarrierInterpretation interpretation)
{
    switch (interpretation)
    {
        case CarrierInterpretation::DeclaredOnly:
            return "DeclaredOnly";
        case CarrierInterpretation::Level1AsCorrectedEncoding:
            return "Level1AsCorrectedEncoding";
        case CarrierInterpretation::Level1AsCorrectedEncodingWithLowercaseHex:
            return "Level1AsCorrectedEncodingWithLowercaseHex";
        case CarrierInterpretation::BlankLevelAsCorrectedEncoding:
            return "BlankLevelAsCorrectedEncoding";
    }
    return "Unknown";
}
InspectOutcome InspectBytes(
    std::string bytes,
    Limits limits,
    HeaderNamePolicy headerNamePolicy,
    DataStringPolicy dataStringPolicy,
    CarrierInterpretation carrierInterpretation,
    HeaderMetadataPolicy headerMetadataPolicy
)
{
    if (headerMetadataPolicy != HeaderMetadataPolicy::Strict && headerMetadataPolicy != HeaderMetadataPolicy::PreserveUnsetAuthorization)
    {
        throw std::invalid_argument("Unknown HEADER metadata policy");
    }
    if (headerNamePolicy != HeaderNamePolicy::StrictAscii && headerNamePolicy != HeaderNamePolicy::PreserveOpaqueBytes &&
        headerNamePolicy != HeaderNamePolicy::PreserveFileNameText)
    {
        throw std::invalid_argument("Unknown HEADER filename policy");
    }
    if (dataStringPolicy != DataStringPolicy::StrictAscii && dataStringPolicy != DataStringPolicy::PreserveOpaqueBytes &&
        dataStringPolicy != DataStringPolicy::PreserveAsciiInvariantBytes)
    {
        throw std::invalid_argument("Unknown DATA string policy");
    }
    if (carrierInterpretation != CarrierInterpretation::DeclaredOnly && !IsExplicitCarrierInterpretation(carrierInterpretation))
    {
        throw std::invalid_argument("Unknown carrier interpretation");
    }
    InspectOutcome result{};
    result.limits = limits;
    result.headerNamePolicy = headerNamePolicy;
    result.dataStringPolicy = dataStringPolicy;
    result.requestedCarrierInterpretation = carrierInterpretation;
    result.headerMetadataPolicy = headerMetadataPolicy;
    if (!ValidLimits(limits))
    {
        result.status = Status::ResourceLimit;
        result.diagnostics.push_back({ "InvalidLimits", "D0", "Requested limits exceed implementation safety ceilings", {}, {}, {}, {}, {}, {} });
        return result;
    }
    if (bytes.size() > limits.inputBytes)
    {
        result.status = Status::ResourceLimit;
        result.diagnostics.push_back({ "InputByteLimit", "D0", "Input capture exceeds configured byte limit", {}, {}, {}, {}, {}, {} });
        return result;
    }
    result.captured = std::make_shared<const std::string>(std::move(bytes));
    result.sha256 = Sha256(*result.captured);
    auto data = std::make_shared<SourceData>();
    data->bytes = result.captured;
    data->sha256 = result.sha256;
    data->lines.push_back(0);
    for (std::size_t i = 0; i < data->bytes->size(); ++i)
    {
        if ((*data->bytes)[i] == '\r' || (*data->bytes)[i] == '\n')
        {
            if ((*data->bytes)[i] == '\r' && i + 1 < data->bytes->size() && (*data->bytes)[i + 1] == '\n')
            {
                ++i;
            }
            data->lines.push_back(i + 1);
        }
    }
    try
    {
        Parser(data, result).Parse();
        result.source = std::make_shared<const SourceSnapshot>(std::move(data));
        result.status = Status::Inspected;
        if (const auto* assumption = result.source->AppliedCarrierAssumption())
        {
            result.diagnostics.push_back(
                { "UnverifiedCarrierInterpretation",
                  "D0",
                  "Declared level1 interpreted using corrected grammar; original encoding conformance is unverified",
                  assumption->declaredSpan,
                  {},
                  {},
                  "FILE_DESCRIPTION",
                  { 2 },
                  {} }
            );
        }
        const auto& name = result.source->Header()[1].parameters[0];
        if (name.kind == ValueKind::OpaqueString)
        {
            result.diagnostics.push_back(
                { "OpaqueHeaderFileName",
                  "D0",
                  "Nonstandard FILE_NAME.name retained as original bytes; text interpretation and encoding remain unestablished",
                  name.span,
                  {},
                  {},
                  "FILE_NAME",
                  { 1 },
                  {} }
            );
        }
        if (headerNamePolicy == HeaderNamePolicy::PreserveFileNameText)
        {
            // FILE_NAME has only administrative STRING fields and one-level
            // STRING lists. CheckHeader already proved that exact shape. Emit
            // per-value source paths; never decode, replace or invent metadata.
            const auto& parameters = result.source->Header()[1].parameters;
            const auto record = [&](const Value& value, std::vector<std::size_t> path)
            {
                if (value.kind == ValueKind::OpaqueString)
                {
                    result.diagnostics.push_back(
                        { "OpaqueHeaderFileNameText",
                          "D0",
                          "Nonstandard FILE_NAME metadata retained; text and encoding remain unestablished",
                          value.span,
                          {},
                          {},
                          "FILE_NAME",
                          std::move(path),
                          {} }
                    );
                }
            };
            for (std::size_t i = 1; i < parameters.size(); ++i)
            {
                if (parameters[i].kind == ValueKind::Aggregate)
                {
                    for (std::size_t j = 0; j < parameters[i].children.size(); ++j)
                    {
                        record(parameters[i].children[j], { i + 1, j + 1 });
                    }
                }
                else
                {
                    record(parameters[i], { i + 1 });
                }
            }
        }
    }
    catch (const Failure& failure)
    {
        result.status = failure.status;
        result.diagnostics.push_back(failure.diagnostic);
    }
    return result;
}

InspectOutcome InspectFile(
    const std::filesystem::path& path,
    Limits limits,
    HeaderNamePolicy headerNamePolicy,
    DataStringPolicy dataStringPolicy,
    CarrierInterpretation carrierInterpretation,
    HeaderMetadataPolicy headerMetadataPolicy
)
{
    if (headerMetadataPolicy != HeaderMetadataPolicy::Strict && headerMetadataPolicy != HeaderMetadataPolicy::PreserveUnsetAuthorization)
    {
        throw std::invalid_argument("Unknown HEADER metadata policy");
    }
    if (dataStringPolicy != DataStringPolicy::StrictAscii && dataStringPolicy != DataStringPolicy::PreserveOpaqueBytes &&
        dataStringPolicy != DataStringPolicy::PreserveAsciiInvariantBytes)
    {
        throw std::invalid_argument("Unknown DATA string policy");
    }
    if (carrierInterpretation != CarrierInterpretation::DeclaredOnly && !IsExplicitCarrierInterpretation(carrierInterpretation))
    {
        throw std::invalid_argument("Unknown carrier interpretation");
    }
    if (!ValidLimits(limits))
    {
        return InspectBytes({}, limits, headerNamePolicy, dataStringPolicy, carrierInterpretation, headerMetadataPolicy);
    }
    std::ifstream input(path, std::ios::binary);
    std::string bytes;
    if (input)
    {
        std::array<char, 4096> buffer{};
        while (input)
        {
            const auto count = std::min(buffer.size(), limits.inputBytes - bytes.size() + 1);
            input.read(buffer.data(), static_cast<std::streamsize>(count));
            bytes.append(buffer.data(), static_cast<std::size_t>(input.gcount()));
            if (bytes.size() > limits.inputBytes)
            {
                return InspectBytes(std::move(bytes), limits, headerNamePolicy, dataStringPolicy, carrierInterpretation, headerMetadataPolicy);
            }
        }
        if (input.eof() && !input.bad())
        {
            return InspectBytes(std::move(bytes), limits, headerNamePolicy, dataStringPolicy, carrierInterpretation, headerMetadataPolicy);
        }
    }
    InspectOutcome result{};
    result.status = Status::IoFailure;
    result.limits = limits;
    result.headerNamePolicy = headerNamePolicy;
    result.dataStringPolicy = dataStringPolicy;
    result.requestedCarrierInterpretation = carrierInterpretation;
    result.headerMetadataPolicy = headerMetadataPolicy;
    result.diagnostics.push_back({ "InputReadFailure", "Capture", "Input capture did not complete", {}, {}, {}, {}, {}, {} });
    return result;
}

const char* StatusName(Status status)
{
    switch (status)
    {
        case Status::Inspected:
            return "Inspected";
        case Status::Rejected:
            return "Rejected";
        case Status::Unsupported:
            return "Unsupported";
        case Status::ResourceLimit:
            return "ResourceLimit";
        case Status::IoFailure:
            return "IoFailure";
    }
    return "Unknown";
}
const char* ValueKindName(ValueKind kind)
{
    switch (kind)
    {
        case ValueKind::Integer:
            return "Integer";
        case ValueKind::Real:
            return "Real";
        case ValueKind::String:
            return "String";
        case ValueKind::Enumeration:
            return "Enumeration";
        case ValueKind::Reference:
            return "Reference";
        case ValueKind::Unset:
            return "Unset";
        case ValueKind::Derived:
            return "Derived";
        case ValueKind::Aggregate:
            return "Aggregate";
        case ValueKind::Typed:
            return "Typed";
        case ValueKind::OpaqueString:
            return "OpaqueString";
    }
    return "Unknown";
}
} // namespace step_source
