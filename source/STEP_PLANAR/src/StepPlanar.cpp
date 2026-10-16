// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepPlanarInternal.h"

#include <algorithm>
#include <limits>

namespace step_planar
{
Vector Add(Vector a, Vector b)
{
    for (std::size_t i = 0; i < 3; ++i)
    {
        a[i] = a[i] + b[i];
    }
    return a;
}
Vector Subtract(Vector a, Vector b)
{
    for (std::size_t i = 0; i < 3; ++i)
    {
        a[i] = a[i] - b[i];
    }
    return a;
}
Vector Times(Vector a, Rational b)
{
    for (auto& c : a)
    {
        c = c * b;
    }
    return a;
}
Rational Dot(Vector a, Vector b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vector Cross(Vector a, Vector b)
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
Rational Squared(Vector a)
{
    return Dot(a, a);
}

Validator::Validator(Outcome& outcome, const step_semantics::Snapshot& source) : out(outcome), sem(source), src(source.Source())
{
    // Preserve the established per-record admission charge; bindings now belong to the semantic snapshot.
    for (std::size_t r = 0; r < src.Records().size(); ++r)
    {
        Tick(r);
    }
}
Validator::Progress& Validator::State(Phase selected)
{
    return progress.at(static_cast<std::size_t>(selected));
}
void Validator::RunPhase(Phase selected, void (Validator::*operation)())
{
    if (phase || State(selected) != Progress::NotRun)
    {
        throw std::logic_error("Planar phase cannot be entered twice or recursively");
    }
    phase = selected;
    State(selected) = Progress::Running;
    (this->*operation)();
    State(selected) = Progress::Passed;
    phase.reset();
}
void Validator::Run()
{
    RunPhase(Phase::Local, &Validator::Local);
    RunPhase(Phase::Global, &Validator::GlobalRules);
    RunPhase(Phase::Geometry, &Validator::Geometry);
}
const char* Validator::ProgressName(Progress value)
{
    switch (value)
    {
        case Progress::NotRun:
            return "NotRun";
        case Progress::Running:
            return "Running";
        case Progress::Passed:
            return "Passed";
        case Progress::Failed:
            return "Failed";
        case Progress::Indeterminate:
            return "Indeterminate";
        case Progress::ResourceLimit:
            return "ResourceLimit";
    }
    throw std::logic_error("Unknown planar phase progress");
}
void Validator::PublishProgress(Status status)
{
    const auto failure = status == Status::Indeterminate ? Progress::Indeterminate :
                         status == Status::ResourceLimit ? Progress::ResourceLimit :
                                                           Progress::Failed;
    for (auto& state : progress)
    {
        if (state == Progress::Running)
        {
            state = failure;
        }
    }
    out.localRules = ProgressName(State(Phase::Local));
    out.globalRules = ProgressName(State(Phase::Global));
    out.geometry = ProgressName(State(Phase::Geometry));
    if (status == Status::Indeterminate && !out.evidence.rules.empty() && !out.evidence.rules.back().value)
    {
        out.evidence.rules.back().evaluation = "Indeterminate";
    }
}
const step_source::Record& Validator::Record(std::size_t r) const
{
    return src.Records().at(r);
}
const Value& Validator::Field(std::size_t r, const std::string& name) const
{
    return sem.Field(r, name).Value();
}
std::size_t Validator::Target(const Value& v) const
{
    return sem.ReferenceTarget(v);
}
std::size_t Validator::Ref(std::size_t r, const std::string& name) const
{
    return sem.Field(r, name).ReferenceTarget();
}
std::vector<std::size_t> Validator::Refs(std::size_t r, const std::string& name) const
{
    return sem.Field(r, name).ReferenceTargets();
}
std::vector<std::size_t> Validator::Population(std::string_view type) const
{
    const auto* population = sem.Instances(type);
    if (!population)
    {
        throw std::logic_error("Unregistered population outside an explicit closed-profile guard");
    }
    return *population;
}
bool Validator::Is(std::size_t r, std::string_view type) const
{
    return sem.IsA(r, type);
}
bool Validator::Sense(std::size_t r, const std::string& name) const
{
    return Field(r, name).text == "T";
}
std::size_t Validator::Role(std::string_view name) const
{
    for (const auto& role : sem.PartDefinition().roles)
    {
        if (role.name == name)
        {
            return role.recordIndex;
        }
    }
    throw std::logic_error("Missing validated part role");
}
void Validator::Tick(std::size_t owner)
{
    current = owner;
    if (out.work >= out.limits.work)
    {
        Fail("PlanarWorkLimit", "Resource", owner, "", "bounded source-rule and polygon work", Status::ResourceLimit);
    }
    ++out.work;
}
void Validator::Fail(std::string key, std::string stage, std::size_t owner, std::string field, std::string responsibility, Status status)
{
    step_semantics::Diagnostic diagnostic;
    diagnostic.source.key = std::move(key);
    diagnostic.source.stage = std::move(stage);
    diagnostic.source.message = responsibility;
    diagnostic.source.recordId = Record(owner).id;
    diagnostic.source.primary = Record(owner).span;
    diagnostic.field = field;
    if (!field.empty())
    {
        const auto binding = sem.Field(owner, field);
        auto location = binding.Location();
        diagnostic.source.recordId = std::move(location.recordId);
        diagnostic.source.primary = location.span;
        diagnostic.source.component = std::move(location.component);
        diagnostic.source.parameterPath = std::move(location.parameterPath);
        diagnostic.declaredIn = binding.Declaration().declaredIn;
        diagnostic.declaredType = binding.Declaration().declaredType;
    }
    diagnostic.classification = status == Status::Rejected      ? "SourceInvalid" :
                                status == Status::Unsupported   ? "ProfileUnsupported" :
                                status == Status::ResourceLimit ? "ResourceLimit" :
                                                                  "Indeterminate";
    diagnostic.responsibility = std::move(responsibility);
    out.diagnostics.push_back(std::move(diagnostic));
    throw Stop{ status };
}
Vector Validator::Components(std::size_t r, const std::string& name) const
{
    const auto& values = Field(r, name).children;
    if (values.size() != 3)
    {
        throw std::domain_error("ThreeDimensionalComponentsRequired");
    }
    return { Rational::Decimal(values[0].text), Rational::Decimal(values[1].text), Rational::Decimal(values[2].text) };
}
Vector Validator::Point(std::size_t r) const
{
    return Components(r, "coordinates");
}
Vector Validator::Direction(std::size_t r) const
{
    return Components(r, "direction_ratios");
}
step_semantics::Diagnostic Validator::NumericDiagnostic(const char* key) const
{
    step_semantics::Diagnostic diagnostic;
    diagnostic.source.key = key;
    // Keep the existing wire convention: only local numeric evidence is labelled D3.
    diagnostic.source.stage = phase == Phase::Local ? "D3" : "D4";
    diagnostic.source.recordId = Record(current).id;
    diagnostic.source.primary = Record(current).span;
    diagnostic.source.message = "Required exact evidence is unestablished; no rounded or invented fallback";
    diagnostic.classification = "Indeterminate";
    diagnostic.responsibility = "numeric/derived evidence, not a proved source violation";
    return diagnostic;
}
std::pair<std::size_t, std::size_t> Validator::Ends(std::size_t oe) const
{
    const auto ec = Ref(oe, "edge_element");
    if (!Is(ec, "EDGE_CURVE"))
    {
        throw std::domain_error("UnsupportedOrientedEdgeElement");
    }
    const auto start = Ref(ec, "edge_start"), end = Ref(ec, "edge_end");
    return Sense(oe, "orientation") ? std::make_pair(start, end) : std::make_pair(end, start);
}

Outcome Check(const step_semantics::Outcome& semantics, Quality quality, Limits limits)
{
    if (quality.id.empty() || quality.consistencyMm.Denominator() <= 0 || quality.consistencyMm.Numerator() <= 0 || limits.work > 10000000)
    {
        throw std::invalid_argument("Explicit positive quality and bounded work configuration required");
    }
    quality.consistencyMm = Rational(quality.consistencyMm.Numerator(), quality.consistencyMm.Denominator());
    if (quality.id == "Q-P0-mm-01" && quality.consistencyMm != Rational(1, 10000000))
    {
        throw std::invalid_argument("Frozen quality identity cannot be reused for different parameters");
    }
    Outcome result;
    result.quality = std::move(quality);
    result.limits = limits;
    if (semantics.status != step_semantics::Status::Checked && semantics.status != step_semantics::Status::Deferred)
    {
        result.diagnostics = semantics.diagnostics;
        return result;
    }
    if (!semantics.snapshot)
    {
        throw std::invalid_argument("Missing complete semantic snapshot");
    }
    result.evidence.source = semantics.snapshot;
    if (semantics.status == step_semantics::Status::Deferred)
    {
        result.status = Status::Deferred;
        return result;
    }
    std::unique_ptr<Validator> validator;
    try
    {
        validator = std::make_unique<Validator>(result, *semantics.snapshot);
        validator->Run();
        result.status = Status::Checked;
    }
    catch (const Stop& stop)
    {
        result.status = stop.status;
    }
    catch (const std::overflow_error& error)
    {
        result.status = Status::Indeterminate;
        if (!validator)
        {
            throw;
        }
        result.diagnostics.push_back(validator->NumericDiagnostic(error.what()));
    }
    catch (const std::domain_error& error)
    {
        result.status = Status::Indeterminate;
        if (!validator)
        {
            throw;
        }
        result.diagnostics.push_back(validator->NumericDiagnostic(error.what()));
    }
    if (validator)
    {
        validator->PublishProgress(result.status);
    }
    return result;
}
const char* StatusName(Status status)
{
    switch (status)
    {
        case Status::Checked:
            return "Checked";
        case Status::Rejected:
            return "Rejected";
        case Status::Unsupported:
            return "Unsupported";
        case Status::Deferred:
            return "Deferred";
        case Status::Indeterminate:
            return "Indeterminate";
        case Status::ResourceLimit:
            return "ResourceLimit";
        case Status::UpstreamFailure:
            return "UpstreamFailure";
    }
    throw std::logic_error("Unknown planar status");
}
} // namespace step_planar
