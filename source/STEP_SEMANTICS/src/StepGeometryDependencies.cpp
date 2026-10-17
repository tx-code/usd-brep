// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepGeometryDependencies.h"

#include "StepSourceNumber.h"

#include <iterator>

namespace step_semantics
{
namespace
{
using step_source::Span;
using step_source::Value;
struct Stop
{
    Diagnostic diagnostic;
};
struct SplineDirection
{
    std::string_view degree, multiplicities, knots, countClause, parameterClause;
};
// Keep fields and governing clauses paired; never exchange source U/V to fit a target convention.
constexpr SplineDirection CurveDirection{ "degree", "knot_multiplicities", "knots", "B_SPLINE_CURVE_WITH_KNOTS.WR2", "B_SPLINE_CURVE_WITH_KNOTS.WR1" };
constexpr SplineDirection UDirection{ "u_degree",
                                      "u_multiplicities",
                                      "u_knots",
                                      "B_SPLINE_SURFACE_WITH_KNOTS.WR3",
                                      "B_SPLINE_SURFACE_WITH_KNOTS.WR1" };
constexpr SplineDirection VDirection{ "v_degree",
                                      "v_multiplicities",
                                      "v_knots",
                                      "B_SPLINE_SURFACE_WITH_KNOTS.WR4",
                                      "B_SPLINE_SURFACE_WITH_KNOTS.WR2" };
class GeometryDependencies
{
public:

    GeometryDependencies(const SemanticData& data, const std::function<void(Span)>& visit, GeometryDependencyCounts& checks)
        : m_data(data), m_visit(visit), m_checks(checks)
    {
    }
    void Run()
    {
        Expects(m_data.bindingsComplete);
        for (std::size_t r = 0; r < m_data.fields.size(); ++r)
        {
            const auto& record = m_data.source->Records()[r];
            m_visit(record.span);
            for (const auto& bound : m_data.fields[r].attributes)
            {
                const auto& field = bound.declaration;
                const bool positive = field.declaredType == "POSITIVE_LENGTH_MEASURE";
                const bool coneRadius = record.components[bound.component].name == "CONICAL_SURFACE" && field.name == "radius";
                if (!positive && !coneRadius)
                {
                    continue;
                }
                Count(field.value->span, positive ? PredicateKind::Type : PredicateKind::Entity);
                // Pinned AP214: positive lengths are >0; CONICAL_SURFACE.WR1 allows
                // radius0. Its angle has no extra declaration bound: never clamp it.
                const auto order = CompareSourceReals(field.value->text, "0.");
                if (positive ? order <= 0 : order < 0)
                {
                    Fail(
                        r,
                        bound,
                        *field.value,
                        positive ? "POSITIVE_LENGTH_MEASURE.WR1" : "CONICAL_SURFACE.WR1",
                        positive ? ">0" : ">=0",
                        field.value->text
                    );
                }
            }
            if (IsA(r, "B_SPLINE_CURVE_WITH_KNOTS"))
            {
                Direction(r, Field(r, "control_points_list").declaration.value->children.size(), CurveDirection);
            }
            if (IsA(r, "B_SPLINE_SURFACE_WITH_KNOTS"))
            {
                const auto& bound = Field(r, "control_points_list");
                const auto& grid = *bound.declaration.value;
                const auto width = grid.children[0].children.size(); // Typed LIST minima checked before this phase.
                Count(grid.span, PredicateKind::Dependency);
                for (std::size_t i = 0; i < grid.children.size(); ++i)
                {
                    const auto& row = grid.children[i];
                    m_visit(row.span);
                    if (row.children.size() != width)
                    {
                        // DERIVE uses one v_upper from the first row. Do not truncate,
                        // pad or transpose a ragged source into another surface.
                        Fail(
                            r,
                            bound,
                            row,
                            "B_SPLINE_SURFACE.control_points DERIVE",
                            std::to_string(width),
                            std::to_string(row.children.size()),
                            { i + 1 },
                            true
                        );
                    }
                }
                Direction(r, grid.children.size(), UDirection);
                Direction(r, width, VDirection);
            }
        }
    }

private:

    const SemanticData& m_data;
    const std::function<void(Span)>& m_visit;
    GeometryDependencyCounts& m_checks;
    enum class PredicateKind
    {
        Dependency,
        Entity,
        Type
    };
    bool IsA(std::size_t r, std::string_view type) const
    {
        const auto index = Schema(m_data).Find(type);
        return index && m_data.types[r].test(*index);
    }
    const BoundAttribute& Field(std::size_t r, std::string_view name) const
    {
        const auto& fields = m_data.fields[r];
        const auto found = fields.named.equal_range(name);
        // Names below are pinned descriptor fields after complete typed binding.
        // This is a catalog/programmer invariant, never a bad-STEP precondition.
        Expects(found.first != found.second && std::next(found.first) == found.second);
        return fields.attributes[found.first->second];
    }
    void Count(Span span, PredicateKind kind = PredicateKind::Entity)
    {
        m_visit(span); // Budget before decimal work or counting an attempted predicate.
        ++m_checks.total;
        if (kind == PredicateKind::Type)
        {
            ++m_checks.typePredicates;
        }
        if (kind == PredicateKind::Entity)
        {
            ++m_checks.entityPredicates;
        }
    }
    [[noreturn]] void Fail(
        std::size_t r,
        const BoundAttribute& bound,
        const Value& value,
        std::string_view clause,
        std::string expected,
        std::string actual,
        std::vector<std::size_t> suffix = {},
        bool dependency = false
    )
    {
        const auto& record = m_data.source->Records()[r];
        const auto& field = bound.declaration;
        Diagnostic issue;
        issue.source.key = dependency ? "SourceGeometryDependencyViolation" : "SourceRuleViolation:" + std::string(clause);
        issue.source.stage = "D3";
        issue.source.message = "Source geometry parameter violates the selected AP214 prerequisite";
        issue.source.primary = value.span;
        issue.source.recordId = record.id;
        issue.source.component = record.components[bound.component].name;
        issue.source.parameterPath = { bound.parameter + 1 };
        issue.source.parameterPath.insert(issue.source.parameterPath.end(), suffix.begin(), suffix.end());
        issue.classification = "SourceInvalid";
        issue.field = field.name;
        issue.declaredIn = field.declaredIn;
        issue.declaredType = field.declaredType;
        issue.expected = std::move(expected);
        issue.actual = std::move(actual);
        issue.responsibility = "AP214E3_2010.exp " + std::string(clause);
        throw Stop{ std::move(issue) };
    }
    void Direction(std::size_t r, std::size_t controls, const SplineDirection& direction)
    {
        const auto& mult = Field(r, direction.multiplicities);
        const auto& knots = Field(r, direction.knots);
        const auto& multiplicities = mult.declaration.value->children;
        const auto& values = knots.declaration.value->children;
        Count(mult.declaration.value->span);
        // Size clauses precede the parameter function's indexing; preserve the
        // distinct knot list and multiplicities without expansion/normalization.
        if (multiplicities.size() != values.size())
        {
            Fail(r, mult, *mult.declaration.value, direction.countClause, std::to_string(values.size()), std::to_string(multiplicities.size()));
        }
        const auto& degreeField = Field(r, direction.degree);
        const auto& degreeValue = *degreeField.declaration.value;
        Count(degreeValue.span);
        const auto degree = SourceIndexAtMost(degreeValue.text, controls - 1);
        if (!degree || *degree < 1)
        {
            Fail(r, degreeField, degreeValue, direction.parameterClause, "1 <= degree <= control count - 1", degreeValue.text);
        }
        // AP214 constraints_param_b_spline, pinned lines12267ff. End multiplicities
        // <=degree+1, interior <=degree; sum=control count+degree+1. Source node/byte
        // limits bound these size sums; large source integers are bounded before conversion.
        const auto required = controls + *degree + 1;
        std::size_t sum = 0;
        for (std::size_t i = 0; i < multiplicities.size(); ++i)
        {
            const auto& value = multiplicities[i];
            m_visit(value.span);
            const auto maximum = (i == 0 || i + 1 == multiplicities.size()) ? *degree + 1 : *degree;
            const auto n = SourceIndexAtMost(value.text, maximum);
            if (!n || *n < 1 || *n > required - sum)
            {
                Fail(r, mult, value, direction.parameterClause, "positive bounded knot multiplicity with required total", value.text, { i + 1 });
            }
            sum += *n;
        }
        if (sum != required)
        {
            Fail(r, mult, *mult.declaration.value, direction.parameterClause, std::to_string(required), std::to_string(sum));
        }
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            m_visit(values[i].span);
            if (i && CompareSourceReals(values[i - 1].text, values[i].text) >= 0)
            {
                Fail(r, knots, values[i], direction.parameterClause, "strictly increasing distinct source knots", values[i].text, { i + 1 });
            }
        }
    }
};
} // namespace
std::optional<Diagnostic> CheckAp214GeometryDependencies(
    const SemanticData& data,
    const std::function<void(step_source::Span)>& visit,
    GeometryDependencyCounts& checks
)
{
    try
    {
        GeometryDependencies(data, visit, checks).Run();
        return {};
    }
    catch (Stop& stop)
    {
        return std::move(stop.diagnostic);
    }
}
} // namespace step_semantics
