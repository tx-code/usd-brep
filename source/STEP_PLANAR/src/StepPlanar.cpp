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
    fields.resize(src.Records().size());
    for (std::size_t r = 0; r < src.Records().size(); ++r)
    {
        Tick(r);
        ids.emplace(Record(r).id, r);
        for (std::size_t c = 0; c < Record(r).components.size(); ++c)
        {
            for (std::size_t p = 0; p < Record(r).components[c].parameters.size(); ++p)
            {
                const auto declaration = sem.Attribute(r, c, p);
                fields[r].emplace(std::string(declaration.name), BoundField{ declaration.value, c, p, declaration });
            }
        }
    }
}
const step_source::Record& Validator::Record(std::size_t r) const
{
    return src.Records().at(r);
}
const Value& Validator::Field(std::size_t r, const std::string& name) const
{
    return *fields.at(r).at(name).value;
}
std::size_t Validator::Target(const Value& v) const
{
    return ids.at(v.text);
}
std::size_t Validator::Ref(std::size_t r, const std::string& name) const
{
    return Target(Field(r, name));
}
std::vector<std::size_t> Validator::Refs(std::size_t r, const std::string& name) const
{
    std::vector<std::size_t> result;
    for (const auto& v : Field(r, name).children)
    {
        result.push_back(Target(v));
    }
    return result;
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
        const auto& binding = fields.at(owner).at(field);
        diagnostic.source.primary = binding.value->span;
        diagnostic.source.component = Record(owner).components[binding.component].name;
        diagnostic.source.parameterPath = { binding.parameter + 1 };
        diagnostic.declaredIn = binding.declaration.declaredIn;
        diagnostic.declaredType = binding.declaration.declaredType;
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
    diagnostic.source.stage = out.localRules == "Running" ? "D3" : "D4";
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
    if (quality.id.empty() || quality.consistencyMm.denominator <= 0 || quality.consistencyMm.numerator <= 0 || limits.work > 10000000)
    {
        throw std::invalid_argument("Explicit positive quality and bounded work configuration required");
    }
    quality.consistencyMm = Rational(quality.consistencyMm.numerator, quality.consistencyMm.denominator);
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
        result.localRules = "Running";
        validator->Local();
        result.localRules = "Passed";
        result.globalRules = "Running";
        validator->GlobalRules();
        result.globalRules = "Passed";
        result.geometry = "Running";
        validator->Geometry();
        result.geometry = "Passed";
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
    const std::string failure = result.status == Status::Indeterminate ? "Indeterminate" :
                                result.status == Status::ResourceLimit ? "ResourceLimit" :
                                                                         "Failed";
    for (auto* phase : { &result.localRules, &result.globalRules, &result.geometry })
    {
        if (*phase == "Running")
        {
            *phase = failure;
        }
    }
    if (result.status == Status::Indeterminate && !result.evidence.rules.empty() && !result.evidence.rules.back().value)
    {
        result.evidence.rules.back().evaluation = "Indeterminate";
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
