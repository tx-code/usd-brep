// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepPlanar.h"
#include "StepRuleCatalog.h"

#include <array>
#include <optional>
#include <stdexcept>

namespace step_planar
{
using Value = step_source::Value;
struct Stop
{
    Status status;
};
Vector Add(Vector a, Vector b);
Vector Subtract(Vector a, Vector b);
Vector Times(Vector a, Rational factor);
Rational Dot(Vector a, Vector b);
Vector Cross(Vector a, Vector b);
Rational Squared(Vector a);

class Validator
{
public:

    Validator(Outcome& outcome, const step_semantics::Snapshot& source);
    void Run();
    void PublishProgress(Status status);
    step_semantics::Diagnostic NumericDiagnostic(const char* key) const;

private:

    enum class Phase
    {
        Local,
        Global,
        Geometry
    };
    enum class Progress
    {
        NotRun,
        Running,
        Passed,
        Failed,
        Indeterminate,
        ResourceLimit
    };
    Outcome& out;
    const step_semantics::Snapshot& sem;
    const step_source::SourceSnapshot& src;
    std::size_t current = 0;
    std::optional<Phase> phase;
    std::array<Progress, 3> progress{};
    Progress& State(Phase selected);
    void RunPhase(Phase selected, void (Validator::*operation)());
    static const char* ProgressName(Progress value);
    void Local();
    void GlobalRules();
    void Geometry();
    const step_source::Record& Record(std::size_t r) const;
    const Value& Field(std::size_t r, const std::string& name) const;
    std::size_t Target(const Value& value) const;
    std::size_t Ref(std::size_t r, const std::string& name) const;
    std::vector<std::size_t> Refs(std::size_t r, const std::string& name) const;
    std::vector<std::size_t> Population(std::string_view type) const;
    bool Is(std::size_t r, std::string_view type) const;
    bool Sense(std::size_t r, const std::string& field) const;
    std::size_t Role(std::string_view name) const;
    void Tick(std::size_t owner);
    [[noreturn]] void Fail(
        std::string key,
        std::string stage,
        std::size_t owner,
        std::string field,
        std::string responsibility,
        Status status = Status::Rejected
    );
    Vector Components(std::size_t r, const std::string& field) const;
    Vector Point(std::size_t r) const;
    Vector Direction(std::size_t r) const;
    std::pair<std::size_t, std::size_t> Ends(std::size_t oe) const;
    bool LocalValue(const Clause& clause, std::size_t r, RuleEvidence& evidence);
    RuleEvidence GlobalValue(const Global& rule);
    void Edge(std::size_t r, Rational scale);
    std::pair<Vector, Vector> Axes(std::size_t placement, Vector& normal);
    FaceEvidence Face(std::size_t r, Rational scale);
    void Shell();
};
} // namespace step_planar
