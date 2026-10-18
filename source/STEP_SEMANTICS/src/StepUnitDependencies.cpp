// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepUnitDependencies.h"

#include "StepSourceNumber.h"
#include "StepUnitRational.h"

#include <algorithm>
#include <iterator>
#include <limits>

namespace step_semantics
{
namespace
{
using unit_dimensions::Rational;
using Dimensions = std::array<Rational, 7>;
using step_source::Span;
using step_source::Value;
constexpr auto NoUnit = std::numeric_limits<std::size_t>::max();
constexpr std::string_view DimensionFields[] = { "length_exponent",
                                                 "mass_exponent",
                                                 "time_exponent",
                                                 "electric_current_exponent",
                                                 "thermodynamic_temperature_exponent",
                                                 "amount_of_substance_exponent",
                                                 "luminous_intensity_exponent" };
struct Stop
{
    UnitDependencyFailure failure;
};
struct Edge
{
    // Completed field bindings/source values outlive this synchronous pass and
    // remain immutable; dimension publication never reallocates binding storage.
    std::size_t target, owner;
    const BoundAttribute* field;
};
struct Node
{
    std::size_t record;
    std::vector<Edge> edges;
    Dimensions dimensions;
};
class UnitDependencies
{
public:

    UnitDependencies(SemanticData& data, const std::function<void(Span)>& visit, UnitDependencyCounts& counts)
        : m_data(data), m_visit(visit), m_counts(counts), m_index(data.fields.size(), NoUnit)
    {
    }
    void Run()
    {
        Expects(m_data.bindingsComplete);
        for (std::size_t r = 0; r < m_data.fields.size(); ++r)
        {
            m_visit(Record(r).span);
            if (IsA(r, "NAMED_UNIT") || IsA(r, "DERIVED_UNIT"))
            {
                m_index[r] = m_nodes.size();
                m_nodes.push_back({ r, {}, {} });
            }
        }
        for (auto& node : m_nodes)
        {
            if (IsA(node.record, "DERIVED_UNIT"))
            {
                for (const auto& element : Field(node.record, "elements").declaration.value->children)
                {
                    m_visit(element.span);
                    const auto owner = Target(element);
                    const auto& bound = Field(owner, "unit");
                    node.edges.push_back({ Index(Target(*bound.declaration.value)), owner, &bound });
                }
            }
            else if (IsA(node.record, "CONVERSION_BASED_UNIT"))
            {
                const auto owner = Target(*Field(node.record, "conversion_factor").declaration.value);
                const auto& bound = Field(owner, "unit_component");
                m_visit(bound.declaration.value->span);
                node.edges.push_back({ Index(Target(*bound.declaration.value)), owner, &bound });
            }
        }
        // The graph includes conversion-factor -> derived-unit paths. Evaluate
        // with explicit DFS frames: source depth must not become C++ stack depth.
        // A zero coefficient does not erase a source cycle or unit reference.
        std::vector<unsigned char> state(m_nodes.size());
        struct Frame
        {
            std::size_t node, next;
        };
        std::vector<Frame> stack;
        for (std::size_t start = 0; start < m_nodes.size(); ++start)
        {
            if (state[start] == 2)
            {
                continue;
            }
            state[start] = 1;
            stack.push_back({ start, 0 });
            while (!stack.empty())
            {
                auto& frame = stack.back();
                auto& node = m_nodes[frame.node];
                m_visit(Record(node.record).span);
                if (frame.next < node.edges.size())
                {
                    const auto& edge = node.edges[frame.next++];
                    m_visit(edge.field->declaration.value->span);
                    if (state[edge.target] == 1)
                    {
                        Fail(
                            Status::Rejected,
                            edge.owner,
                            *edge.field,
                            "SourceUnitDependencyCycle",
                            "finite dimensional dependency graph",
                            Record(m_nodes[edge.target].record).id,
                            "source dimensional dependency graph"
                        );
                    }
                    if (!state[edge.target])
                    {
                        state[edge.target] = 1;
                        stack.push_back({ edge.target, 0 });
                    }
                    continue;
                }
                Evaluate(node);
                ++m_counts.unitDefinitions;
                state[frame.node] = 2;
                stack.pop_back();
            }
        }
        Measures();
        // Publish these derived facts only after the entire unit pass completes.
        // The surrounding Check still withholds the owning snapshot if later phases fail.
        for (const auto& node : m_nodes)
        {
            UnitDimensionDefinition result;
            result.recordIndex = node.record;
            result.nominal = Nominal(node.record);
            for (std::size_t i = 0; i < 7; ++i)
            {
                result.dimensions[i] = { std::to_string(node.dimensions[i].n), std::to_string(node.dimensions[i].d) };
            }
            m_data.units.dimensionDefinitions.push_back(std::move(result));
        }
    }

private:

    SemanticData& m_data;
    const std::function<void(Span)>& m_visit;
    UnitDependencyCounts& m_counts;
    std::vector<std::size_t> m_index;
    std::vector<Node> m_nodes;
    const step_source::Record& Record(std::size_t r) const
    {
        return m_data.source->Records()[r];
    }
    bool IsA(std::size_t r, std::string_view name) const
    {
        const auto type = Schema(m_data).Find(name);
        return type && m_data.types[r].test(*type);
    }
    const BoundAttribute& Field(std::size_t r, std::string_view name) const
    {
        const auto range = m_data.fields[r].named.equal_range(name);
        Expects(range.first != range.second && std::next(range.first) == range.second);
        return m_data.fields[r].attributes[range.first->second];
    }
    std::size_t Target(const Value& value) const
    {
        return m_data.referenceTargets.at(&value);
    }
    std::size_t Index(std::size_t record) const
    {
        Expects(m_index[record] != NoUnit);
        return m_index[record];
    }
    void PreservePending(Stop&& stop, std::size_t record, const BoundAttribute& field)
    {
        // Provisional only: the outer checker still has to prove property
        // attachment and exclude the body's entire physical unit closure.
        if (m_data.pendingIssues.size() >= m_data.preservedDiagnosticLimit)
        {
            Fail(
                Status::ResourceLimit,
                record,
                field,
                "PreservedDiagnosticLimit",
                "configured preserved diagnostic capacity",
                std::to_string(m_data.pendingIssues.size() + 1),
                "explicit interoperability capacity; no diagnostics silently discarded"
            );
        }
        m_data.pendingIssues.push_back({ stop.failure.status, std::move(stop.failure.diagnostic) });
    }
    std::string Nominal(std::size_t r) const
    {
        if (IsA(r, "DERIVED_UNIT"))
        {
            return "Derived";
        }
        if (IsA(r, "LENGTH_UNIT"))
        {
            return "Length";
        }
        if (IsA(r, "MASS_UNIT"))
        {
            return "Mass";
        }
        if (IsA(r, "PLANE_ANGLE_UNIT"))
        {
            return "PlaneAngle";
        }
        if (IsA(r, "SOLID_ANGLE_UNIT"))
        {
            return "SolidAngle";
        }
        return "Named";
    }
    void Count(Span span)
    {
        m_visit(span);
        ++m_counts.total;
    }
    [[noreturn]] void Fail(
        Status status,
        std::size_t r,
        const BoundAttribute& bound,
        std::string key,
        std::string expected,
        std::string actual,
        std::string responsibility
    )
    {
        Diagnostic issue;
        const auto& field = bound.declaration;
        issue.source.key = std::move(key);
        issue.source.stage = status == Status::Unsupported ? "D2" : "D3";
        issue.source.message = m_data.profile.kind == ProfileKind::Ap203e2GeneralSource ?
                                   "Selected N2635 unit dependency or dimension check did not complete" :
                                   "Selected AP214 unit dependency or dimension check did not complete";
        issue.source.primary = field.value->span;
        issue.source.recordId = Record(r).id;
        issue.source.component = Record(r).components[bound.component].name;
        issue.source.parameterPath = { bound.parameter + 1 };
        issue.classification = status == Status::ResourceLimit ? "ResourceLimit" :
                               status == Status::Unsupported   ? "ProfileUnsupported" :
                                                                 "SourceInvalid";
        issue.field = field.name;
        issue.declaredIn = field.declaredIn;
        issue.declaredType = field.declaredType;
        issue.expected = std::move(expected);
        issue.actual = std::move(actual);
        issue.responsibility = std::move(responsibility);
        throw Stop{ { status, std::move(issue) } };
    }
    [[noreturn]] void Rule(std::size_t r, const BoundAttribute& field, std::string_view clause)
    {
        Fail(
            Status::Rejected,
            r,
            field,
            "SourceRuleViolation:" + std::string(clause),
            "declared dimensional/source predicate",
            std::string(m_data.source->Raw(field.declaration.value->span)),
            std::string(m_data.profile.kind == ProfileKind::Ap203e2GeneralSource ? "N2635 " : "AP214E3_2010.exp ") + std::string(clause)
        );
    }
    [[noreturn]] void Capacity(std::size_t r, const BoundAttribute& field)
    {
        Fail(
            Status::ResourceLimit,
            r,
            field,
            "UnitDimensionCapacityExceeded",
            "exact bounded rational capacity",
            field.declaration.value->text,
            "unit dimension arithmetic capacity; not a source-invalid verdict or tolerance"
        );
    }
    Rational Decimal(std::size_t r, const BoundAttribute& field)
    {
        m_visit(field.declaration.value->span);
        try
        {
            return Rational::Decimal(field.declaration.value->text);
        }
        catch (unit_dimensions::Capacity&)
        {
            Capacity(r, field);
        }
    }
    static Dimensions Basis(std::size_t component)
    {
        Dimensions result{};
        result[component] = Rational(1);
        return result;
    }
    void Evaluate(Node& node)
    {
        const auto r = node.record;
        if (IsA(r, "DERIVED_UNIT"))
        {
            const auto& elements = Field(r, "elements");
            Count(elements.declaration.value->span);
            if (node.edges.size() == 1)
            {
                const auto& exponent = Field(node.edges[0].owner, "exponent");
                if (CompareSourceReals(exponent.declaration.value->text, "1.") == 0)
                {
                    Rule(r, elements, "DERIVED_UNIT.WR1");
                }
            }
            for (const auto& edge : node.edges)
            {
                const auto& dims = m_nodes[edge.target].dimensions;
                const auto& exponent = Field(edge.owner, "exponent");
                // All-zero bases contribute exactly zero even for a large exponent.
                // This does not remove the original element or its SI-prefix obligations.
                if (std::all_of(
                        dims.begin(),
                        dims.end(),
                        [](auto value)
                        {
                            return value.n == 0;
                        }
                    ))
                {
                    m_visit(exponent.declaration.value->span);
                    continue;
                }
                const auto power = Decimal(edge.owner, exponent);
                for (std::size_t i = 0; i < 7; ++i)
                {
                    m_visit(exponent.declaration.value->span);
                    try
                    {
                        node.dimensions[i] = unit_dimensions::Add(node.dimensions[i], unit_dimensions::Multiply(power, dims[i]));
                    }
                    catch (unit_dimensions::Capacity&)
                    {
                        Capacity(edge.owner, exponent);
                    }
                }
            }
        }
        else if (IsA(r, "SI_UNIT"))
        {
            const auto& name = Field(r, "name");
            const auto& text = name.declaration.value->text;
            if (text == "METRE")
            {
                node.dimensions = Basis(0);
            }
            else if (text == "GRAM")
            {
                node.dimensions = Basis(1);
            }
            else if (text != "RADIAN" && text != "STERADIAN")
            {
                Fail(
                    Status::Unsupported,
                    r,
                    name,
                    "UnsupportedUnitDimensionKind",
                    "metre/gram/radian/steradian source subset",
                    text,
                    "profile SI dimensional interpretation subset"
                );
            }
            // SI_UNIT.WR1 tests actual references, including elements with exponent0.
            // Never prune them before enforcing the mass/KILO source requirement.
            if (IsA(r, "MASS_UNIT"))
            {
                bool used = false;
                for (const auto incoming : m_data.source->Incoming(Record(r).id))
                {
                    const auto& reference = m_data.source->References()[incoming];
                    m_visit(reference.span);
                    used = used || IsA(reference.recordIndex, "DERIVED_UNIT_ELEMENT");
                }
                if (used)
                {
                    const auto& prefix = Field(r, "prefix");
                    Count(prefix.declaration.value->span);
                    if (prefix.declaration.value->text != "KILO")
                    {
                        try
                        {
                            Rule(r, prefix, "SI_UNIT.WR1");
                        }
                        catch (Stop& stop)
                        {
                            // AP214 SI_UNIT.WR1 is a real source error when a
                            // gram mass unit is used directly in a derived unit.
                            // Its dimensions are already known (mass), so retain
                            // the original prefix/error provisionally for a
                            // nonphysical property; never change GRAM to KILO.
                            if (!IsAp214SourceProfile(m_data.profile.kind) || m_data.policy != SourcePolicy::PreserveNonGeometric || text != "GRAM")
                            {
                                throw;
                            }
                            PreservePending(std::move(stop), r, prefix);
                        }
                    }
                }
            }
        }
        else if (IsA(r, "CONVERSION_BASED_UNIT"))
        {
            // AP214 DERIVE follows the factor unit's dimensions, not its numeric
            // magnitude and not an invented NAMED_UNIT.dimensions reference.
            node.dimensions = m_nodes[node.edges[0].target].dimensions;
            if (IsAp214SourceProfile(m_data.profile.kind))
            {
                const auto& dimensions = Field(r, "dimensions");
                if (dimensions.declaration.value->kind == step_source::ValueKind::Reference)
                {
                    // The checker bound this nonconforming reference only for
                    // the explicit property-only mass exception. It is NOT an
                    // AP214 transport value. Before retaining its marker error,
                    // require its seven source exponents to agree with the
                    // factor-derived vector; never hide contradictory metadata.
                    const auto sourceDimensions = Target(*dimensions.declaration.value);
                    Dimensions explicitDimensions;
                    for (std::size_t i = 0; i < 7; ++i)
                    {
                        explicitDimensions[i] = Decimal(sourceDimensions, Field(sourceDimensions, DimensionFields[i]));
                    }
                    Count(dimensions.declaration.value->span);
                    if (explicitDimensions != node.dimensions)
                    {
                        Fail(
                            Status::Rejected,
                            r,
                            dimensions,
                            "MetadataMassUnitDimensionsConflict",
                            "factor-derived dimensional vector",
                            "contradictory explicit source vector",
                            "property-only compatibility guard; no contradictory unit dimensions retained"
                        );
                    }
                }
            }
            if (m_data.profile.kind == ProfileKind::Ap203e2GeneralSource)
            {
                // N2635 keeps explicit NAMED_UNIT.dimensions, unlike AP214's
                // redeclared marker. Its CBU.WR1 must agree with the factor unit.
                const auto& bound = Field(r, "dimensions");
                const auto sourceDimensions = Target(*bound.declaration.value);
                Dimensions explicitDimensions;
                for (std::size_t i = 0; i < 7; ++i)
                {
                    explicitDimensions[i] = Decimal(sourceDimensions, Field(sourceDimensions, DimensionFields[i]));
                }
                Count(bound.declaration.value->span);
                if (explicitDimensions != node.dimensions)
                {
                    Rule(r, bound, "CONVERSION_BASED_UNIT.WR1");
                }
                const auto factor = Target(*Field(r, "conversion_factor").declaration.value);
                const auto& value = Field(factor, "value_component");
                Count(value.declaration.value->span);
                if (CompareSourceReals(value.declaration.value->children.front().text, "0.") <= 0)
                {
                    Fail(
                        Status::Rejected,
                        factor,
                        value,
                        "SourceUnitScaleNonPositive",
                        ">0",
                        value.declaration.value->children.front().text,
                        "positive physical conversion-scale guard"
                    );
                }
            }
        }
        else
        {
            const auto dimensions = Target(*Field(r, "dimensions").declaration.value);
            for (std::size_t i = 0; i < 7; ++i)
            {
                node.dimensions[i] = Decimal(dimensions, Field(dimensions, DimensionFields[i]));
            }
        }
        if (!IsA(r, "DERIVED_UNIT"))
        {
            const auto nominal = Nominal(r);
            Dimensions required{};
            std::string_view clause;
            if (nominal == "Length")
            {
                required = Basis(0);
                clause = "LENGTH_UNIT.WR1";
            }
            if (nominal == "Mass")
            {
                required = Basis(1);
                clause = "MASS_UNIT.WR1";
            }
            if (nominal == "PlaneAngle")
            {
                clause = "PLANE_ANGLE_UNIT.WR1";
            }
            if (nominal == "SolidAngle")
            {
                clause = "SOLID_ANGLE_UNIT.WR1";
            }
            if (!clause.empty())
            {
                const auto& at = Field(r, IsA(r, "SI_UNIT") ? "name" : "dimensions");
                Count(at.declaration.value->span);
                if (node.dimensions != required)
                {
                    Rule(r, at, clause);
                }
            }
        }
    }
    void Measures()
    {
        for (std::size_t r = 0; r < m_data.fields.size(); ++r)
        {
            if (!IsA(r, "MEASURE_WITH_UNIT"))
            {
                continue;
            }
            try
            {
                const auto& unit = Field(r, "unit_component");
                const auto target = Target(*unit.declaration.value);
                const auto& value = Field(r, "value_component");
                const auto& tag = value.declaration.value->text;
                if (tag == "POSITIVE_RATIO_MEASURE")
                {
                    // AP214E3_2010.exp POSITIVE_RATIO_MEASURE.WR1: compare the exact
                    // source REAL, including signed zero/huge exponents, without
                    // changing its typed branch or guessing a quantity from RI.name.
                    Count(value.declaration.value->span);
                    ++m_counts.typePredicates;
                    if (CompareSourceReals(value.declaration.value->children.front().text, "0.") <= 0)
                    {
                        Rule(r, value, "POSITIVE_RATIO_MEASURE.WR1");
                    }
                }
                Dimensions required;
                if (tag == "MASS_MEASURE")
                {
                    required = Basis(1);
                }
                else if (tag == "LENGTH_MEASURE" || tag == "NON_NEGATIVE_LENGTH_MEASURE" || tag == "POSITIVE_LENGTH_MEASURE")
                {
                    required = Basis(0);
                }
                else if (m_data.profile.kind == ProfileKind::Ap203e2GeneralSource && tag == "PLANE_ANGLE_MEASURE")
                {
                    // N2635 valid_units checks only the value branch's dimension.
                    // Nominal angle-unit membership belongs to the MWU subtype
                    // below; generic MWU must not inherit that entity's WR1.
                    required = {};
                }
                else if (tag == "RATIO_MEASURE" || tag == "POSITIVE_RATIO_MEASURE")
                {
                    // valid_units tests TYPEOF: positive_ratio_measure inherits ratio,
                    // so its unit must also be dimensionless. Preserve a density-named
                    // ratio paired with M/L^3 as source data and diagnose the conflict;
                    // the name is not authority to relabel the measure or its unit.
                    required = {};
                }
                else
                {
                    Fail(
                        Status::Unsupported,
                        r,
                        value,
                        "UnsupportedMeasureDimensionBranch",
                        "interpreted length/mass/ratio measure family",
                        tag,
                        "profile dimensional interpretation subset; no guessed dimension"
                    );
                }
                // Only the dimensional branches handled above are interpreted.
                // Remaining families fail explicitly, never by a guessed dimension.
                Count(unit.declaration.value->span);
                if (m_nodes[Index(target)].dimensions != required)
                {
                    Rule(r, unit, "MEASURE_WITH_UNIT.WR1");
                }
                if (IsA(r, "LENGTH_MEASURE_WITH_UNIT"))
                {
                    Count(unit.declaration.value->span);
                    if (!IsA(target, "LENGTH_UNIT"))
                    {
                        Rule(r, unit, "LENGTH_MEASURE_WITH_UNIT.WR1");
                    }
                }
                if (IsA(r, "MASS_MEASURE_WITH_UNIT"))
                {
                    Count(unit.declaration.value->span);
                    if (!IsA(target, "MASS_UNIT"))
                    {
                        Rule(r, unit, "MASS_MEASURE_WITH_UNIT.WR1");
                    }
                }
                if (IsA(r, "PLANE_ANGLE_MEASURE_WITH_UNIT"))
                {
                    // N2635 PLANE_ANGLE_MEASURE_WITH_UNIT.WR1 constrains this
                    // entity's unit_component regardless of value_component's tag.
                    Count(unit.declaration.value->span);
                    if (!IsA(target, "PLANE_ANGLE_UNIT"))
                    {
                        Rule(r, unit, "PLANE_ANGLE_MEASURE_WITH_UNIT.WR1");
                    }
                }
            }
            catch (Stop& stop)
            {
                const auto& key = stop.failure.diagnostic.source.key;
                if (m_data.policy != SourcePolicy::PreserveNonGeometric || !IsA(r, "MEASURE_REPRESENTATION_ITEM") ||
                    stop.failure.status != Status::Rejected ||
                    (key != "SourceRuleViolation:MEASURE_WITH_UNIT.WR1" && key != "SourceRuleViolation:POSITIVE_RATIO_MEASURE.WR1"))
                {
                    throw;
                }
                // This is only a deferred source error. The outer checker must
                // still prove that the MRI belongs to an attached property and
                // is not a body/physical-unit dependency before any publication.
                PreservePending(std::move(stop), r, Field(r, "value_component"));
            }
        }
    }
};
} // namespace
std::optional<UnitDependencyFailure> CheckAp214UnitDependencies(
    SemanticData& data,
    const std::function<void(step_source::Span)>& visit,
    UnitDependencyCounts& checks
)
{
    try
    {
        UnitDependencies(data, visit, checks).Run();
        return {};
    }
    catch (Stop& stop)
    {
        return std::move(stop.failure);
    }
}
} // namespace step_semantics
