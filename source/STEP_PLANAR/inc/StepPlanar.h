// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepSemantics.h"

#include <array>
#include <cstdint>
#include <map>

namespace step_planar
{
// Source LOGICAL UNKNOWN is a computed value. An unevaluated expression has no Logical value.
enum class Logical
{
    False,
    True,
    Unknown
};
Logical Not(Logical value);
bool QuerySelects(Logical value);
const char* LogicalName(Logical value);

// Bounded, reduced exact rational. Capacity exhaustion never rounds or grants a pass.
class Rational
{
public:

    Rational() = default;
    Rational(std::int64_t n, std::int64_t d = 1);
    static Rational Decimal(std::string_view value);
    constexpr std::int64_t Numerator() const noexcept
    {
        return m_numerator;
    }
    constexpr std::int64_t Denominator() const noexcept
    {
        return m_denominator;
    }

private:

    std::int64_t m_numerator = 0;
    std::int64_t m_denominator = 1;
};
Rational operator+(Rational a, Rational b);
Rational operator-(Rational a, Rational b);
Rational operator-(Rational a);
Rational operator*(Rational a, Rational b);
Rational operator/(Rational a, Rational b);
bool operator==(Rational a, Rational b);
bool operator!=(Rational a, Rational b);
bool operator<(Rational a, Rational b);
bool operator>(Rational a, Rational b);
bool operator<=(Rational a, Rational b);
using Vector = std::array<Rational, 3>;

struct Quality
{
    std::string id;
    Rational consistencyMm;
    static Quality PlanarFixture(); // Explicit test candidate Q-P0-mm-01; not a production default.
};
struct Limits
{
    std::size_t work = 1000000;
};

struct RuleEvidence
{
    std::string name;
    std::string kind; // WHERE / TYPE_WHERE / UNIQUE / INVERSE / GLOBAL.
    std::string applicability; // Mandatory / Conditional / Guarded.
    std::size_t schemaLine = 0;
    std::optional<std::size_t> owner;
    std::optional<Logical> value; // null: NotRun or Indeterminate, never source UNKNOWN.
    std::string evaluation = "NotRun";
    std::string proof;
    // GLOBAL: all FOR populations. Local instance: its owner; full populations reside in the semantic snapshot.
    std::map<std::string, std::vector<std::size_t>> populations;
    std::vector<std::size_t> witnesses;
    std::map<std::string, std::string> facts;
};
struct Metric
{
    std::string kind;
    std::size_t owner = 0;
    std::vector<std::size_t> witnesses;
    std::map<std::string, Rational> exact;
};
struct EdgeEvidence
{
    std::size_t edge = 0, line = 0, start = 0, end = 0;
    bool sameSense = true;
    // t = coefficient / sqrt(directionNormSquared); t is dimensionless, never scaled twice.
    Rational tStartCoefficient, tEndCoefficient, directionNormSquared, lengthSquaredMm;
};
struct LoopEvidence
{
    std::size_t bound = 0, loop = 0;
    bool outer = false, orientation = true;
    std::vector<std::size_t> orientedEdges, edges, vertices;
    std::vector<bool> edgeOrientations;
    std::vector<std::array<Rational, 2>> projected; // Exact coordinates in the unnormalised orthogonal basis.
    Rational signedAreaCoefficient; // Physical signed area = coefficient * sqrt(metricSquared).
};
struct FaceEvidence
{
    std::size_t face = 0, plane = 0, placement = 0;
    bool sameSense = true;
    Vector originMm{}, x{}, y{}, normal{}; // Basis vectors are exact and deliberately unnormalised.
    Rational sourceLengthScaleMm, xNormSquared, yNormSquared, metricSquared, areaSquaredMm;
    Vector centroidMm{}, minimumMm{}, maximumMm{};
    std::vector<LoopEvidence> loops;
};
struct Evidence
{
    std::shared_ptr<const step_semantics::Snapshot> source;
    std::vector<RuleEvidence> rules;
    std::vector<Metric> metrics;
    std::vector<EdgeEvidence> edges;
    std::vector<FaceEvidence> faces;
};
enum class Status
{
    Checked,
    Rejected,
    Unsupported,
    Deferred,
    Indeterminate,
    ResourceLimit,
    UpstreamFailure
};
struct Outcome
{
    Status status = Status::UpstreamFailure;
    Quality quality;
    Limits limits;
    std::size_t work = 0;
    std::string localRules = "NotRun", globalRules = "NotRun", geometry = "NotRun";
    Evidence evidence; // Partial diagnostic evidence may survive failure; never a model/accepted snapshot.
    std::vector<step_semantics::Diagnostic> diagnostics;
};
Outcome Check(const step_semantics::Outcome& semantics, Quality quality, Limits limits);
bool InspectJson(const step_source::InspectOutcome& source, const step_semantics::Outcome& semantics, const Outcome& planar, std::string& output);
const char* StatusName(Status status);
} // namespace step_planar
