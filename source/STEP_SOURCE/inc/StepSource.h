// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Internal source layer. No schema interpretation, geometric conversion or kernel dependency.
namespace step_source
{
struct Span
{
    std::size_t begin = 0;
    std::size_t end = 0; // Exclusive byte offset in the captured input.
};

struct Location
{
    std::size_t line = 1;
    std::size_t column = 1; // One-based byte column, with CRLF counted as one newline.
};

struct Limits
{
    std::size_t inputBytes;
    std::size_t records;
    std::size_t values;
    std::size_t references;
    std::size_t nesting;
    std::size_t stringBytes;
    std::size_t recordBytes;
    std::size_t reportBytes;
    std::size_t idDigits;

    // Explicit synthetic-test configuration; not a production preset.
    static Limits PlanarFixtures();
};

enum class ValueKind
{
    Integer,
    Real,
    String,
    Enumeration,
    Reference,
    Unset,
    Derived,
    Aggregate,
    Typed,
    OpaqueString // Explicit raw HEADER name / DATA text preservation only; no decoded text.
};

enum class HeaderNamePolicy
{
    StrictAscii,
    PreserveOpaqueBytes, // Nonstandard HEADER FILE_NAME.name only; never selects or guesses a code page.
    PreserveFileNameText // All FILE_NAME string fields/elements, never FILE_SCHEMA or implementation level.
};
enum class DataStringPolicy
{
    StrictAscii,
    PreserveOpaqueBytes // Unknown DATA STRING text; downstream must qualify each field's use independently.
};

enum class CarrierInterpretation
{
    DeclaredOnly,
    Level1AsCorrectedEncoding, // Explicit unverified assumption; not support for original 1994 SELECT encoding.
    Level1AsCorrectedEncodingWithLowercaseHex // Additionally interprets a-f in DATA X/X2/X4 payloads, only after a level1 assumption.
};
constexpr bool IsExplicitCarrierInterpretation(CarrierInterpretation interpretation)
{
    return interpretation == CarrierInterpretation::Level1AsCorrectedEncoding ||
           interpretation == CarrierInterpretation::Level1AsCorrectedEncodingWithLowercaseHex;
}

struct Value
{
    ValueKind kind = ValueKind::Unset;
    Span span;
    // Numbers: exact source lexeme, never double. Strings: decoded UTF-8.
    // OpaqueString: text is unavailable (empty storage); use kind + owning Raw(span), never an invented empty label.
    // Reference: canonical decimal ID (without '#'); typed value: type name.
    std::string text;
    std::vector<Value> children;
};

struct Component
{
    std::string name;
    Span span;
    std::vector<Value> parameters;
};

struct Record
{
    std::string id; // Decimal string, not an array index. Leading zero spellings share an ID.
    Span idSpan;
    Span span; // '#' through ';', excluding separator whitespace/comments.
    bool complex = false;
    std::vector<Component> components; // Full source order; no flattening or deduplication.
};

struct Reference
{
    std::string targetId;
    Span span;
    std::size_t recordIndex = 0;
    std::size_t componentIndex = 0;
    std::vector<std::size_t> parameterPath; // One-based parameter/aggregate/typed-child indices.
    std::size_t targetRecordIndex = 0;
};

struct Diagnostic
{
    std::string key;
    std::string stage;
    std::string message;
    Span primary;
    std::vector<Span> related;
    std::string recordId;
    std::string component;
    std::vector<std::size_t> parameterPath;
    std::string targetId;
};

struct StringHexInterpretation
{
    Span tokenSpan; // Complete quoted token, or consumed prefix when complete=false.
    Span firstStrictSpan; // Original strict InvalidStringEncoding at the first lowercase hex digit.
    std::size_t lowercaseDigits = 0;
    bool complete = false;
};
struct CarrierAssumption
{
    CarrierInterpretation interpretation = CarrierInterpretation::DeclaredOnly;
    std::string declaredLevel;
    Span declaredSpan;
    std::vector<Diagnostic> originalDiagnostics;
    std::vector<StringHexInterpretation> stringHexInterpretations; // Source-byte identity; no copied or normalized token.
};

struct Statistics
{
    std::size_t records = 0;
    std::size_t values = 0;
    std::size_t references = 0;
    std::size_t nesting = 0;
    std::size_t maxStringBytes = 0;
    std::size_t maxRecordBytes = 0;
};

struct SourceData;
class SourceSnapshot
{
public:

    explicit SourceSnapshot(std::shared_ptr<const SourceData> data);
    const std::string& Bytes() const;
    const std::vector<Component>& Header() const;
    const std::vector<Record>& Records() const;
    const std::vector<Reference>& References() const;
    const Record* Find(std::string_view canonicalId) const;
    const std::vector<std::size_t>& Incoming(std::string_view canonicalId) const;
    std::string_view Raw(Span span) const;
    Location Locate(std::size_t offset) const;
    const CarrierAssumption* AppliedCarrierAssumption() const; // Borrows immutable snapshot-owned interpretation evidence.

private:

    std::shared_ptr<const SourceData> m_data;
};

enum class Status
{
    Inspected,
    Rejected,
    Unsupported,
    ResourceLimit,
    IoFailure
};

struct InspectOutcome
{
    Status status = Status::Rejected;
    // Present only after complete syntax/index/reference checking; no partial record graph escapes.
    std::shared_ptr<const SourceSnapshot> source;
    // Complete captured bytes may survive a parse rejection. Null means capture was not completed.
    std::shared_ptr<const std::string> captured;
    std::string sha256;
    Statistics statistics;
    Limits limits{};
    std::vector<Diagnostic> diagnostics;
    HeaderNamePolicy headerNamePolicy = HeaderNamePolicy::StrictAscii;
    DataStringPolicy dataStringPolicy = DataStringPolicy::StrictAscii;
    CarrierInterpretation requestedCarrierInterpretation = CarrierInterpretation::DeclaredOnly;
    // Survives later parse failure without publishing a partial source graph.
    // Successful-source consumers use the snapshot's immutable receipt instead.
    std::shared_ptr<const CarrierAssumption> carrierAssumption;
};

// Bytes are owned by value before parsing. Neither entry point consults a path after capture.
InspectOutcome InspectBytes(
    std::string bytes,
    Limits limits,
    HeaderNamePolicy headerNamePolicy = HeaderNamePolicy::StrictAscii,
    DataStringPolicy dataStringPolicy = DataStringPolicy::StrictAscii,
    CarrierInterpretation carrierInterpretation = CarrierInterpretation::DeclaredOnly
);
InspectOutcome InspectFile(
    const std::filesystem::path& path,
    Limits limits,
    HeaderNamePolicy headerNamePolicy = HeaderNamePolicy::StrictAscii,
    DataStringPolicy dataStringPolicy = DataStringPolicy::StrictAscii,
    CarrierInterpretation carrierInterpretation = CarrierInterpretation::DeclaredOnly
);

// Bounded internal JSON report. False clears output; never returns a truncated success document.
bool InspectJson(const InspectOutcome& outcome, std::string& output);
const char* StatusName(Status status);
const char* ValueKindName(ValueKind kind);
const char* CarrierInterpretationName(CarrierInterpretation interpretation);
} // namespace step_source
